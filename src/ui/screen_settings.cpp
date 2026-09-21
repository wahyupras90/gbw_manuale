#include "ui_common.h"
#include "../../include/config.h"
#include <cstdio>
#include "../../include/github_ota.h"
#include "../../include/version.h"

// ============================================================
// SETTINGS SCREEN -- ringkas, 4 baris saja:
//   1. Grind Parameters (OPEN -> UI_SCREEN_GRIND_PARAMS)
//   2. Firmware Update (CHECK)
//   3. Manual Grind (OPEN)
//   4. Debug (OPEN)
//
// Semua 7 parameter grind (Tolerance/Max Pulses/Settle Time/Coast
// Ratio/Confirm Window/Post-Purge/Purge Pulses) dan tombol SAVE
// sudah DIPINDAH ke screen_grind_params.cpp -- diakses via baris
// "Grind Parameters" di bawah. Tidak ada scroll panjang lagi di
// layar ini.
// ============================================================

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_current_version_label = nullptr;
static lv_obj_t* s_latest_version_label = nullptr;

extern void ui_close_settings(lv_event_t* e);
extern void ui_enable_swipe_home(lv_obj_t* screen);
extern void ui_open_grind_params(lv_event_t* e);
extern void ui_open_manual_grind(lv_event_t* e);
extern void ui_open_scale(lv_event_t* e);
extern void ui_open_debug(lv_event_t* e);

static void check_update_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_label_set_text(s_latest_version_label, "Checking...");
    lv_refr_now(NULL);
    githubOta.checkAndUpdate();
    if (githubOta.latestVersion().length() > 0) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Latest: %s (%s)", githubOta.latestVersion().c_str(), githubOta.statusText());
        lv_label_set_text(s_latest_version_label, buf);
    } else {
        lv_label_set_text(s_latest_version_label, githubOta.statusText());
    }
}

// Helper baris dengan tombol OPEN (dipakai untuk Grind Params, Manual Grind, Debug)
static void create_open_row(lv_obj_t* parent, int /*y_offset*/, const char* name, const char* desc,
                             lv_event_cb_t open_cb) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 80);
    // Tidak pakai lv_obj_align -- posisi diatur flex container parent
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
    lv_obj_add_event_cb(open_btn, open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* open_label = lv_label_create(open_btn);
    lv_label_set_text(open_label, "OPEN");
    lv_obj_set_style_text_font(open_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(open_label, COLOR_ACCENT, 0);
    lv_obj_center(open_label);
}

static void create_update_row(lv_obj_t* parent, int /*y_offset*/) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH - 40, 80);
    // Tidak pakai lv_obj_align -- posisi diatur flex container parent
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

    s_current_version_label = lv_label_create(row);
    char cur_buf[24];
    snprintf(cur_buf, sizeof(cur_buf), "Running: %s", FIRMWARE_VERSION);
    lv_label_set_text(s_current_version_label, cur_buf);
    lv_obj_set_style_text_font(s_current_version_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_current_version_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align_to(s_current_version_label, name_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

    s_latest_version_label = lv_label_create(row);
    lv_label_set_text(s_latest_version_label, "Latest: (press CHECK)");
    lv_obj_set_style_text_font(s_latest_version_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_latest_version_label, COLOR_ACCENT, 0);
    lv_obj_align_to(s_latest_version_label, s_current_version_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

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

lv_obj_t* ui_screen_settings_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, nullptr);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    // Container scrollable -- konten row di sini, tombol Back di luar
    // (fixed di BOTTOM_MID s_screen, konsisten dengan screen lain).
    // Tinggi container = SCREEN_HEIGHT - title area - tombol Back area
    // = 456 - (STATUS_BAR_HEIGHT + 44) - 96 = 294px
    lv_obj_t* container = lv_obj_create(s_screen);
    lv_obj_set_size(container, SCREEN_WIDTH, 456 - (STATUS_BAR_HEIGHT + 44) - 96);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 8, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);

    create_open_row(container, 0, "Grind Parameters", "Tolerance, Coast Ratio, dll", ui_open_grind_params);
    create_update_row(container, 0);
    create_open_row(container, 0, "Manual Grind", "Test motor / calibrate grind size", ui_open_manual_grind);
    create_open_row(container, 0, "Scale", "Timbangan HX711 & kalibrasi", ui_open_scale);
    create_open_row(container, 0, "Debug", "Diagnostik grind & sistem", ui_open_debug);

    // Tombol Back -- fixed di s_screen, konsisten dengan screen lain
    lv_obj_t* back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(back_btn, 220, 60);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, COLOR_ACCENT_DIM, 0);
    lv_obj_set_style_radius(back_btn, 30, 0);
    lv_obj_add_event_cb(back_btn, ui_close_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "< BACK");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(back_label, COLOR_ACCENT, 0);
    lv_obj_center(back_label);

    ui_enable_swipe_home(s_screen);

    return s_screen;
}
