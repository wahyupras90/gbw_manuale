#include "ui_common.h"
#include "../../include/debug_snapshot.h"
#include <Arduino.h>
#include <cstdio>
#include <math.h>

extern DebugSnapshot grind_get_debug_snapshot();
extern void ui_close_scale(lv_event_t* e);

// ============================================================
// SCALE SCREEN -- tampilan timbangan HX711 dan akses kalibrasi.
// Dipindah dari Debug screen supaya Debug fokus ke diagnostik grind.
// ============================================================

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_raw_value         = nullptr;
static lv_obj_t* s_offset_value      = nullptr;
static lv_obj_t* s_scale_value       = nullptr;
static lv_obj_t* s_weight_value      = nullptr;

static unsigned long s_lastRefreshMs  = 0;
static const unsigned long SCALE_REFRESH_INTERVAL_MS = 300;

extern void ui_open_calibration_wizard(lv_event_t* e);

// ── helper row (label kiri, value kanan) ────────────────────
static lv_obj_t* create_scale_row(lv_obj_t* parent, const char* label_text) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 32, 34);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1c1c1c), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(value, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);

    return value;
}

// ── update data ─────────────────────────────────────────────
void ui_screen_scale_update(void) {
    if (s_screen == nullptr) return;

    unsigned long now = millis();
    if (now - s_lastRefreshMs < SCALE_REFRESH_INTERVAL_MS) return;
    s_lastRefreshMs = now;

    DebugSnapshot snap = grind_get_debug_snapshot();
    char buf[32];

    if (snap.rawAdc == -2) {
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
}

// ── create screen ────────────────────────────────────────────
lv_obj_t* ui_screen_scale_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, nullptr);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "SCALE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 14);

    // Container rows
    lv_obj_t* container = lv_obj_create(s_screen);
    lv_obj_set_size(container, SCREEN_WIDTH, 456 - (STATUS_BAR_HEIGHT + 44) - 96);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container, 6, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);

    s_raw_value    = create_scale_row(container, "Raw ADC");
    s_offset_value = create_scale_row(container, "Offset aktif");
    s_scale_value  = create_scale_row(container, "Scale aktif");
    s_weight_value = create_scale_row(container, "Berat (gram)");

    // Tombol kalibrasi
    lv_obj_t* calib_btn = lv_btn_create(container);
    lv_obj_set_size(calib_btn, SCREEN_WIDTH - 32, 44);
    lv_obj_set_style_bg_color(calib_btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(calib_btn, 1, 0);
    lv_obj_set_style_border_color(calib_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_set_style_radius(calib_btn, 12, 0);
    lv_obj_add_event_cb(calib_btn, ui_open_calibration_wizard, LV_EVENT_CLICKED, NULL);
    lv_obj_t* calib_label = lv_label_create(calib_btn);
    lv_label_set_text(calib_label, "KALIBRASI ULANG");
    lv_obj_set_style_text_font(calib_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(calib_label, COLOR_ACCENT, 0);
    lv_obj_center(calib_label);

    // Tombol Back
    lv_obj_t* back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(back_btn, 220, 60);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_set_style_radius(back_btn, 30, 0);
    lv_obj_add_event_cb(back_btn, ui_close_scale, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "< BACK TO SETTINGS");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(back_label, COLOR_ACCENT, 0);
    lv_obj_center(back_label);

    s_lastRefreshMs = 0;
    return s_screen;
}

void ui_screen_scale_destroy(void) {
    if (s_screen) {
        lv_obj_del(s_screen);
        s_screen = nullptr;
    }
    s_raw_value = s_offset_value = s_scale_value = s_weight_value = nullptr;
}
