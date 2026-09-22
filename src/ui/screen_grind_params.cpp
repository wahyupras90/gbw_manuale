#include "ui_common.h"
#include "../../include/config.h"
#include <cstdio>

// ============================================================
// GRIND PARAMETERS SCREEN -- 7 parameter GRIND dipindah dari
// Settings screen (screen_settings.cpp) ke layar terpisah ini,
// diakses via tombol "OPEN" di Settings. Semua logika callback,
// helper create_param_row/create_toggle_row, dan tombol SAVE
// dipindah ke sini VERBATIM -- tidak ada perubahan logika/layout,
// cuma dipindahkan supaya Settings utama lebih ringkas (tidak perlu
// scroll panjang hanya untuk mencapai Firmware Update/Manual Grind/
// Debug yang ada di bawah semua parameter).
//
// Tombol Back: kembali ke Settings (ui_close_grind_params()).
// Tombol SAVE: saveSettingsToNVS() lalu kembali ke Settings --
// SAMA seperti sebelumnya di screen_settings.cpp, cuma konteksnya
// sekarang ada di layar ini (bukan Settings).
// ============================================================

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_tolerance_value = nullptr;
static lv_obj_t* s_max_pulses_value = nullptr;
static lv_obj_t* s_settle_time_value = nullptr;
static lv_obj_t* s_stop_at_pct_value = nullptr;
static int s_stop_at_pct_minus_repeat = 0;
static int s_stop_at_pct_plus_repeat = 0;
static lv_obj_t* s_post_purge_toggle_btn = nullptr;
static lv_obj_t* s_post_purge_toggle_label = nullptr;
static lv_obj_t* s_post_purge_pulse_count_value = nullptr;
static lv_obj_t* s_stability_threshold_value = nullptr;
static int s_stability_threshold_minus_repeat = 0;
static int s_stability_threshold_plus_repeat = 0;

static void ui_update_toggle_visual(lv_obj_t* btn, lv_obj_t* label, bool state);

extern void ui_close_grind_params(lv_event_t* e);
extern void saveSettingsToNVS();
extern void ui_enable_swipe_home(lv_obj_t* screen);

static int s_tolerance_minus_repeat = 0;
static int s_tolerance_plus_repeat = 0;
static int s_max_pulses_minus_repeat = 0;
static int s_max_pulses_plus_repeat = 0;
static int s_settle_time_minus_repeat = 0;
static int s_settle_time_plus_repeat = 0;

static int s_post_purge_pulse_count_minus_repeat = 0;
static int s_post_purge_pulse_count_plus_repeat = 0;

static void tolerance_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_tolerance_minus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_tolerance_minus_repeat++;
    float step = 0.01f * ui_repeat_step_multiplier(s_tolerance_minus_repeat);
    g_ui_state.accuracy_tolerance_g -= step;
    if (g_ui_state.accuracy_tolerance_g < 0.1f) g_ui_state.accuracy_tolerance_g = 0.1f;
    char buf[8]; snprintf(buf, sizeof(buf), "%.2f", g_ui_state.accuracy_tolerance_g);
    lv_label_set_text(s_tolerance_value, buf);
}
static void tolerance_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_tolerance_plus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_tolerance_plus_repeat++;
    float step = 0.01f * ui_repeat_step_multiplier(s_tolerance_plus_repeat);
    g_ui_state.accuracy_tolerance_g += step;
    if (g_ui_state.accuracy_tolerance_g > 1.0f) g_ui_state.accuracy_tolerance_g = 1.0f;
    char buf[8]; snprintf(buf, sizeof(buf), "%.2f", g_ui_state.accuracy_tolerance_g);
    lv_label_set_text(s_tolerance_value, buf);
}

static void max_pulses_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_max_pulses_minus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_max_pulses_minus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_max_pulses_minus_repeat);
    g_ui_state.max_pulse_attempts -= step;
    if (g_ui_state.max_pulse_attempts < 1) g_ui_state.max_pulse_attempts = 1;
    char buf[8]; snprintf(buf, sizeof(buf), "%d", g_ui_state.max_pulse_attempts);
    lv_label_set_text(s_max_pulses_value, buf);
}
static void max_pulses_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_max_pulses_plus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_max_pulses_plus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_max_pulses_plus_repeat);
    g_ui_state.max_pulse_attempts += step;
    if (g_ui_state.max_pulse_attempts > 30) g_ui_state.max_pulse_attempts = 30;
    char buf[8]; snprintf(buf, sizeof(buf), "%d", g_ui_state.max_pulse_attempts);
    lv_label_set_text(s_max_pulses_value, buf);
}

static void settle_time_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_settle_time_minus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_settle_time_minus_repeat++;
    unsigned long step = 100UL * (unsigned long)ui_repeat_step_multiplier(s_settle_time_minus_repeat);
    if (step > g_ui_state.settle_time_ms) {
        g_ui_state.settle_time_ms = 200UL;
    } else {
        g_ui_state.settle_time_ms -= step;
        if (g_ui_state.settle_time_ms < 200UL) g_ui_state.settle_time_ms = 200UL;
    }
    char buf[8]; snprintf(buf, sizeof(buf), "%lu", g_ui_state.settle_time_ms);
    lv_label_set_text(s_settle_time_value, buf);
}
static void settle_time_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_settle_time_plus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_settle_time_plus_repeat++;
    unsigned long step = 100UL * (unsigned long)ui_repeat_step_multiplier(s_settle_time_plus_repeat);
    g_ui_state.settle_time_ms += step;
    if (g_ui_state.settle_time_ms > 2000UL) g_ui_state.settle_time_ms = 2000UL;
    char buf[8]; snprintf(buf, sizeof(buf), "%lu", g_ui_state.settle_time_ms);
    lv_label_set_text(s_settle_time_value, buf);
}

static void stop_at_pct_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_stop_at_pct_minus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_stop_at_pct_minus_repeat++;
    float step = 1.0f * ui_repeat_step_multiplier(s_stop_at_pct_minus_repeat);
    g_ui_state.stop_at_percent -= step;
    if (g_ui_state.stop_at_percent < 80.0f) g_ui_state.stop_at_percent = 80.0f;
    char buf[8]; snprintf(buf, sizeof(buf), "%.0f%%", g_ui_state.stop_at_percent);
    lv_label_set_text(s_stop_at_pct_value, buf);
}
static void stop_at_pct_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_stop_at_pct_plus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_stop_at_pct_plus_repeat++;
    float step = 1.0f * ui_repeat_step_multiplier(s_stop_at_pct_plus_repeat);
    g_ui_state.stop_at_percent += step;
    if (g_ui_state.stop_at_percent > 95.0f) g_ui_state.stop_at_percent = 95.0f;
    char buf[8]; snprintf(buf, sizeof(buf), "%.0f%%", g_ui_state.stop_at_percent);
    lv_label_set_text(s_stop_at_pct_value, buf);
}

static void post_purge_toggle_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    g_ui_state.post_purge_enabled = !g_ui_state.post_purge_enabled;
    ui_update_toggle_visual(s_post_purge_toggle_btn, s_post_purge_toggle_label, g_ui_state.post_purge_enabled);
}

static void post_purge_pulse_count_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_post_purge_pulse_count_minus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_post_purge_pulse_count_minus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_post_purge_pulse_count_minus_repeat);
    g_ui_state.post_purge_pulse_count -= step;
    if (g_ui_state.post_purge_pulse_count < 1) g_ui_state.post_purge_pulse_count = 1;
    char buf[8]; snprintf(buf, sizeof(buf), "%d", g_ui_state.post_purge_pulse_count);
    lv_label_set_text(s_post_purge_pulse_count_value, buf);
}
static void post_purge_pulse_count_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_post_purge_pulse_count_plus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_post_purge_pulse_count_plus_repeat++;
    int step = (int)ui_repeat_step_multiplier(s_post_purge_pulse_count_plus_repeat);
    g_ui_state.post_purge_pulse_count += step;
    if (g_ui_state.post_purge_pulse_count > 5) g_ui_state.post_purge_pulse_count = 5;
    char buf[8]; snprintf(buf, sizeof(buf), "%d", g_ui_state.post_purge_pulse_count);
    lv_label_set_text(s_post_purge_pulse_count_value, buf);
}

static void stability_threshold_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_stability_threshold_minus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_stability_threshold_minus_repeat++;
    float step = 0.1f * ui_repeat_step_multiplier(s_stability_threshold_minus_repeat);
    g_ui_state.stability_threshold_g -= step;
    if (g_ui_state.stability_threshold_g < 0.1f) g_ui_state.stability_threshold_g = 0.1f;
    char buf[8]; snprintf(buf, sizeof(buf), "%.1f", g_ui_state.stability_threshold_g);
    lv_label_set_text(s_stability_threshold_value, buf);
}
static void stability_threshold_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_stability_threshold_plus_repeat = 0; return; }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_stability_threshold_plus_repeat++;
    float step = 0.1f * ui_repeat_step_multiplier(s_stability_threshold_plus_repeat);
    g_ui_state.stability_threshold_g += step;
    if (g_ui_state.stability_threshold_g > 1.0f) g_ui_state.stability_threshold_g = 1.0f;
    char buf[8]; snprintf(buf, sizeof(buf), "%.1f", g_ui_state.stability_threshold_g);
    lv_label_set_text(s_stability_threshold_value, buf);
}

static void save_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    saveSettingsToNVS();
    ui_close_grind_params(e);
}

// Helpers -- identik dengan versi di screen_settings.cpp lama
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

    lv_obj_t* stepper_row = lv_obj_create(row);
    lv_obj_set_size(stepper_row, SCREEN_WIDTH - 40 - 12, 46);
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
    lv_obj_add_event_cb(minus_btn, minus_cb, LV_EVENT_ALL, NULL);
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
    lv_obj_add_event_cb(plus_btn, plus_cb, LV_EVENT_ALL, NULL);
    lv_obj_t* plus_label = lv_label_create(plus_btn);
    lv_label_set_text(plus_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(plus_label, COLOR_ACCENT, 0);
    lv_obj_center(plus_label);
}

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

    lv_obj_t* desc_label = lv_label_create(row);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(desc_label, 110);
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

    *out_toggle_btn = toggle_btn;
    *out_toggle_label = toggle_label;
    (void)initial_state;
}

static void ui_update_toggle_visual(lv_obj_t* btn, lv_obj_t* label, bool state) {
    if (state) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a4a2a), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, COLOR_SUCCESS, 0);
        lv_label_set_text(label, "ON");
        lv_obj_set_style_text_color(label, COLOR_SUCCESS, 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x4a4a4a), 0);
        lv_label_set_text(label, "OFF");
        lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    }
}

lv_obj_t* ui_screen_grind_params_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, nullptr);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "GRIND PARAMETERS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    // Scroll area -- sama persis dengan pola Settings lama
    lv_obj_t* scroll_area = lv_obj_create(s_screen);
    lv_obj_set_size(scroll_area, SCREEN_WIDTH, 456 - (STATUS_BAR_HEIGHT + 36) - 96);
    lv_obj_align(scroll_area, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 36);
    lv_obj_set_style_bg_opa(scroll_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_area, 0, 0);
    lv_obj_set_style_pad_all(scroll_area, 0, 0);
    lv_obj_set_scroll_dir(scroll_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_area, LV_SCROLLBAR_MODE_AUTO);

    // 7 parameter rows -- offset identik dengan screen_settings.cpp lama
    char tol_buf[8]; snprintf(tol_buf, sizeof(tol_buf), "%.2f", g_ui_state.accuracy_tolerance_g);
    create_param_row(scroll_area, 0, "Tolerance", "Accuracy window (g)",
                     &s_tolerance_value, tolerance_minus_cb, tolerance_plus_cb, tol_buf);

    char pulse_buf[8]; snprintf(pulse_buf, sizeof(pulse_buf), "%d", g_ui_state.max_pulse_attempts);
    create_param_row(scroll_area, 108, "Max Pulses", "Pulse attempt limit",
                     &s_max_pulses_value, max_pulses_minus_cb, max_pulses_plus_cb, pulse_buf);

    char settle_buf[8]; snprintf(settle_buf, sizeof(settle_buf), "%lu", g_ui_state.settle_time_ms);
    create_param_row(scroll_area, 216, "Settle Time", "Scale settle (ms)",
                     &s_settle_time_value, settle_time_minus_cb, settle_time_plus_cb, settle_buf);

    char stop_pct_buf[8]; snprintf(stop_pct_buf, sizeof(stop_pct_buf), "%.0f%%", g_ui_state.stop_at_percent);
    create_param_row(scroll_area, 324, "Stop At %", "Motor stop saat berat >= target x pct (80-95%)",
                     &s_stop_at_pct_value, stop_at_pct_minus_cb, stop_at_pct_plus_cb, stop_pct_buf);

    create_toggle_row(scroll_area, 432, "Post-Purge", "Getar buang sisa chute",
                      &s_post_purge_toggle_btn, &s_post_purge_toggle_label,
                      post_purge_toggle_cb, g_ui_state.post_purge_enabled);
    ui_update_toggle_visual(s_post_purge_toggle_btn, s_post_purge_toggle_label, g_ui_state.post_purge_enabled);

    char purge_pulse_buf[8]; snprintf(purge_pulse_buf, sizeof(purge_pulse_buf), "%d", g_ui_state.post_purge_pulse_count);
    create_param_row(scroll_area, 628, "Purge Pulses", "Jumlah pulsa getar",
                     &s_post_purge_pulse_count_value, post_purge_pulse_count_minus_cb, post_purge_pulse_count_plus_cb, purge_pulse_buf);

    char stab_buf[8]; snprintf(stab_buf, sizeof(stab_buf), "%.1f", g_ui_state.stability_threshold_g);
    create_param_row(scroll_area, 736, "Stability", "Pre-grind stabil (g)",
                     &s_stability_threshold_value, stability_threshold_minus_cb, stability_threshold_plus_cb, stab_buf);

    // SAVE button -- fixed di bawah, tidak ikut scroll (child s_screen)
    lv_obj_t* save_btn = lv_btn_create(s_screen);
    lv_obj_set_size(save_btn, 220, 60);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(save_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(save_btn, 30, 0);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "SAVE");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(save_label, lv_color_hex(0x1a1305), 0);
    lv_obj_center(save_label);

    ui_enable_swipe_home(s_screen);

    return s_screen;
}
