#include "ui_common.h"
#include <cstdio>
#include <math.h>
#include <Preferences.h>

// Range roller: 5.0 - 25.0, step 0.1 = 201 item
#define ROLLER_MIN_G     5.0f
#define ROLLER_MAX_G    25.0f
#define ROLLER_STEP_G    0.1f
#define ROLLER_COUNT   201   // (25.0 - 5.0) / 0.1 + 1

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_roller = nullptr;

extern void ui_open_settings(lv_event_t* e);
extern void ui_confirm_target(float target_g);

// ----------------------------------------------------------------
// NVS helpers
// ----------------------------------------------------------------
static float load_last_target() {
    Preferences prefs;
    prefs.begin("gbw", true);
    float v = prefs.getFloat("last_target", 18.0f);
    prefs.end();
    if (!isfinite(v) || v < ROLLER_MIN_G || v > ROLLER_MAX_G) v = 18.0f;
    // Snap ke preset terdekat: < 14g -> 10g, >= 14g -> 18g
    return (v < 14.0f) ? 10.0f : 18.0f;
}

static void save_last_target(float v) {
    Preferences prefs;
    prefs.begin("gbw", false);
    prefs.putFloat("last_target", v);
    prefs.end();
}

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------
static float roller_index_to_g(uint16_t idx) {
    return ROLLER_MIN_G + idx * ROLLER_STEP_G;
}

static uint16_t g_to_roller_index(float g) {
    if (g < ROLLER_MIN_G) g = ROLLER_MIN_G;
    if (g > ROLLER_MAX_G) g = ROLLER_MAX_G;
    return (uint16_t)roundf((g - ROLLER_MIN_G) / ROLLER_STEP_G);
}

// ----------------------------------------------------------------
// Callbacks
// ----------------------------------------------------------------
static void start_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint16_t idx = lv_roller_get_selected(s_roller);
    float target = roller_index_to_g(idx);
    save_last_target(target);
    ui_confirm_target(target);
}

// ----------------------------------------------------------------
// Create
// ----------------------------------------------------------------
lv_obj_t* ui_screen_set_target_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, ui_open_settings);

    // Title
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "TARGET WEIGHT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 20);

    // Bangun options string: "5.0\n5.1\n5.2\n..."
    // 201 item x 5 char ("18.0\n") = ~1005 bytes
    static char s_options[1100];
    int pos = 0;
    for (int i = 0; i < ROLLER_COUNT; i++) {
        float g = ROLLER_MIN_G + i * ROLLER_STEP_G;
        pos += snprintf(s_options + pos, sizeof(s_options) - pos,
                        "%.1f%s", g, (i < ROLLER_COUNT - 1) ? "\n" : "");
    }

    // Roller
    s_roller = lv_roller_create(s_screen);
    lv_roller_set_options(s_roller, s_options, LV_ROLLER_MODE_NORMAL);

    // Ukuran: lebar penuh - margin, tinggi auto dari row count
    lv_obj_set_width(s_roller, SCREEN_WIDTH);
    lv_obj_set_style_pad_all(s_roller, 0, 0);
    lv_obj_set_style_text_align(s_roller, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_roller, 12, 0);
    lv_obj_align(s_roller, LV_ALIGN_CENTER, 0, -10);

    // Style roller — background transparan, tanpa border
    lv_obj_set_style_bg_opa(s_roller, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_roller, 0, 0);
    lv_obj_set_style_shadow_width(s_roller, 0, 0);

    // Style item normal (muted)
    lv_obj_set_style_text_font(s_roller, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_roller, lv_color_hex(0x666666), 0);

    // Style item selected (accent, besar)
    lv_obj_set_style_text_font(s_roller, &lv_font_montserrat_48, LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_roller, COLOR_ACCENT, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(s_roller, LV_OPA_TRANSP, LV_PART_SELECTED);
    lv_obj_set_style_border_width(s_roller, 0, LV_PART_SELECTED);

    // Tinggi dihitung dari font MAIN + line_space -> panggil SETELAH keduanya diset
    lv_roller_set_visible_row_count(s_roller, 3);
    lv_obj_align(s_roller, LV_ALIGN_CENTER, 0, -10);

    lv_obj_clear_flag(s_roller, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Set posisi awal dari NVS
    float initial = load_last_target();
    lv_obj_update_layout(s_roller);
    lv_roller_set_selected(s_roller, g_to_roller_index(initial), LV_ANIM_OFF);
    lv_obj_invalidate(s_roller);

    // Tombol START — posisi standar BOTTOM_MID -28
    lv_obj_t* start_btn = lv_btn_create(s_screen);
    lv_obj_set_size(start_btn, 220, 60);
    lv_obj_align(start_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(start_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(start_btn, 30, 0);
    lv_obj_add_event_cb(start_btn, start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "START");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(start_label, lv_color_hex(0x1a1305), 0);
    lv_obj_center(start_label);

    // Roller TIDAK memicu START; start hanya lewat tombol START

    return s_screen;
}
