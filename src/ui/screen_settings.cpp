#include "ui_common.h"
#include "../../include/config.h"
#include <cstdio>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: snprintf() dipakai di bawah tanpa include ini
#include "../github_ota.h"  // GithubOtaManager -- tombol "Check for Update" menggantikan baris IP Address (lihat catatan create_update_row())

// ============================================================
// SETTINGS SCREEN -- SENGAJA HANYA 2 parameter: Tolerance & Max
// Pulses, KEDUANYA MANUAL. Per keputusan eksplisit (lihat riwayat
// diskusi): Coast Ratio dan Flow Threshold TIDAK ditampilkan di sini
// sama sekali -- TIDAK ADA toggle Auto/Manual, TIDAK ADA persistence/
// learning state/EMA/logic "learned" untuk kedua parameter itu.
// Mereka baru disentuh UI setelah keputusan adaptive learning
// dikunci terpisah. Jangan tambah field lain ke screen ini tanpa
// keputusan eksplisit yang sama.
// ============================================================

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_tolerance_value = nullptr;
static lv_obj_t* s_max_pulses_value = nullptr;

extern void ui_close_settings(lv_event_t* e);
extern void ui_enable_swipe_home(lv_obj_t* screen);  // swipe kanan -> Set Target (Home)

// Counter repeat per tombol (independen satu sama lain) -- reset ke
// 0 tiap kali sesi tekan baru dimulai (LV_EVENT_PRESSED), naik tiap
// LV_EVENT_LONG_PRESSED_REPEAT. Lihat ui_repeat_step_multiplier() di
// ui_common.h untuk kurva percepatannya.
static int s_tolerance_minus_repeat = 0;
static int s_tolerance_plus_repeat = 0;
static int s_max_pulses_minus_repeat = 0;
static int s_max_pulses_plus_repeat = 0;

static void tolerance_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_tolerance_minus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_tolerance_minus_repeat++;
    float step = 0.01f * ui_repeat_step_multiplier(s_tolerance_minus_repeat);
    g_ui_state.accuracy_tolerance_g -= step;
    if (g_ui_state.accuracy_tolerance_g < 0.01f) g_ui_state.accuracy_tolerance_g = 0.01f;  // batas bawah wajar, jangan 0
    char buf[8];
    snprintf(buf, sizeof(buf), "%.2f", g_ui_state.accuracy_tolerance_g);
    lv_label_set_text(s_tolerance_value, buf);
}

static void tolerance_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_tolerance_plus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_tolerance_plus_repeat++;
    float step = 0.01f * ui_repeat_step_multiplier(s_tolerance_plus_repeat);
    g_ui_state.accuracy_tolerance_g += step;
    if (g_ui_state.accuracy_tolerance_g > 1.0f) g_ui_state.accuracy_tolerance_g = 1.0f;  // batas atas wajar
    char buf[8];
    snprintf(buf, sizeof(buf), "%.2f", g_ui_state.accuracy_tolerance_g);
    lv_label_set_text(s_tolerance_value, buf);
}

static void max_pulses_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_max_pulses_minus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_max_pulses_minus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_max_pulses_minus_repeat);
    g_ui_state.max_pulse_attempts -= step;
    if (g_ui_state.max_pulse_attempts < 1) g_ui_state.max_pulse_attempts = 1;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_ui_state.max_pulse_attempts);
    lv_label_set_text(s_max_pulses_value, buf);
}

static void max_pulses_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_max_pulses_plus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_max_pulses_plus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_max_pulses_plus_repeat);
    g_ui_state.max_pulse_attempts += step;
    if (g_ui_state.max_pulse_attempts > 30) g_ui_state.max_pulse_attempts = 30;  // batas atas wajar
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_ui_state.max_pulse_attempts);
    lv_label_set_text(s_max_pulses_value, buf);
}

static void save_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // TODO: persist ke NVS/flash kalau memang mau bertahan lintas
    // reboot -- BELUM diimplementasikan (di luar scope keputusan
    // sekarang, cuma runtime in-memory dulu lewat g_ui_state).
    ui_close_settings(e);
}

// Helper bikin satu baris param manual (nama+deskripsi di atas,
// stepper -/value/+ di BARIS TERPISAH di bawahnya), sesuai mockup
// .param-row versi Manual (tanpa toggle Auto/Manual maupun info
// "learned").
//
// LAYOUT DIUBAH (permintaan eksplisit -- foto hardware fisik
// menunjukkan tombol +/- 26x26px terlalu kecil/sulit dipencet
// dibanding standar tombol lain di firmware ini, mis. 110x60 di Set
// Target): stepper dipindah dari sejajar-kanan nama parameter ke
// baris terpisah di bawah.
//
// KOREKSI (dilaporkan lewat foto hardware fisik -- 2 row param DAN
// row IP address SALING TUMPUK): tinggi row SEBELUMNYA 118px tapi
// offset Y row kedua/IP TIDAK ikut disesuaikan (bug ceroboh dari sisi
// saya, murni salah hitung, bukan salah desain) -- row Max Pulses
// (offset lama +116) dimulai SEBELUM row Tolerance (tinggi 118,
// berakhir di +166) selesai. KOREKSI GANDA: (1) tinggi row diturunkan
// 118->92 (masih jauh lebih lega dari original 58, cukup untuk 2
// baris teks + 1 baris stepper 65x46 tanpa perlu setinggi itu), (2)
// SEMUA offset Y (row Tolerance/Max Pulses/IP/Save) dihitung ulang
// dari 0 secara eksplisit di ui_screen_settings_create() -- lihat
// komentar perhitungan di sana, supaya tidak terulang lagi kalau
// tinggi row berubah lagi di masa depan.
// KOREKSI KEDUA (dilaporkan lewat foto hardware fisik -- MASIH
// tumpang tindih setelah fix pertama, kali ini deskripsi teks
// ("Accuracy window (g)"/"Pulse attempt limit") tertindih stepper_row
// di BAWAHNYA, bukan soal horizontal seperti fix pertama): row_h=92
// dengan pad_all=8 menyisakan area konten cuma 76px, sementara
// name_label+desc_label+stepper_row (46-50px) butuh sekitar 86px
// total -- KEKURANGAN RUANG VERTIKAL, bukan salah posisi X. FIX:
// row_h dinaikkan 92->96, pad_all diturunkan 8->6 (memberi lebih
// banyak ruang konten efektif tanpa membuat row terlalu tinggi),
// SEMUA offset Y turunan (row2/IP/Save) dihitung ulang lagi dari nol
// -- lihat perhitungan lengkap di ui_screen_settings_create().
static void create_param_row(lv_obj_t* parent, int y_offset, const char* name, const char* desc,
                               lv_obj_t** out_value_label, lv_event_cb_t minus_cb, lv_event_cb_t plus_cb,
                               const char* initial_value) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 100);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_set_style_bg_color(row, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* desc_label = lv_label_create(row);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align_to(desc_label, name_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    // Stepper -/value/+ di BARIS BAWAH -- PAKAI FLEXBOX (row terpisah
    // di dalam row utama), BUKAN lv_obj_align manual dengan offset X
    // tetap. KOREKSI (dilaporkan lewat foto hardware fisik -- value
    // label "0.03"/"10" TERTINDIH tombol -/+, bukan rapi di celah
    // tengah): perhitungan offset X manual (-66/+66 dari BOTTOM_MID)
    // sudah 2x meleset (sekali soal tinggi row, sekali soal ini) --
    // flexbox SPACE_EVENLY membiarkan LVGL yang menghitung jarak
    // otomatis berdasarkan lebar aktual tiap child, jauh lebih tahan
    // terhadap perubahan ukuran font/tombol di masa depan dibanding
    // offset piksel manual yang gampang meleset kalau salah satu
    // ukuran berubah tapi yang lain lupa disesuaikan.
    lv_obj_t* stepper_row = lv_obj_create(row);
    lv_obj_set_size(stepper_row, SCREEN_WIDTH - 40 - 12, 46);  // -12 = kompensasi pad_all row induk (6px x2)
    lv_obj_align(stepper_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(stepper_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stepper_row, 0, 0);
    lv_obj_set_style_pad_all(stepper_row, 0, 0);
    lv_obj_set_flex_flow(stepper_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stepper_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(stepper_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* minus_btn = lv_btn_create(stepper_row);
    lv_obj_set_size(minus_btn, 65, 46);
    lv_obj_set_style_radius(minus_btn, 14, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(0x2a2412), 0);
    lv_obj_set_style_border_width(minus_btn, 1, 0);
    lv_obj_set_style_border_color(minus_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_add_event_cb(minus_btn, minus_cb, LV_EVENT_ALL, NULL);  // LV_EVENT_ALL -- perlu PRESSED (reset counter) & LONG_PRESSED_REPEAT (percepatan), bukan cuma CLICKED
    lv_obj_t* minus_label = lv_label_create(minus_btn);
    lv_label_set_text(minus_label, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(minus_label, COLOR_ACCENT, 0);
    lv_obj_center(minus_label);

    lv_obj_t* value_label = lv_label_create(stepper_row);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(value_label, COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(value_label, initial_value);
    *out_value_label = value_label;

    lv_obj_t* plus_btn = lv_btn_create(stepper_row);
    lv_obj_set_size(plus_btn, 65, 46);
    lv_obj_set_style_radius(plus_btn, 14, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(0x2a2412), 0);
    lv_obj_set_style_border_width(plus_btn, 1, 0);
    lv_obj_set_style_border_color(plus_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_add_event_cb(plus_btn, plus_cb, LV_EVENT_ALL, NULL);  // LV_EVENT_ALL -- lihat catatan di minus_btn di atas
    lv_obj_t* plus_label = lv_label_create(plus_btn);
    lv_label_set_text(plus_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(plus_label, COLOR_ACCENT, 0);
    lv_obj_center(plus_label);
}

// ------------------------------------------------------------
// Baris "Check for Update" (GANTI dari baris IP Address -- keputusan
// eksplisit setelah pindah ke GitHub OTA, IP address device sudah
// tidak relevan ditampilkan untuk keperluan OTA lagi, karena OTA
// sekarang bukan device yang dituju laptop, tapi device yang menarik
// data dari GitHub). Tombol ini memanggil githubOta.checkAndUpdate()
// -- BLOCKING (LVGL sengaja tidak responsif selama proses ini, sama
// filosofinya dengan ArduinoOTA dulu saat upload berlangsung), lalu
// menampilkan status/progress lewat label di baris ini.
// ------------------------------------------------------------
static lv_obj_t* s_update_status_label = nullptr;

static void check_update_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_label_set_text(s_update_status_label, "Checking...");
    // lv_refr_now(NULL) -- paksa LVGL render SEKARANG (bukan menunggu
    // giliran lv_timer_handler() di loop() berikutnya), supaya label
    // "Checking..." di atas sempat tampil SEBELUM checkAndUpdate() di
    // bawah BLOCKING (bisa beberapa detik/menit tergantung ukuran
    // firmware & kecepatan internet). API ini standar LVGL v8 (disp
    // NULL = display default/aktif) -- SAMA seperti fungsi LVGL v8
    // lain di codebase ini, TAPI belum pernah dipakai di file manapun
    // sebelumnya, jadi tetap perlu dikonfirmasi lewat compile aktual
    // sebelum dianggap pasti benar (prinsip project: jangan klaim
    // sudah benar tanpa bukti compile).
    lv_refr_now(NULL);

    githubOta.checkAndUpdate();

    // BARIS DI BAWAH INI HANYA TERCAPAI KALAU checkAndUpdate() GAGAL --
    // kalau sukses, device sudah reboot duluan (lihat catatan di
    // github_ota.cpp GithubOtaManager::checkAndUpdate(), kasus
    // HTTP_UPDATE_OK) dan baris ini tidak akan pernah dieksekusi.
    lv_label_set_text(s_update_status_label, githubOta.statusText());
}

static void create_update_row(lv_obj_t* parent, int y_offset) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 58);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_set_style_bg_color(row, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, "Firmware Update");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_update_status_label = lv_label_create(row);
    lv_label_set_text(s_update_status_label, "via GitHub");
    lv_obj_set_style_text_font(s_update_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_update_status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align_to(s_update_status_label, name_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t* check_btn = lv_btn_create(row);
    lv_obj_set_size(check_btn, 90, 36);
    lv_obj_set_style_radius(check_btn, 12, 0);
    lv_obj_set_style_bg_color(check_btn, lv_color_hex(0x2a2412), 0);
    lv_obj_set_style_border_width(check_btn, 1, 0);
    lv_obj_set_style_border_color(check_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_align(check_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(check_btn, check_update_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* check_label = lv_label_create(check_btn);
    lv_label_set_text(check_label, "CHECK");
    lv_obj_set_style_text_font(check_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(check_label, COLOR_ACCENT, 0);
    lv_obj_center(check_label);
}

lv_obj_t* ui_screen_settings_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, nullptr);  // tidak ada gear di dalam Settings sendiri

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "GRIND PARAMETERS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    char tol_buf[8];
    snprintf(tol_buf, sizeof(tol_buf), "%.2f", g_ui_state.accuracy_tolerance_g);
    // PERHITUNGAN OFFSET (v3 -- row_h dinaikkan 96->100 untuk menambah
    // margin aman internal row dari 3px ke 7px, lihat catatan lengkap
    // riwayat perbaikan di create_param_row()). title berakhir sekitar
    // Y=18+20=38, row1 mulai Y=36.
    create_param_row(s_screen, STATUS_BAR_HEIGHT + 36, "Tolerance", "Accuracy window (g)",
                      &s_tolerance_value, tolerance_minus_cb, tolerance_plus_cb, tol_buf);

    char pulse_buf[8];
    snprintf(pulse_buf, sizeof(pulse_buf), "%d", g_ui_state.max_pulse_attempts);
    // row2 mulai SETELAH row1 selesai: 36 + tinggi_row(100) + gap(8) = 144.
    create_param_row(s_screen, STATUS_BAR_HEIGHT + 144, "Max Pulses", "Pulse attempt limit",
                      &s_max_pulses_value, max_pulses_minus_cb, max_pulses_plus_cb, pulse_buf);

    // Catatan: Coast Ratio & Flow Threshold SENGAJA TIDAK ADA di sini
    // -- lihat komentar header file ini untuk alasannya.

    // Update row mulai SETELAH row2 selesai: 144 + 100 + gap(8) = 252.
    // (252+58=310 relatif status bar, +22=332 absolut; Save mulai
    // Y=368 absolut -- margin aman 36px, TIDAK overlap -- SAMA POSISI
    // dengan bekas IP row, ukurannya identik 58px jadi tidak perlu
    // hitung ulang.)
    create_update_row(s_screen, STATUS_BAR_HEIGHT + 252);

    lv_obj_t* save_btn = lv_btn_create(s_screen);
    // KOREKSI (dilaporkan -- ukuran beda dari tombol lain): SEBELUMNYA
    // 200x48, sisa dari sebelum standarisasi v18 (lihat spec tunggal
    // 220x60 / offset -28 di komentar screen_idle.cpp start_btn) --
    // Settings terlewat saat standarisasi awal. Disamakan di sini.
    lv_obj_set_size(save_btn, 220, 60);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(save_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(save_btn, 30, 0);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "SAVE");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_14, 0);  // KOREKSI -- konsisten dengan font tombol besar lain (Start/Stop), lihat catatan ukuran save_btn di atas
    lv_obj_set_style_text_color(save_label, lv_color_hex(0x1a1305), 0);
    lv_obj_center(save_label);

    ui_enable_swipe_home(s_screen);  // layar aman -- swipe kanan boleh aktif

    return s_screen;
}
