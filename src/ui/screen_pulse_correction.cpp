#include "ui_common.h"
#include <cstdio>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: snprintf() dipakai di bawah tanpa include ini
#include <math.h>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: isnan() dipakai di bawah -- WAJIB <math.h> (C-style, global namespace), BUKAN <cmath> (taruh di std::)

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_arc = nullptr;
static lv_obj_t* s_weight_label = nullptr;
static lv_obj_t* s_pulse_sublabel = nullptr;
static lv_obj_t* s_phase_pill = nullptr;
static lv_obj_t* s_flow_stat = nullptr;
static lv_obj_t* s_error_stat = nullptr;
static lv_obj_t* s_pulse_stat = nullptr;

extern void ui_open_settings(lv_event_t* e);

// Sama seperti screen_predictive_grind.cpp -- lihat komentar di sana
// untuk alasan lengkap kenapa tombol Stop di sini memanggil
// grind_force_abort() (bukan ui_new_grind() langsung), dan kenapa
// navigasi ke Done ditangani main.cpp, bukan di sini.
extern void grind_force_abort(void);

static void stop_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        grind_force_abort();
    }
}

static lv_obj_t* create_stat_item(lv_obj_t* parent, const char* label_text) {
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, 76, 36);  // KOREKSI: 60px kepotong untuk label panjang (LATENCY MS dst), lihat catatan lengkap di screen_predictive_grind.cpp
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* value = lv_label_create(col);
    lv_label_set_text(value, "--");  // KOREKSI: em dash kotak kosong di hardware fisik
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(value, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* label = lv_label_create(col);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);

    return col;
}

lv_obj_t* ui_screen_pulse_correction_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, ui_open_settings);

    // Title (permintaan eksplisit -- "seragamkan dengan yang lain").
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "PULSE CORRECTION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    // Ring hijau selama fase pulse correction.
    // Digeser turun (+14 -> +40) untuk memberi ruang ke title di atas.
    s_arc = lv_arc_create(s_screen);
    lv_obj_set_size(s_arc, 184, 184);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 97);  // sudah dekat target saat masuk fase ini
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 40);
    lv_obj_set_style_arc_color(s_arc, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, COLOR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);

    s_weight_label = ui_create_weight_label(s_arc);
    lv_obj_align(s_weight_label, LV_ALIGN_CENTER, 0, -10);

    s_pulse_sublabel = lv_label_create(s_arc);
    lv_obj_set_style_text_font(s_pulse_sublabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_pulse_sublabel, COLOR_ACCENT_DIM, 0);
    // KOREKSI (bug sama dengan screen_idle.cpp/screen_done.cpp/
    // screen_predictive_grind.cpp -- lihat catatan lengkap di sana):
    // align ke s_arc, bukan relatif ke s_weight_label.
    lv_obj_align(s_pulse_sublabel, LV_ALIGN_CENTER, 0, 26);
    lv_label_set_text(s_pulse_sublabel, "PULSE 0/10");

    s_phase_pill = ui_create_phase_label(s_screen);
    // Digeser turun (+14 -> +40) mengikuti ring.
    lv_obj_align(s_phase_pill, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 40 + 184 + 14);
    ui_set_phase_label(s_phase_pill, "FINE TUNING", COLOR_SUCCESS, lv_color_hex(0x1a2a17));

    lv_obj_t* stats_row = lv_obj_create(s_screen);
    lv_obj_set_size(stats_row, SCREEN_WIDTH - 40, 36);
    lv_obj_align_to(stats_row, s_phase_pill, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

    // 3 stat sesuai mockup: Flow / Error / Pulses (skeleton lama cuma
    // punya 2 -- Pulses & Error -- ditambah Flow di sini).
    s_flow_stat = create_stat_item(stats_row, "FLOW G/S");
    s_error_stat = create_stat_item(stats_row, "ERROR G");
    s_pulse_stat = create_stat_item(stats_row, "PULSES");

    // Tombol Stop
    lv_obj_t* stop_btn = lv_btn_create(s_screen);
    // STANDARISASI (lihat catatan lengkap di screen_idle.cpp) --
    // 220x60, offset -28, SPEC TUNGGAL untuk semua tombol bawah.
    lv_obj_set_size(stop_btn, 220, 60);
    lv_obj_align(stop_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0x2a1c17), 0);
    lv_obj_set_style_border_width(stop_btn, 1, 0);
    lv_obj_set_style_border_color(stop_btn, COLOR_WARN, 0);
    lv_obj_set_style_radius(stop_btn, 30, 0);
    lv_obj_add_event_cb(stop_btn, stop_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* stop_label = lv_label_create(stop_btn);
    // KOREKSI: sama seperti screen_predictive_grind.cpp -- lihat
    // catatan lengkap di sana dan di ui_common.h.
    lv_label_set_text(stop_label, "X STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);  // DIPERBESAR seiring stop_btn
    lv_obj_set_style_text_color(stop_label, COLOR_WARN, 0);
    lv_obj_center(stop_label);

    return s_screen;
}

void ui_screen_pulse_correction_update(void) {
    if (s_weight_label == nullptr) return;

    char buf[24];
    // Angka besar = DOSE, sama seperti screen_predictive_grind.cpp --
    // lihat komentar di sana untuk alasan lengkap.
    float doseNow = g_ui_state.current_weight_g - g_ui_state.start_weight_g;
    snprintf(buf, sizeof(buf), "%.2fg", doseNow);
    lv_label_set_text(s_weight_label, buf);

    snprintf(buf, sizeof(buf), "PULSE %d/%d", g_ui_state.pulse_count, g_ui_state.max_pulse_attempts);
    lv_label_set_text(s_pulse_sublabel, buf);

    if (!isnan(g_ui_state.flow_rate_gps)) {
        snprintf(buf, sizeof(buf), "%.2f", g_ui_state.flow_rate_gps);
    } else {
        // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: sama seperti
        // di screen_done.cpp, "—" (dimaksudkan 3 byte UTF-8
        // untuk em-dash '\u2014') dibaca compiler sebagai satu hex
        // escape sequence panjang, bukan 3 byte terpisah. Diganti
        // literal UTF-8 langsung.
        snprintf(buf, sizeof(buf), "\u2014");
    }
    lv_label_set_text(lv_obj_get_child(s_flow_stat, 0), buf);

    snprintf(buf, sizeof(buf), "%+.2f", g_ui_state.pulse_error_g);
    lv_label_set_text(lv_obj_get_child(s_error_stat, 0), buf);

    snprintf(buf, sizeof(buf), "%d/%d", g_ui_state.pulse_count, g_ui_state.max_pulse_attempts);
    lv_label_set_text(lv_obj_get_child(s_pulse_stat, 0), buf);
}
