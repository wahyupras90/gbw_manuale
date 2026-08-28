#pragma once

#include <lvgl.h>

// ============================================================
// PALET WARNA -- sesuai visual language mockup HTML yang sudah
// disetujui (brass/copper accent, hijau untuk sukses/pulse state).
// ============================================================
#define COLOR_BG           lv_color_hex(0x060605)  // dark background, sesuai mockup --bg
#define COLOR_BG_CARD      lv_color_hex(0x1a1a1a)  // card/panel sedikit lebih terang
#define COLOR_ACCENT       lv_color_hex(0xC9A15F)  // brass -- sesuai mockup --brass
#define COLOR_ACCENT_DIM   lv_color_hex(0x6b5a3a)  // brass-dim, sesuai mockup --brass-dim
#define COLOR_SUCCESS       lv_color_hex(0x7FB069)  // hijau -- sesuai mockup --success
#define COLOR_WARN          lv_color_hex(0xC1543C)  // merah/warn -- sesuai mockup --warn
#define COLOR_TEXT_PRIMARY   lv_color_hex(0xF2EFE9)  // sesuai mockup --text
#define COLOR_TEXT_SECONDARY lv_color_hex(0x7a7670)  // sesuai mockup --text-dim
#define COLOR_ARC_BG        lv_color_hex(0x1c1911)  // sesuai mockup --ring-track

// ============================================================
// UKURAN LAYAR -- 280x456, AMOLED round-corner Waveshare
// ============================================================
#define SCREEN_WIDTH   280
#define SCREEN_HEIGHT  456
#define STATUS_BAR_HEIGHT  22  // sesuai budget vertikal mockup (statusbar 22px)

// ============================================================
// STATE GRIND -- dipakai lintas screen untuk tahu fase sekarang
// ============================================================
typedef enum {
    UI_SCREEN_SET_TARGET,
    UI_SCREEN_IDLE,
    UI_SCREEN_PREDICTIVE_GRIND,
    UI_SCREEN_PULSE_CORRECTION,
    UI_SCREEN_DONE,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_MANUAL_GRIND,  // test motor/kalibrasi grind size, akses dari Settings (scroll)
    UI_SCREEN_DEBUG,  // debug HX711/validasi grind tanpa Serial, akses dari Settings (scroll)
} ui_screen_id_t;

// Data yang dibagi ke semua screen -- di-update oleh GrindController
// lewat main.cpp (bukan langsung, lihat catatan dependency di
// grind_controller.h: Controller -> business logic, main.cpp yang
// sync ke sini, UI cuma presentation).
//
// PENTING (per keputusan urutan kerja terkini): field
// coast_ratio/flow_threshold_gps SENGAJA TIDAK ADA di sini --
// Settings screen HANYA expose Tolerance & Max Pulses (manual).
// Coast Ratio & Flow Threshold BELUM disentuh UI sampai keputusan
// adaptive learning dikunci -- lihat screen_settings.cpp.
typedef struct {
    // target_weight_g -- DOSE TAMBAHAN yang diminta operator (mis. 18.0
    // untuk "tambah 18 gram dari berat sekarang"), BUKAN berat absolut
    // akhir yang harus dicapai timbangan. HANYA dipakai untuk LABEL
    // ("TARGET 18.0g" di screen Set Target/Idle/Predictive Grind) --
    // JANGAN dipakai untuk kalkulasi progress/error, itu perlu
    // target_absolute_g (lihat di bawah). Ini bug yang pernah terjadi:
    // draft sebelumnya sempat memakai target_weight_g langsung untuk
    // hitung error di screen Done dan progress ring Predictive Grind,
    // padahal GrindController bekerja dengan target ABSOLUT
    // (startWeight + dose) -- hasilnya angka error/progress yang
    // ditampilkan salah total (mis. error dihitung dari dose 18g,
    // bukan dari target absolut ~250+18=268g).
    float target_weight_g;

    // target_absolute_g -- BERAT ABSOLUT yang harus dicapai timbangan
    // (target_weight_g/dose + start_weight_g), DIISI dari
    // GrindController::targetAbsoluteG() lewat syncGrindControllerToUi()
    // di main.cpp SETELAH startGrind() dipanggil (0 sebelum grind
    // pertama dimulai -- lihat inisialisasi default di
    // ui_screen_manager.cpp). SEMUA kalkulasi error/progress yang
    // membandingkan terhadap "target" HARUS pakai field ini, BUKAN
    // target_weight_g.
    float target_absolute_g;

    // start_weight_g -- berat timbangan SAAT startGrind() dipanggil
    // (tare point), DIISI dari GrindController::startWeightG(). Dipakai
    // bersama target_absolute_g untuk hitung progress dose-based yang
    // benar: (current_weight_g - start_weight_g) / (target_absolute_g -
    // start_weight_g), BUKAN current_weight_g / target_weight_g
    // (progress murni berbasis dose, target_weight_g cuma buat label).
    float start_weight_g;

    float current_weight_g;
    float flow_rate_gps;        // flow_now real-time, NAN kalau belum valid (lihat GrindController::currentFlowGps())
    bool  flow_start_confirmed; // dari GrindController::flowStartConfirmed() -- state WAIT_FLOW_START vs GRINDING
    unsigned long grind_latency_ms;  // dari GrindController::grindLatencyMs(), 0 kalau belum confirmed
    int   pulse_count;
    float pulse_error_g;
    unsigned long grind_duration_ms;
    bool  grind_success;        // true = GrindResult::SUCCESS, false = INACCURATE/ABORTED (dipakai screen Done)

    // Status bar (kiri): WiFi = dari OtaManager::isWifiConnected() --
    // MURNI indikator status OTA connectivity, TIDAK ADA hubungannya
    // dengan kesehatan proses grinding (WiFi cuma dipakai untuk OTA,
    // lihat ota_manager.h). BLE = PLACEHOLDER, SELALU false -- belum
    // diimplementasikan, jangan diubah jadi true sampai fitur remote
    // monitoring/app companion benar-benar ada.
    bool  wifi_connected;
    bool  ble_connected;

    // Editable manual di Settings (Tolerance & Max Pulses saja --
    // lihat catatan di atas soal Coast Ratio/Flow Threshold).
    float accuracy_tolerance_g;   // default dari GRIND_ACCURACY_TOLERANCE_G, bisa diubah user
    int   max_pulse_attempts;     // default dari GRIND_MAX_PULSE_ATTEMPTS, bisa diubah user
} ui_shared_state_t;

extern ui_shared_state_t g_ui_state;

// ============================================================
// HELPER -- style umum dipakai berulang di banyak screen
// ============================================================
inline void ui_apply_screen_bg(lv_obj_t* scr) {
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

inline lv_obj_t* ui_create_status_bar(lv_obj_t* parent, void (*gear_cb)(lv_event_t*)) {
    lv_obj_t* bar = lv_obj_create(parent);
    // CATATAN (permintaan eksplisit -- "semua icon di status bar
    // masih kecil, apakah ada ruang?"): STATUS_BAR_HEIGHT TIDAK
    // dinaikkan -- konstanta ini dipakai sebagai basis offset posisi
    // di BANYAK layar (arc, phase pill, preset row, dst di
    // screen_*.cpp), menaikkannya akan menggeser SEMUA elemen itu
    // turun sekaligus dan berisiko menabrak elemen lain yang baru saja
    // di-tune jaraknya (preset_row/confirm_btn di screen_set_target.cpp
    // misalnya cuma py 26px). Sebagai gantinya, font icon diperbesar
    // ke maksimal yang tersedia (14px) dan DIBIARKAN overflow sedikit
    // di luar bar 22px -- LVGL tidak clip child secara default kecuali
    // LV_OBJ_FLAG_CLIP_CORNER diset (tidak diset di sini), jadi ini
    // aman secara visual, cuma bar-nya sendiri yang tetap 22px tinggi.
    lv_obj_set_size(bar, SCREEN_WIDTH, STATUS_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi + BLE indicator (kiri). WiFi = status OTA connectivity
    // (lihat ota_manager.h -- WiFi HANYA dipakai untuk OTA, TIDAK
    // pernah untuk kontrol grind, jadi indikator ini murni informasi
    // "siap update firmware", bukan indikasi kesehatan proses
    // grinding). BLE = PLACEHOLDER, SELALU tampil OFF/abu-abu --
    // belum diimplementasikan sama sekali (dibahas terpisah nanti
    // untuk remote monitoring/app companion, di luar scope saat ini).
    // JANGAN aktifkan warna ON untuk BLE sampai fitur itu benar-benar
    // ada -- lihat ui_update_status_bar_dots().
    lv_obj_t* wifi_icon = lv_label_create(bar);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    // DIPERBESAR (permintaan eksplisit): font 14 adalah MAKSIMAL yang
    // tersedia untuk ukuran status bar ini -- font Montserrat yang
    // di-enable di lv_conf.h cuma 12/14/32 (lihat catatan di sana),
    // 32 terlalu besar untuk status bar setinggi ini. Eksplisit set ke
    // 14 (sebelumnya pakai LV_FONT_DEFAULT tanpa override, yang
    // kebetulan juga 14, tapi sekarang dieksplisitkan supaya jelas &
    // konsisten dengan ble_icon/gear_label).
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wifi_icon, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* ble_icon = lv_label_create(bar);
    // KOREKSI (ditemukan lewat foto hardware fisik yang sedang jalan):
    // LV_SYMBOL_BLUETOOTH tampil sebagai kotak kosong di physical
    // display, walau LV_SYMBOL_WIFI di baris atas tampil normal --
    // font Montserrat bawaan LVGL 8.4.0 punya subset glyph simbol yang
    // TIDAK menyertakan semua LV_SYMBOL_* untuk ukuran font 14 (WiFi
    // termasuk, Bluetooth tidak -- ini variasi normal build resmi
    // LVGL, dikonfirmasi lewat dokumentasi resmi: font tanpa glyph
    // yang diminta akan render kotak kosong kecuali ada fallback
    // font). Diganti ke teks ASCII biasa ("BLE") yang pasti didukung
    // font manapun, alih-alih menambah fallback font terpisah cuma
    // untuk satu simbol.
    lv_label_set_text(ble_icon, "BLE");
    // DIPERBESAR (permintaan eksplisit): 12 -> 14, maksimal yang
    // tersedia (lihat catatan di wifi_icon di atas). Posisi X (36)
    // disesuaikan dari 28 supaya tidak menumpuk dengan wifi_icon yang
    // sekarang sedikit lebih lebar di font 14.
    lv_obj_set_style_text_font(ble_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ble_icon, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(ble_icon, LV_ALIGN_LEFT_MID, 36, 0);

    // Gear icon (kanan) -- buka Settings dari screen manapun
    lv_obj_t* gear_btn = lv_btn_create(bar);
    // DIPERBESAR (permintaan eksplisit -- gear icon terlalu kecil
    // untuk disentuh): dari 24x24 ke 34x34. Status bar tetap
    // STATUS_BAR_HEIGHT (22px) -- tombol boleh visually overflow
    // sedikit di luar status bar (LVGL tidak clip child secara
    // default kecuali LV_OBJ_FLAG_CLIP_CORNER diset), area yang lebih
    // besar meningkatkan touch target tanpa mengubah tinggi status bar
    // itu sendiri.
    lv_obj_set_size(gear_btn, 34, 34);
    lv_obj_align(gear_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_opa(gear_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gear_btn, 0, 0);
    if (gear_cb) {
        lv_obj_add_event_cb(gear_btn, gear_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t* gear_label = lv_label_create(gear_btn);
    lv_label_set_text(gear_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gear_label, &lv_font_montserrat_14, 0);  // DIPERBESAR seiring gear_btn (permintaan eksplisit, gear icon terlalu kecil)
    lv_obj_set_style_text_color(gear_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_center(gear_label);

    return bar;
}

// Update dot indicator SCALE/PLUG di status bar -- dipanggil dari
// ui_tick() supaya status hardware selalu terkini di semua screen.
// Update warna icon WiFi/BLE di status bar -- dipanggil dari
// ui_tick() (perlu ditambahkan pemanggilannya di ui_screen_manager.cpp)
// supaya status OTA connectivity selalu terkini di semua screen.
// WiFi ikut g_ui_state.wifi_connected (dari OtaManager::isWifiConnected()
// via main.cpp). BLE SELALU abu-abu/OFF -- placeholder, lihat catatan
// di ui_create_status_bar().
inline void ui_update_status_bar_dots(lv_obj_t* bar) {
    if (bar == nullptr) return;
    lv_obj_t* wifi_icon = lv_obj_get_child(bar, 0);
    lv_obj_t* ble_icon = lv_obj_get_child(bar, 1);
    if (wifi_icon) {
        lv_obj_set_style_text_color(wifi_icon, g_ui_state.wifi_connected ? COLOR_SUCCESS : COLOR_TEXT_SECONDARY, 0);
    }
    if (ble_icon) {
        // SELALU abu-abu -- lihat catatan placeholder di atas.
        lv_obj_set_style_text_color(ble_icon, COLOR_TEXT_SECONDARY, 0);
    }
}

// Label angka berat, monospace-style (tabular figures) supaya digit
// tidak geser lebar saat update.
inline lv_obj_t* ui_create_weight_label(lv_obj_t* parent) {
    lv_obj_t* label = lv_label_create(parent);
    // DIPERBESAR (permintaan eksplisit -- "target weight...tidak
    // proporsional"): 32px -> 40px, konsisten di SEMUA layar yang
    // pakai helper ini (Idle/Predictive Grind/Pulse Correction/Done).
    // Font baru ditambahkan ke lv_conf.h (LV_FONT_MONTSERRAT_40).
    lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(label, "0.00g");
    return label;
}

// Phase label pill -- dipakai di Idle/Predictive/Pulse/Done, sesuai
// mockup (.phase-label dengan varian idle/active/pulse/done).
inline lv_obj_t* ui_create_phase_label(lv_obj_t* parent) {
    lv_obj_t* pill = lv_obj_create(parent);
    lv_obj_set_height(pill, 22);
    lv_obj_set_width(pill, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(pill, 11, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_pad_hor(pill, 14, 0);
    lv_obj_set_style_pad_ver(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(pill);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);

    return pill;
}

inline void ui_set_phase_label(lv_obj_t* pill, const char* text, lv_color_t fg, lv_color_t bg) {
    lv_obj_set_style_bg_color(pill, bg, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_t* label = lv_obj_get_child(pill, 0);
    if (label) {
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, fg, 0);
    }
}

// ============================================================
// STEPPER TEKAN-TAHAN (permintaan eksplisit -- tap tetap step kecil
// seperti biasa, tekan-tahan MEMPERCEPAT step secara bertahap
// selama tombol ditahan). Dipakai bersama oleh screen_set_target.cpp
// (target dosis, step dasar 0.1g) dan screen_settings.cpp (Tolerance
// step dasar 0.01g, Max Pulses step dasar 1).
//
// MEKANISME: LVGL bawaan sudah mengirim LV_EVENT_LONG_PRESSED sekali
// (begitu tombol ditahan lewat ambang lama-tekan default), lalu
// LV_EVENT_LONG_PRESSED_REPEAT berulang tiap ambang repeat default
// selama tombol TETAP ditahan (lihat LV_INDEV_DEF_LONG_PRESS_TIME/
// LV_INDEV_DEF_LONG_PRESS_REPEAT_TIME di lv_conf.h, dipakai APA ADANYA
// tanpa override -- tidak ada alasan mengubah nilai default LVGL
// untuk kasus ini). Fungsi ini TIDAK menyalakan repeat sendiri --
// hanya menghitung PENGALI step yang caller kalikan ke step dasarnya,
// berdasarkan seberapa banyak repeat event sudah terjadi utuk
// SATU sesi tekan (counter di-reset ke 0 tiap kali LV_EVENT_RELEASED/
// LV_EVENT_PRESSED terjadi, lihat caller).
//
// KURVA PERCEPATAN: pengali dimulai 1x, naik +1 tiap repeat, dibatasi
// maksimal 10x -- supaya percepatan terasa progresif (bukan lompatan
// tiba-tiba) tapi tetap ada batas atas wajar (step dasar 0.1g x 10 =
// 1.0g per repeat event di titik tercepat, sesuai permintaan "turun
// per 1gr" untuk kasus target dosis).
inline float ui_repeat_step_multiplier(int repeat_count) {
    int multiplier = 1 + repeat_count;
    if (multiplier > 10) multiplier = 10;
    return (float)multiplier;
}
