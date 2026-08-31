#include "ui_common.h"
#include "../../include/config.h"
#include <cstdio>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: snprintf() dipakai di bawah tanpa include ini
#include "../../include/github_ota.h"  // GithubOtaManager -- tombol "Check for Update" menggantikan baris IP Address (lihat catatan create_update_row())
#include "../../include/version.h"  // FIRMWARE_VERSION -- DI-GENERATE OTOMATIS oleh generate_version.py tiap compile, JANGAN edit include/version.h manual

// ============================================================
// SETTINGS SCREEN -- 7 parameter MANUAL: Tolerance, Max Pulses,
// Settle Time, Coast Ratio, Confirmation Window, Post-Purge Enable,
// dan Post-Purge Pulses (SEMUA ditambahkan lewat kesepakatan eksplisit
// di sesi-sesi berbeda -- lihat riwayat diskusi). Post-Purge pulse
// DURATION/GAP (per pulsa) SENGAJA hardcode di config.h, TIDAK
// disetting -- keputusan eksplisit untuk versi pertama fitur ini.
// Flow Threshold TETAP TIDAK ditampilkan (keputusan ini MASIH
// BERLAKU, tidak berubah) -- TIDAK ADA toggle Auto/Manual, TIDAK ADA
// persistence/learning state/EMA/logic "learned" untuknya. Baru
// disentuh UI setelah keputusan adaptive learning dikunci terpisah.
// Jangan tambah field lain ke screen ini tanpa keputusan eksplisit
// yang sama.
// ============================================================

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_tolerance_value = nullptr;
static lv_obj_t* s_max_pulses_value = nullptr;
static lv_obj_t* s_settle_time_value = nullptr;  // BARU
static lv_obj_t* s_coast_ratio_value = nullptr;  // BARU
static lv_obj_t* s_confirmation_window_value = nullptr;  // BARU
static lv_obj_t* s_post_purge_toggle_btn = nullptr;  // BARU
static lv_obj_t* s_post_purge_toggle_label = nullptr;  // BARU
static lv_obj_t* s_post_purge_pulse_count_value = nullptr;  // BARU

// FORWARD DECLARATION -- BUG DITEMUKAN SEBELUM COMPILE (audit urutan
// definisi): post_purge_toggle_cb() (di bawah) memanggil
// ui_update_toggle_visual(), tapi definisi PENUH fungsi itu ada JAUH
// di bawah (dekat create_toggle_row()) -- C++ butuh deklarasi
// terlihat SEBELUM titik pemakaian. Forward declare di sini (dekat
// variabel global lain) supaya urutan compile benar TANPA perlu
// menata ulang banyak kode yang sudah ada.
static void ui_update_toggle_visual(lv_obj_t* btn, lv_obj_t* label, bool state);

extern void ui_close_settings(lv_event_t* e);
extern void ui_enable_swipe_home(lv_obj_t* screen);  // swipe kanan -> Set Target (Home)
extern void saveSettingsToNVS();  // BARU -- main.cpp, persist 4 setting UI ke flash lintas restart

// Counter repeat per tombol (independen satu sama lain) -- reset ke
// 0 tiap kali sesi tekan baru dimulai (LV_EVENT_PRESSED), naik tiap
// LV_EVENT_LONG_PRESSED_REPEAT. Lihat ui_repeat_step_multiplier() di
// ui_common.h untuk kurva percepatannya.
static int s_tolerance_minus_repeat = 0;
static int s_tolerance_plus_repeat = 0;
static int s_max_pulses_minus_repeat = 0;
static int s_max_pulses_plus_repeat = 0;
static int s_settle_time_minus_repeat = 0;  // BARU
static int s_settle_time_plus_repeat = 0;   // BARU
static int s_coast_ratio_minus_repeat = 0;  // BARU
static int s_coast_ratio_plus_repeat = 0;   // BARU
static int s_confirmation_window_minus_repeat = 0;  // BARU
static int s_confirmation_window_plus_repeat = 0;   // BARU
static int s_post_purge_pulse_count_minus_repeat = 0;  // BARU
static int s_post_purge_pulse_count_plus_repeat = 0;   // BARU

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
    if (g_ui_state.accuracy_tolerance_g < 0.1f) g_ui_state.accuracy_tolerance_g = 0.1f;  // batas bawah wajar (dinaikkan dari 0.01f -- YZC-131 akurasi fisik ~0.3-0.5g, toleransi di bawah itu tidak realistis dicapai)
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

// BARU -- Settle Time (GRIND_SCALE_PRECISION_SETTLING_TIME_MS via
// setSettlingTimeMs()). Range 200-2000ms, step 100ms -- disepakati
// eksplisit sebelum coding (lihat riwayat diskusi). Pola SAMA PERSIS
// dengan max_pulses_minus_cb/plus_cb di atas (step tetap 100, TIDAK
// pakai ui_repeat_step_multiplier() untuk kelipatan step seperti
// tolerance/max_pulses -- long-press tetap mempercepat lewat repeat
// count, tapi step dasarnya tetap kelipatan 100 rapi, bukan makin
// besar tidak beraturan, supaya nilai akhir selalu representable
// persis sebagai kelipatan 100ms).
static void settle_time_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_settle_time_minus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_settle_time_minus_repeat++;
    unsigned long step = 100UL * (unsigned long)ui_repeat_step_multiplier(s_settle_time_minus_repeat);
    if (step > g_ui_state.settle_time_ms) {
        g_ui_state.settle_time_ms = 200UL;  // clamp bawah, hindari underflow unsigned
    } else {
        g_ui_state.settle_time_ms -= step;
        if (g_ui_state.settle_time_ms < 200UL) g_ui_state.settle_time_ms = 200UL;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%lu", g_ui_state.settle_time_ms);
    lv_label_set_text(s_settle_time_value, buf);
}

static void settle_time_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_settle_time_plus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_settle_time_plus_repeat++;
    unsigned long step = 100UL * (unsigned long)ui_repeat_step_multiplier(s_settle_time_plus_repeat);
    g_ui_state.settle_time_ms += step;
    if (g_ui_state.settle_time_ms > 2000UL) g_ui_state.settle_time_ms = 2000UL;  // batas atas disepakati
    char buf[8];
    snprintf(buf, sizeof(buf), "%lu", g_ui_state.settle_time_ms);
    lv_label_set_text(s_settle_time_value, buf);
}

// BARU -- Coast Ratio (GRIND_LATENCY_TO_COAST_RATIO via
// setCoastRatio()). Range 0.5-3.0, step 0.1 -- disepakati eksplisit
// setelah investigasi overshoot 18g (lihat riwayat diskusi). Pola
// SAMA PERSIS dengan tolerance_minus_cb/plus_cb (float dengan step
// tetap, TIDAK pakai ui_repeat_step_multiplier() untuk kelipatan step
// yang membesar, supaya nilai akhir selalu representable rapi sebagai
// kelipatan 0.1 -- sama alasan dengan settle_time di atas).
static void coast_ratio_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_coast_ratio_minus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_coast_ratio_minus_repeat++;
    float ratioStep = 0.1f * ui_repeat_step_multiplier(s_coast_ratio_minus_repeat);
    g_ui_state.coast_ratio -= ratioStep;
    if (g_ui_state.coast_ratio < 0.5f) g_ui_state.coast_ratio = 0.5f;  // batas bawah disepakati
    char coastBuf[8];
    snprintf(coastBuf, sizeof(coastBuf), "%.1f", g_ui_state.coast_ratio);
    lv_label_set_text(s_coast_ratio_value, coastBuf);
}

static void coast_ratio_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_coast_ratio_plus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_coast_ratio_plus_repeat++;
    float ratioStep = 0.1f * ui_repeat_step_multiplier(s_coast_ratio_plus_repeat);
    g_ui_state.coast_ratio += ratioStep;
    if (g_ui_state.coast_ratio > 3.0f) g_ui_state.coast_ratio = 3.0f;  // batas atas disepakati
    char coastBuf[8];
    snprintf(coastBuf, sizeof(coastBuf), "%.1f", g_ui_state.coast_ratio);
    lv_label_set_text(s_coast_ratio_value, coastBuf);
}

// BARU -- Confirmation Window (GRIND_LATENCY_CONFIRMATION_MS via
// setConfirmationWindowMs()). Range 300-2000ms, step 100ms --
// disepakati eksplisit setelah observasi gumpalan sisa chute bisa
// lolos window konfirmasi lama (500ms) seolah flow kopi sungguhan
// yang sudah stabil, mencemari grind_latency_ms basis Coast Ratio.
// Pola SAMA PERSIS dengan settle_time_minus_cb/plus_cb (unsigned
// long, step tetap kelipatan 100).
static void confirmation_window_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_confirmation_window_minus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_confirmation_window_minus_repeat++;
    unsigned long step = 100UL * (unsigned long)ui_repeat_step_multiplier(s_confirmation_window_minus_repeat);
    if (step > g_ui_state.confirmation_window_ms) {
        g_ui_state.confirmation_window_ms = 300UL;  // clamp bawah, hindari underflow unsigned
    } else {
        g_ui_state.confirmation_window_ms -= step;
        if (g_ui_state.confirmation_window_ms < 300UL) g_ui_state.confirmation_window_ms = 300UL;
    }
    char confirmBuf[8];
    snprintf(confirmBuf, sizeof(confirmBuf), "%lu", g_ui_state.confirmation_window_ms);
    lv_label_set_text(s_confirmation_window_value, confirmBuf);
}

static void confirmation_window_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_confirmation_window_plus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_confirmation_window_plus_repeat++;
    unsigned long step = 100UL * (unsigned long)ui_repeat_step_multiplier(s_confirmation_window_plus_repeat);
    g_ui_state.confirmation_window_ms += step;
    if (g_ui_state.confirmation_window_ms > 2000UL) g_ui_state.confirmation_window_ms = 2000UL;  // batas atas disepakati
    char confirmBuf[8];
    snprintf(confirmBuf, sizeof(confirmBuf), "%lu", g_ui_state.confirmation_window_ms);
    lv_label_set_text(s_confirmation_window_value, confirmBuf);
}

// BARU -- POST_PURGE enable toggle. LV_EVENT_CLICKED saja (bukan
// LV_EVENT_ALL seperti stepper -/+) -- tombol toggle simpel, tidak
// butuh percepatan long-press seperti stepper angka.
static void post_purge_toggle_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    g_ui_state.post_purge_enabled = !g_ui_state.post_purge_enabled;
    ui_update_toggle_visual(s_post_purge_toggle_btn, s_post_purge_toggle_label, g_ui_state.post_purge_enabled);
}

// BARU -- Post-Purge Pulses (jumlah pulsa purge per sesi). Range
// 1-5, step 1 -- disepakati implisit dari GRIND_POST_PURGE_PULSE_COUNT_DEFAULT=2
// sebagai titik tengah wajar. Durasi/jeda TIAP pulsa TIDAK disetting
// (hardcode di config.h) -- lihat catatan lengkap di config.h.
static void post_purge_pulse_count_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_post_purge_pulse_count_minus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_post_purge_pulse_count_minus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_post_purge_pulse_count_minus_repeat);
    g_ui_state.post_purge_pulse_count -= step;
    if (g_ui_state.post_purge_pulse_count < 1) g_ui_state.post_purge_pulse_count = 1;  // minimal 1 pulsa kalau enabled
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_ui_state.post_purge_pulse_count);
    lv_label_set_text(s_post_purge_pulse_count_value, buf);
}

static void post_purge_pulse_count_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_post_purge_pulse_count_plus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_post_purge_pulse_count_plus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_post_purge_pulse_count_plus_repeat);
    g_ui_state.post_purge_pulse_count += step;
    if (g_ui_state.post_purge_pulse_count > 5) g_ui_state.post_purge_pulse_count = 5;  // batas atas wajar
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_ui_state.post_purge_pulse_count);
    lv_label_set_text(s_post_purge_pulse_count_value, buf);
}

static void save_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // BARU -- persist ke NVS/flash SEKARANG SUDAH diimplementasikan
    // (sebelumnya TODO/cuma runtime in-memory lewat g_ui_state) --
    // disepakati eksplisit setelah kejadian nyata: Settle Time 700ms
    // hilang begitu board restart. Lihat catatan lengkap di
    // saveSettingsToNVS() (main.cpp).
    saveSettingsToNVS();
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

// BARU -- row dengan TOMBOL TOGGLE (BUKAN lv_switch -- DICEK lv_conf.h,
// LV_USE_SWITCH = 0/DISABLED di build LVGL ini, lv_switch_create()
// akan GAGAL COMPILE kalau dipakai. lv_btn SUDAH PASTI tersedia
// -- LV_USE_BTN = 1, dipakai di SELURUH UI firmware ini -- jadi
// tombol yang berubah teks/warna sesuai state meniru switch secara
// visual, TANPA risiko compile error atau nambah flash usage untuk
// enable widget baru). Dipakai untuk POST_PURGE enable. Layout DITIRU
// dari create_manual_grind_row()/create_debug_row() (name+desc di
// kiri dengan set_width+wrap eksplisit, elemen interaktif sejajar
// kanan) -- pola itu SUDAH TERBUKTI aman dari overlap.
static void create_toggle_row(lv_obj_t* parent, int y_offset, const char* name, const char* desc,
                                lv_obj_t** out_toggle_btn, lv_obj_t** out_toggle_label,
                                lv_event_cb_t toggle_cb, bool initial_state) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 80);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_set_style_bg_color(row, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // set_width+wrap eksplisit -- SAMA seperti fix overlap di
    // create_manual_grind_row()/create_debug_row().
    lv_obj_t* desc_label = lv_label_create(row);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(desc_label, 110);  // sedikit lebih sempit dari 132 (Manual Grind/Debug) -- tombol toggle lebih lebar dari tombol OPEN
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(desc_label, name_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

    lv_obj_t* toggle_btn = lv_btn_create(row);
    lv_obj_set_size(toggle_btn, 80, 36);
    lv_obj_set_style_radius(toggle_btn, 12, 0);
    lv_obj_align(toggle_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(toggle_btn, toggle_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* toggle_label = lv_label_create(toggle_btn);
    lv_obj_set_style_text_font(toggle_label, &lv_font_montserrat_12, 0);
    lv_obj_center(toggle_label);

    // Warna & teks awal sesuai initial_state -- factored ke fungsi
    // shared ui_update_toggle_visual() (dipanggil di sini DAN di
    // dalam toggle_cb setelah state berubah) supaya tidak duplikasi
    // logic warna/teks di 2 tempat berbeda.
    *out_toggle_btn = toggle_btn;
    *out_toggle_label = toggle_label;
}

// Update visual tombol toggle (warna bg + teks ON/OFF) sesuai state
// bool yang diberikan -- dipanggil dari create_toggle_row() (state
// awal) DAN dari toggle_cb (setelah state berubah), supaya logic
// warna/teks CUMA ada di SATU tempat (hindari 2 tempat yang bisa
// diam-diam jadi tidak konsisten kalau salah satu diubah tapi yang
// lain lupa).
static void ui_update_toggle_visual(lv_obj_t* btn, lv_obj_t* label, bool state) {
    if (state) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a4a2a), 0);  // hijau gelap -- ON
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, COLOR_SUCCESS, 0);
        lv_label_set_text(label, "ON");
        lv_obj_set_style_text_color(label, COLOR_SUCCESS, 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a2a), 0);  // abu netral -- OFF
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x4a4a4a), 0);
        lv_label_set_text(label, "OFF");
        lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    }
}

// ------------------------------------------------------------
// Baris "Firmware Update" (GANTI dari baris IP Address -- keputusan
// eksplisit setelah pindah ke GitHub OTA). Tombol CHECK memanggil
// githubOta.checkAndUpdate() -- BLOCKING (LVGL sengaja tidak
// responsif selama proses ini), lalu menampilkan status/progress
// lewat label di baris ini.
//
// LAYOUT DIPERBESAR (dilaporkan tumpang tindih -- row sebelumnya
// 58px/pad_all=10 terlalu pendek untuk name_label+status_label+
// check_btn yang sama-sama butuh ruang vertikal, mirip pola bug
// row Tolerance/Max Pulses sebelumnya): row_h dinaikkan ke 80px
// (memanfaatkan ruang kosong yang tadinya tersisa sebelum tombol
// Save, sesuai permintaan eksplisit), pad_all diturunkan ke 8.
// Ditambah baris versi sekarang & terbaru (permintaan eksplisit),
// dibatasi 3 baris teks total (bukan 4) supaya tetap muat dengan
// margin aman -- lihat catatan lengkap di create_update_row().
// ------------------------------------------------------------
static lv_obj_t* s_current_version_label = nullptr;
static lv_obj_t* s_latest_version_label = nullptr;  // dipakai ganda: versi terbaru DAN status hasil check, lihat catatan di create_update_row()

static void check_update_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_label_set_text(s_latest_version_label, "Checking...");
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
    // Prioritaskan tampilkan versi terbaru kalau berhasil didapat
    // (lebih berguna buat operator daripada status generik error),
    // baru fallback ke statusText() kalau versi belum sempat didapat
    // sama sekali (mis. WiFi gagal connect dari awal).
    if (githubOta.latestVersion().length() > 0) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Latest: %s (%s)", githubOta.latestVersion().c_str(), githubOta.statusText());
        lv_label_set_text(s_latest_version_label, buf);
    } else {
        lv_label_set_text(s_latest_version_label, githubOta.statusText());
    }
}

static void create_update_row(lv_obj_t* parent, int y_offset) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 80);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_set_style_bg_color(row, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, "Firmware Update");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // Versi SEKARANG (baris ke-2) dan gabungan versi-terbaru+status
    // (baris ke-3) -- KOREKSI setelah dihitung ulang: 4 baris terpisah
    // (nama/versi-sekarang/versi-terbaru/status) tidak muat di ruang
    // tersisa (80px tinggi row) tanpa membuat margin ke tombol Save
    // terlalu ketat (2px, riskan overlap kalau rendering meleset
    // sedikit dari estimasi). Digabung jadi 3 baris total: versi
    // terbaru dan status hasil check DIGABUNG SATU baris (keduanya
    // memang informasi terkait -- "Latest: v1.0.2" lalu berubah jadi
    // "Success, rebooting..." saat proses berjalan, tidak perlu baris
    // terpisah).
    s_current_version_label = lv_label_create(row);
    char cur_buf[24];
    snprintf(cur_buf, sizeof(cur_buf), "Running: %s", FIRMWARE_VERSION);
    lv_label_set_text(s_current_version_label, cur_buf);
    lv_obj_set_style_text_font(s_current_version_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_current_version_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align_to(s_current_version_label, name_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

    // s_latest_version_label DIPAKAI GANDA: awalnya tampilkan "Latest:
    // (press CHECK)", lalu check_update_btn_cb() menimpanya dengan
    // versi terbaru hasil query, dan KEMUDIAN (kalau checkAndUpdate()
    // gagal, bukan sukses+reboot) ditimpa LAGI dengan status error dari
    // githubOta.statusText() -- satu label yang sama dipakai bergantian
    // untuk 2 keperluan supaya tidak perlu baris ke-4 terpisah.
    s_latest_version_label = lv_label_create(row);
    lv_label_set_text(s_latest_version_label, "Latest: (press CHECK)");
    lv_obj_set_style_text_font(s_latest_version_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_latest_version_label, COLOR_ACCENT, 0);
    lv_obj_align_to(s_latest_version_label, s_current_version_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

    // (s_update_status_label DIHAPUS -- statusnya sekarang digabung ke
    // s_latest_version_label, lihat catatan lengkap di
    // check_update_btn_cb() dan komentar di atas s_latest_version_label)

    lv_obj_t* check_btn = lv_btn_create(row);
    lv_obj_set_size(check_btn, 90, 36);
    lv_obj_set_style_radius(check_btn, 12, 0);
    lv_obj_set_style_bg_color(check_btn, lv_color_hex(0x2a2412), 0);
    lv_obj_set_style_border_width(check_btn, 1, 0);
    lv_obj_set_style_border_color(check_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_align(check_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(check_btn, check_update_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* check_label = lv_label_create(check_btn);
    lv_label_set_text(check_label, "CHECK");
    lv_obj_set_style_text_font(check_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(check_label, COLOR_ACCENT, 0);
    lv_obj_center(check_label);
}

// ------------------------------------------------------------
// Baris "Manual Grind" (BARU) -- pintu masuk ke layar test motor/
// kalibrasi grind size (lihat screen_manual_grind.cpp). Row ini
// SEDERHANA (cuma nama + tombol "OPEN"), TIDAK ada peringatan safety
// di sini -- peringatan lengkap ada di layar tujuannya sendiri,
// row ini cuma pintu masuk.
// ------------------------------------------------------------
extern void ui_open_manual_grind(lv_event_t* e);

static void create_manual_grind_row(lv_obj_t* parent, int y_offset) {
    lv_obj_t* row = lv_obj_create(parent);
    // TINGGI DINAIKKAN 66 -> 80 (LIHAT CATATAN DI BAWAH soal desc_label
    // wrap 2 baris -- kalau row ini ditinggikan lagi di masa depan,
    // pad_all tetap 10, jadi content_h = row_h - 20).
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 80);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_set_style_bg_color(row, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, "Manual Grind");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // BUG DITEMUKAN LEWAT FOTO HARDWARE FISIK: desc_label sebelumnya
    // TIDAK diberi lebar eksplisit (auto-size LVGL default) sehingga
    // teks "Test motor / calibrate grind size" melebar penuh dan
    // TERTINDIH tombol OPEN (open_btn digambar setelah desc_label,
    // jadi menutupinya). Estimasi lebar-karakter yang dipakai untuk
    // audit numerik SEBELUM ada foto ternyata meleset cukup jauh dari
    // kondisi nyata font Montserrat di layar ini -- pelajaran yang
    // SAMA seperti kasus offset X manual stepper param_row di atas:
    // JANGAN andalkan estimasi piksel manual, pakai constraint LVGL
    // eksplisit (set_width + long_mode wrap) yang otomatis
    // menyesuaikan diri berapa pun panjang teksnya.
    // Lebar: content_w row (SCREEN_WIDTH-40-2*10=220) dikurangi lebar
    // tombol OPEN (70) dan margin aman (10) = 140px, dibulatkan ke
    // bawah sedikit supaya ada jarak visual dari tombol.
    lv_obj_t* desc_label = lv_label_create(row);
    lv_label_set_text(desc_label, "Test motor / calibrate grind size");
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(desc_label, 132);
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(desc_label, name_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

    lv_obj_t* open_btn = lv_btn_create(row);
    lv_obj_set_size(open_btn, 70, 36);
    lv_obj_set_style_radius(open_btn, 12, 0);
    lv_obj_set_style_bg_color(open_btn, lv_color_hex(0x2a2412), 0);
    lv_obj_set_style_border_width(open_btn, 1, 0);
    lv_obj_set_style_border_color(open_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_align(open_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(open_btn, ui_open_manual_grind, LV_EVENT_CLICKED, NULL);
    lv_obj_t* open_label = lv_label_create(open_btn);
    lv_label_set_text(open_label, "OPEN");
    lv_obj_set_style_text_font(open_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(open_label, COLOR_ACCENT, 0);
    lv_obj_center(open_label);
}

// ------------------------------------------------------------
// Baris "Debug" (BARU) -- pintu masuk ke layar diagnostik HX711/
// validasi grind tanpa Serial (lihat screen_debug.cpp). Ditambahkan
// karena permintaan eksplisit Wahyu: setelah case dipasang & power
// pindah ke buck 5V (USB tidak lagi praktis diakses), grind ditolak
// tanpa cara melihat kenapa. Row SEDERHANA sama seperti Manual Grind
// -- cuma nama + tombol OPEN, detail diagnostik ada di layar tujuan.
// SENGAJA ditaruh SETELAH create_manual_grind_row() di scroll_area
// (lihat pemanggilan di ui_screen_settings_create()), gap 8px
// konsisten dengan row lain (dihitung eksplisit -- lihat riwayat
// kalkulasi layout Settings di README/brief, pola yang sama dipakai
// di sini).
// ------------------------------------------------------------
extern void ui_open_debug(lv_event_t* e);

static void create_debug_row(lv_obj_t* parent, int y_offset) {
    lv_obj_t* row = lv_obj_create(parent);
    // TINGGI DINAIKKAN 66 -> 80, SAMA ALASAN dengan create_manual_grind_row() di atas.
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 80);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_set_style_bg_color(row, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, "Debug");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // BUG SAMA seperti create_manual_grind_row() di atas -- lihat
    // catatan lengkap di sana. Teks di sini SEDIKIT DIPERPENDEK
    // ("HX711 raw / validasi grind tanpa Serial" -> "HX711 raw /
    // validasi grind") dibanding versi lama, sebagai margin aman
    // TAMBAHAN di atas wrap+set_width -- karena estimasi lebar
    // karakter font Montserrat 12 SUDAH 2x terbukti meleset dari
    // kondisi nyata di layar ini (bukti: bug overlap ini sendiri),
    // lebih aman perpendek teks + pasang wrap, daripada cuma
    // mengandalkan wrap sendirian untuk teks yang lebih panjang.
    lv_obj_t* desc_label = lv_label_create(row);
    lv_label_set_text(desc_label, "HX711 raw / validasi grind");
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(desc_label, 132);
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(desc_label, name_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

    lv_obj_t* open_btn = lv_btn_create(row);
    lv_obj_set_size(open_btn, 70, 36);
    lv_obj_set_style_radius(open_btn, 12, 0);
    lv_obj_set_style_bg_color(open_btn, lv_color_hex(0x2a2412), 0);
    lv_obj_set_style_border_width(open_btn, 1, 0);
    lv_obj_set_style_border_color(open_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_align(open_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(open_btn, ui_open_debug, LV_EVENT_CLICKED, NULL);
    lv_obj_t* open_label = lv_label_create(open_btn);
    lv_label_set_text(open_label, "OPEN");
    lv_obj_set_style_text_font(open_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(open_label, COLOR_ACCENT, 0);
    lv_obj_center(open_label);
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

    // ------------------------------------------------------------
    // CONTAINER SCROLL (BARU) -- permintaan eksplisit untuk muat baris
    // "Manual Grind" tambahan tanpa row lain jadi mepet lagi (Settings
    // sudah penuh: Tolerance+Max Pulses+Firmware Update+Save, sisa
    // ruang cuma 14px sebelum perubahan ini -- dihitung eksplisit
    // sebelum diputuskan perlu scroll, bukan tebakan).
    //
    // KENAPA CONTAINER TERPISAH (bukan langsung
    // lv_obj_add_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE)): Save button
    // di-align LV_ALIGN_BOTTOM_MID terhadap s_screen dengan asumsi
    // posisinya TETAP/fixed (selalu terlihat, tidak ikut scroll status
    // bar/title ikut ke atas kalau di-scroll naik). Kalau scroll
    // dipasang langsung ke s_screen, Save/status bar/title akan IKUT
    // ter-scroll bersama konten -- TIDAK diinginkan. Container ini
    // membungkus HANYA baris-baris parameter (Tolerance/Max Pulses/
    // Firmware Update/Manual Grind), sementara title/status bar/Save
    // tetap child LANGSUNG s_screen (fixed, selalu terlihat).
    lv_obj_t* scroll_area = lv_obj_create(s_screen);
    lv_obj_set_size(scroll_area, SCREEN_WIDTH, 456 - (STATUS_BAR_HEIGHT + 36) - 96);  // 96 = ruang disisakan utk Save button + margin visual (dinaikkan dari 88 supaya scroll_area tidak berhimpitan pas dengan Save)
    lv_obj_align(scroll_area, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 36);
    lv_obj_set_style_bg_opa(scroll_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_area, 0, 0);
    lv_obj_set_style_pad_all(scroll_area, 0, 0);
    lv_obj_set_scroll_dir(scroll_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_area, LV_SCROLLBAR_MODE_AUTO);

    char tol_buf[8];
    snprintf(tol_buf, sizeof(tol_buf), "%.2f", g_ui_state.accuracy_tolerance_g);
    // SEMUA row sekarang jadi child scroll_area (BUKAN s_screen lagi)
    // -- offset Y di bawah ini SEKARANG RELATIF terhadap scroll_area
    // (yang sudah dimulai di STATUS_BAR_HEIGHT+36), BUKAN lagi relatif
    // terhadap seluruh screen -- makanya offset row1 sekarang 0 (dulu
    // STATUS_BAR_HEIGHT+36).
    create_param_row(scroll_area, 0, "Tolerance", "Accuracy window (g)",
                      &s_tolerance_value, tolerance_minus_cb, tolerance_plus_cb, tol_buf);

    char pulse_buf[8];
    snprintf(pulse_buf, sizeof(pulse_buf), "%d", g_ui_state.max_pulse_attempts);
    create_param_row(scroll_area, 108, "Max Pulses", "Pulse attempt limit",
                      &s_max_pulses_value, max_pulses_minus_cb, max_pulses_plus_cb, pulse_buf);

    // BARU -- Settle Time (GRIND_SCALE_PRECISION_SETTLING_TIME_MS),
    // disepakati eksplisit sebelum coding (lihat riwayat diskusi).
    // Row height 100, SAMA seperti Tolerance/Max Pulses (create_param_row
    // selalu 100px) -- offset 216 = 108 (Max Pulses offset) + 100
    // (tingginya) + 8 (gap konsisten semua row layar ini).
    char settle_buf[8];
    snprintf(settle_buf, sizeof(settle_buf), "%lu", g_ui_state.settle_time_ms);
    create_param_row(scroll_area, 216, "Settle Time", "Scale settle (ms)",
                      &s_settle_time_value, settle_time_minus_cb, settle_time_plus_cb, settle_buf);

    // BARU -- Coast Ratio (GRIND_LATENCY_TO_COAST_RATIO), disepakati
    // eksplisit setelah investigasi overshoot 18g (lihat riwayat
    // diskusi) -- KOMENTAR LAMA di sini sebelumnya bilang "Coast Ratio
    // SENGAJA TIDAK ADA", itu keputusan LAMA yang SEKARANG SUDAH
    // BERUBAH lewat kesepakatan eksplisit baru. Flow Threshold TETAP
    // tidak ditampilkan (keputusan itu masih berlaku, TIDAK berubah).
    // Offset 324 = 216 (Settle Time offset) + 100 (tingginya) + 8 (gap).
    char coast_buf[8];
    snprintf(coast_buf, sizeof(coast_buf), "%.1f", g_ui_state.coast_ratio);
    create_param_row(scroll_area, 324, "Coast Ratio", "Latency-to-coast multiplier",
                      &s_coast_ratio_value, coast_ratio_minus_cb, coast_ratio_plus_cb, coast_buf);

    // BARU -- Confirmation Window (GRIND_LATENCY_CONFIRMATION_MS),
    // disepakati eksplisit setelah observasi gumpalan sisa chute bisa
    // lolos window konfirmasi lama seolah flow sungguhan (lihat
    // riwayat diskusi). Offset 432 = 324 (Coast Ratio offset) + 100
    // (tingginya) + 8 (gap).
    char confirm_buf[8];
    snprintf(confirm_buf, sizeof(confirm_buf), "%lu", g_ui_state.confirmation_window_ms);
    create_param_row(scroll_area, 432, "Confirm Window", "Flow confirmation time (ms)",
                      &s_confirmation_window_value, confirmation_window_minus_cb, confirmation_window_plus_cb, confirm_buf);

    // BARU -- Post-Purge Enable (toggle) & Post-Purge Pulses (jumlah
    // pulsa), disepakati eksplisit setelah observasi sisa chute
    // terdorong jatuh selama grinding (lihat riwayat diskusi). Offset
    // 540 = 432 (Confirm Window offset) + 100 (tingginya) + 8 (gap).
    // Post Purge Enable row_h=80 (toggle row, SAMA seperti Manual
    // Grind/Debug), BUKAN 100 (param row) -- lihat create_toggle_row().
    create_toggle_row(scroll_area, 540, "Post-Purge", "Getar buang sisa chute",
                       &s_post_purge_toggle_btn, &s_post_purge_toggle_label,
                       post_purge_toggle_cb, g_ui_state.post_purge_enabled);
    ui_update_toggle_visual(s_post_purge_toggle_btn, s_post_purge_toggle_label, g_ui_state.post_purge_enabled);

    // Offset 628 = 540 (Post-Purge Enable offset) + 80 (tingginya,
    // BUKAN 100 -- row ini toggle_row) + 8 (gap).
    char purge_pulse_buf[8];
    snprintf(purge_pulse_buf, sizeof(purge_pulse_buf), "%d", g_ui_state.post_purge_pulse_count);
    create_param_row(scroll_area, 628, "Purge Pulses", "Jumlah pulsa getar",
                      &s_post_purge_pulse_count_value, post_purge_pulse_count_minus_cb, post_purge_pulse_count_plus_cb, purge_pulse_buf);

    // OFFSET DIGESER LAGI: 540 -> 736 (2 row Post-Purge baru di atas
    // menambah 88px + 108px = 196px total ke semua row di bawahnya).
    // Dihitung eksplisit, BUKAN ditebak.
    create_update_row(scroll_area, 736);

    // OFFSET DIGESER LAGI: 628 -> 824, alasan SAMA seperti
    // create_update_row() di atas.
    create_manual_grind_row(scroll_area, 824);

    // Y OFFSET SUDAH DIUBAH 5X sekarang: 378 -> 392 (fix overlap
    // Manual Grind) -> 500 (Settle Time) -> 608 (Coast Ratio) -> 716
    // (Confirmation Window) -> SEKARANG 716 -> 912 (2 row Post-Purge
    // disisipkan, +196px lagi). Dihitung eksplisit tiap kali, BUKAN
    // ditebak -- gap 8px konsisten di SEMUA row.
    create_debug_row(scroll_area, 912);

    lv_obj_t* save_btn = lv_btn_create(s_screen);
    // KOREKSI (dilaporkan -- ukuran beda dari tombol lain): SEBELUMNYA
    // 200x48, sisa dari sebelum standarisasi v18 (lihat spec tunggal
    // 220x60 / offset -28 di komentar screen_idle.cpp start_btn) --
    // Settings terlewat saat standarisasi awal. Disamakan di sini.
    // TETAP child s_screen (bukan scroll_area) -- lihat catatan
    // container scroll di atas.
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
