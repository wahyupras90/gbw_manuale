#include "ui_common.h"
#include <cstdio>
#include <math.h>

static lv_obj_t* s_screen       = nullptr;
static lv_obj_t* s_target_label = nullptr;
static lv_obj_t* s_number_box   = nullptr;  // kotak tappable di sekeliling angka

static float s_pending_target = 18.0f;  // default 18g

extern void ui_open_settings(lv_event_t* e);
extern void ui_confirm_target(float target_g);

static void update_target_label(void) {
    char buf[16];
    // Tampilkan tanpa desimal jika bulat, dengan desimal jika tidak
    if (fabsf(s_pending_target - roundf(s_pending_target)) < 0.05f) {
        snprintf(buf, sizeof(buf), "%.0fg", s_pending_target);
    } else {
        snprintf(buf, sizeof(buf), "%.1fg", s_pending_target);
    }
    lv_label_set_text(s_target_label, buf);
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
    if (s_pending_target < 1.0f) s_pending_target = 1.0f;  // minimum 1g
    update_target_label();
}

static void confirm_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_confirm_target(s_pending_target);
}

lv_obj_t* ui_screen_set_target_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, ui_open_settings);

    // ----------------------------------------------------------------
    // Layout vertikal (dari mockup final):
    //   STATUS_BAR_HEIGHT = 22px
    //   margin atas title  = 20px  → title y_offset = 20
    //   title height       ≈ 18px
    //   gap                = 26px
    //   number box         = 148px
    //   gap                = 26px
    //   +/- buttons        = 80px
    //   gap                = 26px
    //   START button       = 60px (LV_ALIGN_BOTTOM_MID, -28)
    // ----------------------------------------------------------------

    // "TARGET WEIGHT" -- bold, putih
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "TARGET WEIGHT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 20);

    // Kotak angka tappable -- klik = START
    // y_offset: 20(margin) + 18(title) + 26(gap) = 64
    s_number_box = lv_btn_create(s_screen);
    lv_obj_set_size(s_number_box, SCREEN_WIDTH - 32, 148);
    lv_obj_align(s_number_box, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 64);
    lv_obj_set_style_bg_color(s_number_box, lv_color_hex(0x1e1a0e), 0);
    lv_obj_set_style_border_color(s_number_box, COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(s_number_box, 2, 0);
    lv_obj_set_style_radius(s_number_box, 24, 0);
    lv_obj_add_event_cb(s_number_box, confirm_cb, LV_EVENT_CLICKED, NULL);

    // Angka di dalam kotak -- font terbesar yang tersedia (montserrat_48)
    // untuk tampilan besar sesuai mockup
    s_target_label = lv_label_create(s_number_box);
    lv_obj_set_style_text_font(s_target_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_target_label, COLOR_ACCENT, 0);
    lv_obj_center(s_target_label);
    update_target_label();

    // Tombol MINUS -- y_offset: 64+148+26 = 238, h=80
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

    // Tombol START -- posisi standar semua screen: BOTTOM_MID, -28
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

    return s_screen;
}
