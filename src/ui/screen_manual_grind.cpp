#include "ui_common.h"
#include <cstdio>

// ============================================================
// MANUAL GRIND SCREEN -- test motor/kalibrasi grind size, permintaan
// eksplisit untuk kebutuhan kalibrasi burr gap sebelum GBW otomatis
// siap dipakai penuh (HX711 belum tentu terpasang/terkalibrasi saat
// layar ini dipakai -- SENGAJA TIDAK ada syarat HX711 valid di sini,
// beda dari screen Idle/Predictive Grind).
//
// SAFETY (disepakati eksplisit sebelum coding):
// 1. Toggle (bukan hold-to-run) -- tap sekali nyala, tap lagi mati.
// 2. Auto-stop TIMEOUT 20 detik (MANUAL_GRIND_TIMEOUT_MS di main.cpp)
//    -- jaring pengaman kalau operator lupa mematikan.
// 3. Swipe kanan (ui_enable_swipe_home) SENGAJA TIDAK dipasang di
//    layar ini -- BEDA dari kebanyakan layar lain (Idle/Done/
//    Settings) yang dianggap "aman". Layar ini bisa menyalakan motor
//    secara langsung, jadi diperlakukan setara Predictive Grind/
//    Pulse Correction soal larangan gesture "kabur" tersembunyi --
//    satu-satunya jalan keluar adalah tombol Back eksplisit.
// 4. Tombol Back TIDAK di-disable saat motor menyala (disepakati
//    lewat diskusi) -- sebagai gantinya, Back MEMATIKAN MOTOR DULU
//    sebagai bagian dari aksinya, baru navigasi. Tidak ada dead-end.
// ============================================================

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_motor_btn = nullptr;
static lv_obj_t* s_motor_btn_label = nullptr;
static lv_obj_t* s_motor_btn_sublabel = nullptr;
static lv_obj_t* s_warning_label = nullptr;
static lv_obj_t* s_timer_value_label = nullptr;
static lv_obj_t* s_timer_bar_fill = nullptr;

// Semua 3 fungsi ini didefinisikan di main.cpp -- lihat catatan
// lengkap soal isolasi dari GrindController di sana.
extern bool manual_grind_toggle(void);
extern bool manual_grind_is_on(void);
extern int manual_grind_seconds_remaining(void);

extern void ui_close_manual_grind(lv_event_t* e);

#define MANUAL_GRIND_TIMEOUT_S 20  // HARUS SAMA dengan MANUAL_GRIND_TIMEOUT_MS/1000 di main.cpp -- dipakai murni untuk hitung persentase progress bar visual di sini, bukan sumber kebenaran timeout (itu tetap di main.cpp)

static void refresh_motor_button_visual(void) {
    bool isOn = manual_grind_is_on();
    if (isOn) {
        lv_obj_set_style_bg_color(s_motor_btn, lv_color_hex(0x2e1712), 0);  // merah gelap, senada COLOR_WARN
        lv_obj_set_style_border_color(s_motor_btn, COLOR_WARN, 0);
        lv_label_set_text(s_motor_btn_label, "X STOP MOTOR");
        lv_obj_set_style_text_color(s_motor_btn_label, COLOR_WARN, 0);
        lv_label_set_text(s_motor_btn_sublabel, "Tap to stop");
        lv_label_set_text(s_warning_label, "MOTOR RUNNING - tap the red button to stop anytime.");
        lv_obj_set_style_text_color(s_warning_label, COLOR_WARN, 0);
    } else {
        lv_obj_set_style_bg_color(s_motor_btn, lv_color_hex(0x14251a), 0);  // hijau gelap, senada COLOR_SUCCESS
        lv_obj_set_style_border_color(s_motor_btn, COLOR_SUCCESS, 0);
        lv_label_set_text(s_motor_btn_label, "> START MOTOR");
        lv_obj_set_style_text_color(s_motor_btn_label, COLOR_SUCCESS, 0);
        lv_label_set_text(s_motor_btn_sublabel, "Tap to start");
        lv_label_set_text(s_warning_label, "WARNING: this directly powers the grinder motor. Keep hands and foreign objects away from the burr.");
        lv_obj_set_style_text_color(s_warning_label, COLOR_TEXT_SECONDARY, 0);
    }
}

static void motor_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // Return value SENGAJA TIDAK dicek di sini (beda dari grind_start()
    // di ui_start_grind()) -- manual_grind_toggle() cuma bisa gagal
    // saat MENYALAKAN (guard GrindController aktif), TIDAK PERNAH
    // gagal saat MEMATIKAN. Kalau ditolak, motor tetap OFF secara
    // logic (tidak ada state UI yang perlu "dibatalkan" seperti kasus
    // ui_desync dulu) -- refresh_motor_button_visual() di bawah tetap
    // menampilkan state yang benar (OFF) apa pun hasilnya, jadi aman
    // tanpa perlu propagate return value ke sini. Pesan TOLAK spesifik
    // (kalau ditolak) sudah di-print ke Serial oleh manual_grind_toggle()
    // sendiri.
    manual_grind_toggle();
    refresh_motor_button_visual();
}

static void back_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // URUTAN PENTING (disepakati eksplisit): matikan motor DULU kalau
    // masih menyala, BARU navigasi keluar -- supaya operator tidak
    // bisa "meninggalkan" motor menyala di layar lain manapun tanpa
    // sadar. manual_grind_toggle() sendiri sudah handle "kalau sedang
    // ON, matikan" (lihat main.cpp) -- SATU pemanggilan ini cukup,
    // tidak perlu cek manual_grind_is_on() dulu di sini.
    if (manual_grind_is_on()) {
        manual_grind_toggle();
    }
    ui_close_manual_grind(e);
}

// Dipanggil dari ui_tick() (main.cpp) TIAP frame selama layar ini
// aktif -- update tombol (kalau auto-stop timeout terjadi di
// background, visual tombol harus ikut berubah OFF walau operator
// tidak sempat tap apa pun) + countdown timer.
void ui_screen_manual_grind_update(void) {
    if (s_screen == nullptr) return;

    bool isOn = manual_grind_is_on();
    int secondsRemaining = manual_grind_seconds_remaining();

    // Refresh visual tombol HANYA kalau state-nya beda dari yang
    // terakhir ditampilkan -- dicek via warna border saat ini
    // (murah, tidak perlu variable static tambahan cuma untuk ini).
    // Auto-stop timeout (dipicu main.cpp, bukan tap operator) adalah
    // SATU-SATUNYA jalur yang bisa membuat state berubah TANPA lewat
    // motor_btn_cb() -- makanya perlu refresh eksplisit di sini,
    // bukan cuma di dalam callback.
    static bool s_lastKnownOn = false;
    if (isOn != s_lastKnownOn) {
        refresh_motor_button_visual();
        s_lastKnownOn = isOn;
    }

    if (isOn) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", secondsRemaining);
        lv_label_set_text(s_timer_value_label, buf);
        lv_obj_set_style_text_color(s_timer_value_label, COLOR_ACCENT, 0);

        int pct = (secondsRemaining * 100) / MANUAL_GRIND_TIMEOUT_S;
        if (pct > 100) pct = 100;
        if (pct < 0) pct = 0;
        lv_obj_set_width(s_timer_bar_fill, (200 * pct) / 100);
    } else {
        lv_label_set_text(s_timer_value_label, "--");
        lv_obj_set_style_text_color(s_timer_value_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_width(s_timer_bar_fill, 0);
    }
}

lv_obj_t* ui_screen_manual_grind_create(void) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    // TIDAK ada ui_enable_swipe_home() di sini -- lihat catatan safety
    // lengkap di komentar header file ini.

    ui_create_status_bar(s_screen, nullptr);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "MANUAL GRIND");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 14);

    // Warning box -- teks berubah tergantung motor ON/OFF, lihat
    // refresh_motor_button_visual().
    lv_obj_t* warning_box = lv_obj_create(s_screen);
    lv_obj_set_size(warning_box, SCREEN_WIDTH - 40, 60);
    lv_obj_align(warning_box, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_bg_color(warning_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(warning_box, 1, 0);
    lv_obj_set_style_border_color(warning_box, COLOR_WARN, 0);
    lv_obj_set_style_radius(warning_box, 10, 0);
    lv_obj_set_style_pad_all(warning_box, 8, 0);
    lv_obj_clear_flag(warning_box, LV_OBJ_FLAG_SCROLLABLE);

    s_warning_label = lv_label_create(warning_box);
    lv_label_set_long_mode(s_warning_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_warning_label, SCREEN_WIDTH - 40 - 16);
    lv_obj_set_style_text_font(s_warning_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_warning_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_warning_label);

    // Tombol motor besar -- toggle START/STOP, warna berubah lewat
    // refresh_motor_button_visual().
    s_motor_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_motor_btn, 220, 90);
    lv_obj_align(s_motor_btn, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 130);
    lv_obj_set_style_radius(s_motor_btn, 20, 0);
    lv_obj_set_style_border_width(s_motor_btn, 2, 0);
    lv_obj_add_event_cb(s_motor_btn, motor_btn_cb, LV_EVENT_CLICKED, NULL);

    s_motor_btn_label = lv_label_create(s_motor_btn);
    lv_obj_set_style_text_font(s_motor_btn_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_motor_btn_label, LV_ALIGN_CENTER, 0, -8);

    s_motor_btn_sublabel = lv_label_create(s_motor_btn);
    lv_obj_set_style_text_font(s_motor_btn_sublabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_motor_btn_sublabel, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(s_motor_btn_sublabel, LV_ALIGN_CENTER, 0, 16);

    // Countdown timer
    lv_obj_t* timer_label = lv_label_create(s_screen);
    lv_label_set_text(timer_label, "AUTO-STOP TIMEOUT");
    lv_obj_set_style_text_font(timer_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(timer_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(timer_label, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 250);

    s_timer_value_label = lv_label_create(s_screen);
    lv_label_set_text(s_timer_value_label, "--");
    lv_obj_set_style_text_font(s_timer_value_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_timer_value_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(s_timer_value_label, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 266);

    lv_obj_t* timer_bar_track = lv_obj_create(s_screen);
    lv_obj_set_size(timer_bar_track, 200, 6);
    lv_obj_align(timer_bar_track, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 316);
    lv_obj_set_style_bg_color(timer_bar_track, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(timer_bar_track, 0, 0);
    lv_obj_set_style_radius(timer_bar_track, 3, 0);
    lv_obj_set_style_pad_all(timer_bar_track, 0, 0);
    lv_obj_clear_flag(timer_bar_track, LV_OBJ_FLAG_SCROLLABLE);

    s_timer_bar_fill = lv_obj_create(timer_bar_track);
    lv_obj_set_size(s_timer_bar_fill, 0, 6);
    lv_obj_set_pos(s_timer_bar_fill, 0, 0);
    lv_obj_set_style_bg_color(s_timer_bar_fill, COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(s_timer_bar_fill, 0, 0);
    lv_obj_set_style_radius(s_timer_bar_fill, 3, 0);
    lv_obj_clear_flag(s_timer_bar_fill, LV_OBJ_FLAG_SCROLLABLE);

    // Tombol Back -- SELALU aktif (tidak pernah disabled), mematikan
    // motor dulu sebagai bagian aksinya kalau masih menyala. Lihat
    // catatan lengkap di back_btn_cb().
    lv_obj_t* back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(back_btn, 220, 60);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_set_style_radius(back_btn, 30, 0);
    lv_obj_add_event_cb(back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "< BACK TO SETTINGS");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(back_label, COLOR_ACCENT, 0);
    lv_obj_center(back_label);

    refresh_motor_button_visual();

    return s_screen;
}
