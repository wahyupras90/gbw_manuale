#include "ui_common.h"
#include <cstdio>
#include <math.h>
#include <Preferences.h>

static lv_obj_t* s_screen      = nullptr;
static lv_obj_t* s_target_label = nullptr;
static lv_obj_t* s_reject_label = nullptr;

static float s_pending_target = 18.0f;

extern void ui_open_settings(lv_event_t* e);
extern void ui_confirm_target(float target_g);

// NVS persistence untuk last_target
static float load_last_target() {
    Preferences prefs;
    prefs.begin("gbw", true);
    float v = prefs.getFloat("last_target", 18.0f);
    prefs.end();
    if (!isfinite(v) || v < 1.0f || v > 50.0f) v = 18.0f;
    // Snap ke preset terdekat: < 14g → 10g, >= 14g → 18g
    return (v < 14.0f) ? 10.0f : 18.0f;
}

static void save_last_target(float v) {
    Preferences prefs;
    prefs.begin("gbw", false);
    prefs.putFloat("last_target", v);
    prefs.end();
}

static void update_target_label(void) {
    char buf[16];
    if (fabsf(s_pending_target - roundf(s_pending_target)) < 0.05f) {
        snprintf(buf, sizeof(buf), "%.0fg", s_pending_target);
    } else {
        snprintf(buf, sizeof(buf), "%.1fg", s_pending_target);
    }
    lv_label_set_text(s_target_label, buf);
    // Kosongkan reject label saat nilai berubah
    if (s_reject_label) lv_label_set_text(s_reject_label, "");
}

static int s_plus_repeat  = 0;
static int s_minus_repeat = 0;

static void step_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_plus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_plus_repeat++;
    float step = 0.1f * ui_repeat_step_multiplier(s_plus_repeat);
    s_pending_target += step;
    if (s_pending_target > 50.0f) s_pending_target = 50.0f;
    update_target_label();
}

static void step_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_minus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_minus_repeat++;
    float step = 0.1f * ui_repeat_step_multiplier(s_minus_repeat);
    s_pending_target -= step;
    if (s_pending_target < 1.0f) s_pending_target = 1.0f;
    update_target_label();
}

static void confirm_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    save_last_target(s_pending_target);
    ui_confirm_target(s_pending_target);
}

// Swipe kiri/kanan untuk preset 10g/18g
static void swipe_preset_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_indev_t* indev = lv_indev_get_act();
    if (indev == nullptr) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        s_pending_target = 10.0f;
        update_target_label();
    } else if (dir == LV_DIR_RIGHT) {
        s_pending_target = 18.0f;
        update_target_label();
    }
}

lv_obj_t* ui_screen_set_target_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Load last_target dari NVS saat screen di-create (saat boot)
    s_pending_target = load_last_target();

    ui_create_status_bar(s_screen, ui_open_settings);

    // Title
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "TARGET WEIGHT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 20);

    // Kotak angka tappable -- klik = START
    lv_obj_t* number_box = lv_btn_create(s_screen);
    lv_obj_set_size(number_box, SCREEN_WIDTH - 32, 148);
    lv_obj_align(number_box, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 64);
    lv_obj_set_style_bg_color(number_box, lv_color_hex(0x1e1a0e), 0);
    lv_obj_set_style_border_color(number_box, COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(number_box, 2, 0);
    lv_obj_set_style_radius(number_box, 24, 0);
    lv_obj_add_event_cb(number_box, confirm_cb, LV_EVENT_CLICKED, NULL);

    s_target_label = lv_label_create(number_box);
    lv_obj_set_style_text_font(s_target_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_target_label, COLOR_ACCENT, 0);
    lv_obj_center(s_target_label);
    update_target_label();

    // Tombol MINUS
    lv_obj_t* minus_btn = lv_btn_create(s_screen);
    lv_obj_set_size(minus_btn, 116, 80);
    lv_obj_align(minus_btn, LV_ALIGN_TOP_MID, -(116/2 + 4), STATUS_BAR_HEIGHT + 238);
    lv_obj_set_style_radius(minus_btn, 16, 0);
    lv_obj_set_style_bg_color(minus_btn, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(minus_btn, 1, 0);
    lv_obj_set_style_border_color(minus_btn, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(minus_btn, step_minus_cb, LV_EVENT_ALL, NULL);
    lv_obj_t* minus_label = lv_label_create(minus_btn);
    lv_label_set_text(minus_label, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(minus_label, COLOR_ACCENT, 0);
    lv_obj_center(minus_label);

    // Tombol PLUS
    lv_obj_t* plus_btn = lv_btn_create(s_screen);
    lv_obj_set_size(plus_btn, 116, 80);
    lv_obj_align(plus_btn, LV_ALIGN_TOP_MID, (116/2 + 4), STATUS_BAR_HEIGHT + 238);
    lv_obj_set_style_radius(plus_btn, 16, 0);
    lv_obj_set_style_bg_color(plus_btn, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(plus_btn, 1, 0);
    lv_obj_set_style_border_color(plus_btn, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(plus_btn, step_plus_cb, LV_EVENT_ALL, NULL);
    lv_obj_t* plus_label = lv_label_create(plus_btn);
    lv_label_set_text(plus_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(plus_label, COLOR_ACCENT, 0);
    lv_obj_center(plus_label);

    // Reject label -- tampilkan pesan error grind ditolak
    s_reject_label = lv_label_create(s_screen);
    lv_label_set_text(s_reject_label, "");
    lv_obj_set_style_text_font(s_reject_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_reject_label, COLOR_WARN, 0);
    lv_obj_set_width(s_reject_label, SCREEN_WIDTH - 32);
    lv_obj_set_style_text_align(s_reject_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_reject_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_reject_label, LV_ALIGN_BOTTOM_MID, 0, -100);

    // Tombol START
    lv_obj_t* start_btn = lv_btn_create(s_screen);
    lv_obj_set_size(start_btn, 220, 60);
    lv_obj_align(start_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(start_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(start_btn, 30, 0);
    lv_obj_add_event_cb(start_btn, confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "START");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(start_label, lv_color_hex(0x1a1305), 0);
    lv_obj_center(start_label);

    // Swipe kiri/kanan = preset 10g/18g (di seluruh screen)
    lv_obj_add_event_cb(s_screen, swipe_preset_cb, LV_EVENT_GESTURE, NULL);

    return s_screen;
}
