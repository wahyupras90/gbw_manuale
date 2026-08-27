#include "ui_common.h"
#include <cstdio>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: snprintf() dipakai di bawah tanpa include ini
#include <math.h>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: fabsf() dipakai di bawah -- WAJIB <math.h> (C-style, global namespace), BUKAN <cmath> (taruh di std::)

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_target_label = nullptr;
static lv_obj_t* s_preset_btns[3] = {nullptr, nullptr, nullptr};
static float s_pending_target = 18.0f;  // default, disinkron ke g_ui_state saat Confirm
static int s_selected_preset = 1;       // 0=Single, 1=Double, 2=Custom -- default Double sesuai mockup

extern void ui_open_settings(lv_event_t* e);
extern void ui_confirm_target(float target_g);  // di ui_screen_manager

static void update_preset_highlight(void) {
    for (int i = 0; i < 3; i++) {
        if (s_preset_btns[i] == nullptr) continue;
        bool selected = (i == s_selected_preset);
        lv_obj_set_style_bg_color(s_preset_btns[i], selected ? lv_color_hex(0x2a2412) : COLOR_BG_CARD, 0);
        lv_obj_set_style_border_color(s_preset_btns[i], selected ? COLOR_ACCENT : lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(s_preset_btns[i], selected ? 2 : 1, 0);
        lv_obj_t* label = lv_obj_get_child(s_preset_btns[i], 0);
        if (label) {
            lv_obj_set_style_text_color(label, selected ? COLOR_ACCENT : COLOR_TEXT_SECONDARY, 0);
        }
    }
}

static void update_target_label(void) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fg", s_pending_target);
    lv_label_set_text(s_target_label, buf);
}

// Stepper manual (+/-) menandakan target custom -- lepas highlight
// preset kecuali nilainya kebetulan pas cocok dengan salah satu preset.
static void sync_preset_from_target(void) {
    if (fabsf(s_pending_target - 9.0f) < 0.05f) {
        s_selected_preset = 0;
    } else if (fabsf(s_pending_target - 18.0f) < 0.05f) {
        s_selected_preset = 1;
    } else {
        s_selected_preset = 2;  // Custom
    }
    update_preset_highlight();
}

// Counter repeat per tombol (independen) -- lihat ui_repeat_step_multiplier()
// di ui_common.h untuk kurva percepatan & penjelasan mekanisme.
static int s_plus_repeat = 0;
static int s_minus_repeat = 0;

static void step_plus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_plus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_plus_repeat++;
    float step = 0.1f * ui_repeat_step_multiplier(s_plus_repeat);
    s_pending_target += step;
    if (s_pending_target > 50.0f) s_pending_target = 50.0f;  // batas atas wajar
    update_target_label();
    sync_preset_from_target();
}

static void step_minus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_minus_repeat = 0;
        return;
    }
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) s_minus_repeat++;
    float step = 0.1f * ui_repeat_step_multiplier(s_minus_repeat);
    s_pending_target -= step;
    if (s_pending_target < 5.0f) s_pending_target = 5.0f;  // batas bawah wajar
    update_target_label();
    sync_preset_from_target();
}

static void preset_single_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_pending_target = 9.0f;
    s_selected_preset = 0;
    update_target_label();
    update_preset_highlight();
}

static void preset_double_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_pending_target = 18.0f;
    s_selected_preset = 1;
    update_target_label();
    update_preset_highlight();
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

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "TARGET WEIGHT");
    // DIPERBESAR (permintaan eksplisit -- "target weight dan quick
    // presets ukurannya terlalu kecil, tidak proporsional" dibanding
    // angka besar 40px): 12px -> 16px. Font 16 ditambahkan ke
    // lv_conf.h (LV_FONT_MONTSERRAT_16, sebelumnya cuma 12/14/32 aktif).
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    // Angka target besar di tengah
    s_target_label = lv_label_create(s_screen);
    // DIPERBESAR (permintaan eksplisit): 32px -> 40px. Font baru
    // ditambahkan ke lv_conf.h (LV_FONT_MONTSERRAT_40).
    lv_obj_set_style_text_font(s_target_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_target_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(s_target_label, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 50);
    update_target_label();

    // Tombol +/- di bawah angka
    // DIPERLEBAR (permintaan eksplisit -- "tombol + dan minus bisa
    // diperlebar, ada ruang di kanan kirinya"): 90x56 -> 110x60,
    // offset X disesuaikan (-65/+65, dari -55/+55) supaya tetap
    // simetris dengan lebar baru dan tidak saling menabrak di tengah.
    lv_obj_t* minus_btn = lv_btn_create(s_screen);
    lv_obj_set_size(minus_btn, 110, 60);
    lv_obj_align(minus_btn, LV_ALIGN_TOP_MID, -65, STATUS_BAR_HEIGHT + 118);
    lv_obj_set_style_radius(minus_btn, 16, 0);
    lv_obj_set_style_bg_color(minus_btn, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(minus_btn, 1, 0);
    lv_obj_set_style_border_color(minus_btn, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(minus_btn, step_minus_cb, LV_EVENT_ALL, NULL);  // LV_EVENT_ALL -- perlu PRESSED (reset counter) & LONG_PRESSED_REPEAT (percepatan tekan-tahan)
    lv_obj_t* minus_label = lv_label_create(minus_btn);
    lv_label_set_text(minus_label, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(minus_label, COLOR_ACCENT, 0);
    lv_obj_center(minus_label);

    lv_obj_t* plus_btn = lv_btn_create(s_screen);
    lv_obj_set_size(plus_btn, 110, 60);
    lv_obj_align(plus_btn, LV_ALIGN_TOP_MID, 65, STATUS_BAR_HEIGHT + 118);
    lv_obj_set_style_radius(plus_btn, 16, 0);
    lv_obj_set_style_bg_color(plus_btn, COLOR_BG_CARD, 0);
    lv_obj_set_style_border_width(plus_btn, 1, 0);
    lv_obj_set_style_border_color(plus_btn, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(plus_btn, step_plus_cb, LV_EVENT_ALL, NULL);  // LV_EVENT_ALL -- lihat catatan di minus_btn di atas
    lv_obj_t* plus_label = lv_label_create(plus_btn);
    lv_label_set_text(plus_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(plus_label, COLOR_ACCENT, 0);
    lv_obj_center(plus_label);

    // Label "QUICK PRESETS"
    lv_obj_t* presets_title = lv_label_create(s_screen);
    lv_label_set_text(presets_title, "QUICK PRESETS");
    lv_obj_set_style_text_font(presets_title, &lv_font_montserrat_16, 0);  // DIPERBESAR, lihat catatan di title
    lv_obj_set_style_text_color(presets_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(presets_title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 206);

    // Preset row: Single / Double SAJA
    // KOREKSI (permintaan eksplisit -- "tombol custom tidak
    // berfungsi" -> diputuskan DIHAPUS, bukan diperbaiki, karena
    // operator tetap bisa set nilai bebas lewat tombol +/- tanpa
    // perlu tombol Custom terpisah -- lihat sync_preset_from_target()
    // yang sudah otomatis lepas highlight preset begitu nilai digeser
    // manual). SEKARANG cuma 2 tombol (Single/Double), lebih lebar
    // mengisi ruang yang sebelumnya dipakai Custom.
    lv_obj_t* preset_row = lv_obj_create(s_screen);
    lv_obj_set_size(preset_row, SCREEN_WIDTH - 32, 60);
    lv_obj_align(preset_row, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 260);
    lv_obj_set_style_bg_opa(preset_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(preset_row, 0, 0);
    lv_obj_set_flex_flow(preset_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(preset_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(preset_row, LV_OBJ_FLAG_SCROLLABLE);

    const char* preset_names[] = {"Single\n9g", "Double\n18g"};
    lv_event_cb_t preset_cbs[] = {preset_single_cb, preset_double_cb};
    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_btn_create(preset_row);
        // DIPERLEBAR (mengisi ruang bekas tombol Custom yang dihapus):
        // 73x60 -> 110x60.
        lv_obj_set_size(btn, 110, 60);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_add_event_cb(btn, preset_cbs[i], LV_EVENT_CLICKED, NULL);
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, preset_names[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);  // DIPERBESAR, tombol sekarang lebih lebar
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        s_preset_btns[i] = btn;
    }
    update_preset_highlight();

    // Tombol Confirm di bawah
    // STANDARISASI (permintaan eksplisit -- semua tombol bawah harus
    // identik ukuran & posisi: 220x60, offset -28 dari bawah -- lihat
    // catatan lengkap di screen_idle.cpp). SEBELUMNYA dipakai offset
    // -70 sebagai perbaikan sementara untuk gap preset_row/confirm_btn
    // -- sekarang gap itu diatasi dengan menggeser preset_row TURUN
    // (lebih dekat ke confirm_btn), bukan menggeser confirm_btn naik,
    // supaya posisi Y tombol tetap konsisten dengan layar lain.
    lv_obj_t* confirm_btn = lv_btn_create(s_screen);
    lv_obj_set_size(confirm_btn, 220, 60);
    lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(confirm_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(confirm_btn, 30, 0);
    lv_obj_add_event_cb(confirm_btn, confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "CONFIRM");
    lv_obj_set_style_text_font(confirm_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(confirm_label, lv_color_hex(0x1a1305), 0);
    lv_obj_center(confirm_label);

    return s_screen;
}
