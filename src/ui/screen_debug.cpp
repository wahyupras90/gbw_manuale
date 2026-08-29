#include "ui_common.h"
#include "../../include/debug_snapshot.h"
#include <Arduino.h>   // millis() -- BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: ui_common.h cuma include lvgl.h, tidak transitif ke Arduino.h
#include <cstdio>
#include <cstring>  // strcmp() -- BARU, dipakai bandingkan resetReasonStr di ui_screen_debug_update()
#include <math.h>   // isnan()

// ============================================================
// DEBUG SCREEN -- diagnostik HX711/validasi grind TANPA Serial.
// Ditambahkan (permintaan eksplisit Wahyu) setelah case dipasang
// permanen dan power dipindah ke buck 5V dari grinder (USB/Serial
// tidak lagi praktis diakses untuk debug harian). Read-only murni --
// TIDAK ada aksi yang mengubah state HX711/weightFilter/grindController
// dari layar ini, cuma menampilkan snapshot data (lihat
// grind_get_debug_snapshot() di main.cpp).
//
// THROTTLING (disepakati eksplisit sebelum coding): refresh data
// TIDAK dilakukan tiap frame ui_tick() (dipanggil sangat sering dari
// loop(), lihat main.cpp) -- grind_get_debug_snapshot() memanggil
// HX711Reader::readRawAverage(1) yang BLOCKING lewat wait_ready()
// (lihat komentar lengkap di main.cpp), kalau dipanggil tiap frame
// bisa mengganggu responsivitas UI/motor safety checks lain di
// loop(). Refresh di-throttle ke ~300ms via millis(), pola sama
// seperti manual_grind_check_timeout() (main.cpp).
// ============================================================

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_raw_value = nullptr;
static lv_obj_t* s_offset_value = nullptr;
static lv_obj_t* s_scale_value = nullptr;
static lv_obj_t* s_weight_value = nullptr;
static lv_obj_t* s_has_sample_value = nullptr;
static lv_obj_t* s_flow_valid_value = nullptr;
static lv_obj_t* s_flow_rate_value = nullptr;

// BARU -- section "DIAGNOSTIK SISTEM", lihat catatan lengkap alasan
// penambahan (investigasi "layar tiba-tiba lompat ke Set Target saat
// GRINDING") di debug_snapshot.h.
static lv_obj_t* s_reset_reason_value = nullptr;
static lv_obj_t* s_home_gesture_value = nullptr;
static lv_obj_t* s_touch_recovery_value = nullptr;

static unsigned long s_lastRefreshMs = 0;
#define DEBUG_REFRESH_INTERVAL_MS 300  // disepakati eksplisit -- lihat catatan header file ini

extern DebugSnapshot grind_get_debug_snapshot();
extern void ui_close_debug(lv_event_t* e);

// Helper bikin satu baris "label kiri -- value kanan" di dalam card,
// pola sama seperti create_stat_item() di screen_idle.cpp tapi
// horizontal (bukan kolom) -- lebih sesuai untuk daftar panjang di
// layar ini. Return value label-nya supaya bisa di-update caller.
static lv_obj_t* create_debug_row(lv_obj_t* parent, const char* label_text) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 32, 34);
    lv_obj_set_style_bg_color(row, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);

    return value;
}

static lv_obj_t* create_section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, COLOR_ACCENT_DIM, 0);
    return label;
}

// Dipanggil ui_tick() (ui_screen_manager.cpp) TIAP frame selama layar
// ini aktif -- TAPI isi datanya SENDIRI di-throttle internal (lihat
// catatan header file ini), bukan tiap panggilan benar-benar refresh.
void ui_screen_debug_update(void) {
    if (s_screen == nullptr) return;

    unsigned long now = millis();
    if (now - s_lastRefreshMs < DEBUG_REFRESH_INTERVAL_MS) return;
    s_lastRefreshMs = now;

    DebugSnapshot snap = grind_get_debug_snapshot();
    char buf[32];

    if (snap.rawAdc == -2) {
        // Grind sedang aktif -- lihat catatan lengkap di
        // grind_get_debug_snapshot() (main.cpp) kenapa raw dilewati.
        lv_label_set_text(s_raw_value, "-- (grind aktif)");
        lv_obj_set_style_text_color(s_raw_value, COLOR_ACCENT_DIM, 0);
    } else if (snap.rawAdc < 0) {
        lv_label_set_text(s_raw_value, "-- (belum ready)");
        lv_obj_set_style_text_color(s_raw_value, COLOR_TEXT_SECONDARY, 0);
    } else {
        snprintf(buf, sizeof(buf), "%ld", snap.rawAdc);
        lv_label_set_text(s_raw_value, buf);
        lv_obj_set_style_text_color(s_raw_value, COLOR_TEXT_PRIMARY, 0);
    }

    snprintf(buf, sizeof(buf), "%ld", snap.offsetActive);
    lv_label_set_text(s_offset_value, buf);

    snprintf(buf, sizeof(buf), "%.2f", snap.scaleActive);
    lv_label_set_text(s_scale_value, buf);

    if (snap.rawAdc == -2) {
        // Konsisten dengan raw ADC di atas -- weightGrams SENGAJA
        // NAN selama grind aktif (lihat grind_get_debug_snapshot()),
        // tapi ini BUKAN masalah/warning, murni supaya Debug screen
        // tidak mengganggu timing predictive-stop lewat blocking call.
        lv_label_set_text(s_weight_value, "-- (grind aktif)");
        lv_obj_set_style_text_color(s_weight_value, COLOR_ACCENT_DIM, 0);
    } else if (isnan(snap.weightGrams)) {
        lv_label_set_text(s_weight_value, "-- g (NAN)");
        lv_obj_set_style_text_color(s_weight_value, COLOR_WARN, 0);
    } else {
        snprintf(buf, sizeof(buf), "%.2f g", snap.weightGrams);
        lv_label_set_text(s_weight_value, buf);
        lv_obj_set_style_text_color(s_weight_value, COLOR_TEXT_PRIMARY, 0);
    }

    lv_label_set_text(s_has_sample_value, snap.hasSample ? "YA" : "TIDAK");
    lv_obj_set_style_text_color(s_has_sample_value, snap.hasSample ? COLOR_SUCCESS : COLOR_WARN, 0);

    lv_label_set_text(s_flow_valid_value, snap.flowValid ? "YA" : "TIDAK");
    lv_obj_set_style_text_color(s_flow_valid_value, snap.flowValid ? COLOR_SUCCESS : COLOR_WARN, 0);

    if (isnan(snap.flowRateGps)) {
        lv_label_set_text(s_flow_rate_value, "--");
        lv_obj_set_style_text_color(s_flow_rate_value, COLOR_TEXT_SECONDARY, 0);
    } else {
        snprintf(buf, sizeof(buf), "%.3f g/s", snap.flowRateGps);
        lv_label_set_text(s_flow_rate_value, buf);
        lv_obj_set_style_text_color(s_flow_rate_value, COLOR_TEXT_PRIMARY, 0);
    }

    // BARU -- 3 field diagnostik, lihat catatan lengkap di
    // debug_snapshot.h. Semua murni angka/string yang sudah dihitung
    // di grind_get_debug_snapshot() (tidak ada blocking call), jadi
    // aman ikut throttle 300ms yang sama seperti field lain di atas.
    lv_label_set_text(s_reset_reason_value, snap.resetReasonStr);
    // Warna WARN kalau reason BUKAN POWERON -- reboot tak terduga
    // (brownout/watchdog/panic) yang perlu perhatian, dibedakan dari
    // POWERON (boot normal/pertama kali colok listrik) yang wajar.
    bool unexpectedReboot = (strcmp(snap.resetReasonStr, "POWERON") != 0);
    lv_obj_set_style_text_color(s_reset_reason_value, unexpectedReboot ? COLOR_WARN : COLOR_TEXT_PRIMARY, 0);

    snprintf(buf, sizeof(buf), "%lu", snap.homeGestureCount);
    lv_label_set_text(s_home_gesture_value, buf);
    lv_obj_set_style_text_color(s_home_gesture_value, snap.homeGestureCount > 0 ? COLOR_WARN : COLOR_TEXT_PRIMARY, 0);

    snprintf(buf, sizeof(buf), "%lu", snap.touchRecoveryCount);
    lv_label_set_text(s_touch_recovery_value, buf);
    lv_obj_set_style_text_color(s_touch_recovery_value, snap.touchRecoveryCount > 0 ? COLOR_WARN : COLOR_TEXT_PRIMARY, 0);
}

lv_obj_t* ui_screen_debug_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    // TIDAK ada ui_enable_swipe_home() -- konsisten dengan layar
    // sekunder lain yang diakses dari Settings (Manual Grind), operator
    // keluar lewat tombol Back eksplisit supaya tidak ambigu.

    ui_create_status_bar(s_screen, nullptr);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "DEBUG");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 14);

    // Container rows -- dihitung eksplisit: 2 section label (14px
    // tinggi masing2 + margin) + 7 row (34px + gap 6px masing2), mulai
    // Y=STATUS_BAR_HEIGHT+44, muat sebelum tombol Back (align
    // BOTTOM_MID -28-60=-88 dari bawah, SCREEN_HEIGHT=456).
    lv_obj_t* container = lv_obj_create(s_screen);
    lv_obj_set_size(container, SCREEN_WIDTH, 456 - (STATUS_BAR_HEIGHT + 44) - 96);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 16, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 6, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);

    create_section_label(container, "HX711 RAW");
    s_raw_value = create_debug_row(container, "Raw ADC");
    s_offset_value = create_debug_row(container, "Offset aktif");
    s_scale_value = create_debug_row(container, "Scale aktif");
    s_weight_value = create_debug_row(container, "Berat (gram)");

    create_section_label(container, "VALIDASI GRIND");
    s_has_sample_value = create_debug_row(container, "hasSample()");
    s_flow_valid_value = create_debug_row(container, "Flow valid");
    s_flow_rate_value = create_debug_row(container, "Flow rate");

    // BARU -- section diagnostik untuk investigasi "layar tiba-tiba
    // lompat ke Set Target saat GRINDING, sesekali/random". Lihat
    // catatan lengkap tiap field di debug_snapshot.h. Container induk
    // sudah flexbox+scroll (lihat deklarasi container di atas), jadi
    // menambah row di sini TIDAK perlu hitung ulang offset Y manual
    // apa pun -- row baru otomatis mengalir ke bawah, scroll
    // menyesuaikan sendiri.
    create_section_label(container, "DIAGNOSTIK SISTEM");
    s_reset_reason_value = create_debug_row(container, "Reset reason");
    s_home_gesture_value = create_debug_row(container, "Home gesture #");
    s_touch_recovery_value = create_debug_row(container, "Touch recovery #");

    lv_obj_t* back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(back_btn, 220, 60);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_set_style_radius(back_btn, 30, 0);
    lv_obj_add_event_cb(back_btn, ui_close_debug, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "< BACK TO SETTINGS");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(back_label, COLOR_ACCENT, 0);
    lv_obj_center(back_label);

    s_lastRefreshMs = 0;  // paksa refresh pertama SEGERA (bukan tunggu 300ms) begitu layar ini dibuka pertama kali
    return s_screen;
}
