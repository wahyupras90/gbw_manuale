#include "ui_common.h"
#include "../../include/config.h"
#include <cstdio>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: snprintf() dipakai di bawah tanpa include ini
#include <math.h>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: fabsf()/isnan() dipakai di bawah -- WAJIB <math.h> (C-style, taruh di global namespace), BUKAN <cmath> (C++-style, taruh di std:: -- itu yang menyebabkan error kedua "isnan tidak dideklarasikan di scope ini, maksudnya std::isnan?"). Konsisten dengan grind_controller.cpp yang juga pakai <math.h>.

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_arc = nullptr;
static lv_obj_t* s_weight_label = nullptr;
static lv_obj_t* s_status_sublabel = nullptr;
static lv_obj_t* s_phase_pill = nullptr;
static lv_obj_t* s_flow_stat = nullptr;
static lv_obj_t* s_pulse_stat = nullptr;
static lv_obj_t* s_duration_stat = nullptr;

extern void ui_open_settings(lv_event_t* e);
extern void ui_new_grind(lv_event_t* e);
extern void ui_enable_swipe_home(lv_obj_t* screen);  // swipe kanan -> Set Target (Home)

static void new_grind_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_new_grind(e);
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

lv_obj_t* ui_screen_done_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, ui_open_settings);

    // Title (permintaan eksplisit -- "seragamkan dengan yang lain").
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "DONE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    // Ring penuh, hijau -- sesuai mockup step Done (ring-fill.done).
    // Digeser turun (+14 -> +40) untuk memberi ruang ke title di atas.
    s_arc = lv_arc_create(s_screen);
    lv_obj_set_size(s_arc, 184, 184);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 100);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 40);
    lv_obj_set_style_arc_color(s_arc, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, COLOR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);

    s_weight_label = ui_create_weight_label(s_arc);
    lv_obj_align(s_weight_label, LV_ALIGN_CENTER, 0, -10);

    // Sublabel error + status (mis. "±0.02g OK") -- sesuai mockup
    // .weight-target di step Done.
    s_status_sublabel = lv_label_create(s_arc);
    lv_obj_set_style_text_font(s_status_sublabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status_sublabel, COLOR_SUCCESS, 0);
    // KOREKSI (bug sama persis dengan screen_idle.cpp -- ditemukan
    // lewat foto hardware fisik, label ini terlihat tidak center):
    // align relatif ke s_weight_label (LV_ALIGN_OUT_BOTTOM_MID) bisa
    // bergeser dari pusat visual ring karena lebar s_weight_label
    // berubah-ubah tergantung teks ("0.00g" vs "128.45g"). KOREKSI:
    // align langsung ke s_arc (parent, ukuran TETAP 184x184).
    lv_obj_align(s_status_sublabel, LV_ALIGN_CENTER, 0, 26);
    // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: "\xC2\xB1"
    // (dimaksudkan sebagai 2 byte UTF-8 terpisah untuk karakter '±')
    // ternyata dibaca compiler sebagai SATU hex escape sequence
    // panjang (C++ hex escape terus "melahap" digit hex berikutnya
    // tanpa batas), menghasilkan nilai di luar rentang char dan
    // warning "hex escape sequence out of range" + isi string yang
    // salah. Diperbaiki dengan literal UTF-8 langsung -- lebih
    // sederhana dan benar, karena LV_TXT_ENC_UTF8 sudah diaktifkan di
    // lv_conf.h (lihat catatan di file itu).
    lv_label_set_text(s_status_sublabel, "+/-0.00g OK");  // KOREKSI: '±' kotak kosong di hardware fisik

    s_phase_pill = ui_create_phase_label(s_screen);
    // Digeser turun (+14 -> +40) mengikuti ring.
    lv_obj_align(s_phase_pill, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 40 + 184 + 14);
    ui_set_phase_label(s_phase_pill, "OK COMPLETE", COLOR_SUCCESS, lv_color_hex(0x1a2a17));  // KOREKSI: '✓' kotak kosong di hardware fisik

    lv_obj_t* stats_row = lv_obj_create(s_screen);
    lv_obj_set_size(stats_row, SCREEN_WIDTH - 40, 36);
    lv_obj_align_to(stats_row, s_phase_pill, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

    // 3 stat sesuai mockup step Done: Flow / Pulses / Duration
    // (skeleton lama cuma Duration & Pulses -- ditambah Flow di sini).
    s_flow_stat = create_stat_item(stats_row, "FLOW G/S");
    s_pulse_stat = create_stat_item(stats_row, "PULSES");
    s_duration_stat = create_stat_item(stats_row, "DURATION");

    // Tombol New Grind
    // STANDARISASI (permintaan eksplisit -- lihat catatan lengkap di
    // screen_idle.cpp): 220x60, offset -28, font label diperbesar juga.
    lv_obj_t* new_grind_btn = lv_btn_create(s_screen);
    lv_obj_set_size(new_grind_btn, 220, 60);
    lv_obj_align(new_grind_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(new_grind_btn, lv_color_hex(0x1a2a17), 0);
    lv_obj_set_style_border_width(new_grind_btn, 1, 0);
    lv_obj_set_style_border_color(new_grind_btn, COLOR_SUCCESS, 0);
    lv_obj_set_style_radius(new_grind_btn, 30, 0);
    lv_obj_add_event_cb(new_grind_btn, new_grind_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* new_grind_label = lv_label_create(new_grind_btn);
    lv_label_set_text(new_grind_label, "NEW GRIND");
    lv_obj_set_style_text_font(new_grind_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(new_grind_label, COLOR_SUCCESS, 0);
    lv_obj_center(new_grind_label);

    ui_enable_swipe_home(s_screen);  // layar aman -- swipe kanan boleh aktif

    return s_screen;
}

// Dipanggil sekali saat masuk ke screen Done (bukan tiap loop, karena
// grind sudah selesai dan nilai-nilai final tidak berubah lagi).
void ui_screen_done_update(void) {
    if (s_weight_label == nullptr) return;

    char buf[24];
    // Angka besar = DOSE FINAL (kopi yang benar-benar masuk), sama
    // seperti layar Predictive Grind/Pulse Correction. Perhitungan
    // error/tolerance di bawah TETAP pakai current_weight_g ABSOLUT
    // vs target_absolute_g -- itu benar apa adanya dan TIDAK diubah,
    // karena akurasi grind dievaluasi di level absolut yang sama
    // seperti GrindController, bukan di level dose.
    float doseFinal = g_ui_state.current_weight_g - g_ui_state.start_weight_g;
    snprintf(buf, sizeof(buf), "%.2fg", doseFinal);
    lv_label_set_text(s_weight_label, buf);

    float error = g_ui_state.current_weight_g - g_ui_state.target_absolute_g;
    bool within_tolerance = fabsf(error) <= g_ui_state.accuracy_tolerance_g;

    snprintf(buf, sizeof(buf), "+/-%.2fg %s", fabsf(error), within_tolerance ? "OK" : "OUT");  // KOREKSI: '±' (U+00B1) kotak kosong di hardware fisik
    lv_label_set_text(s_status_sublabel, buf);
    lv_obj_set_style_text_color(s_status_sublabel, within_tolerance ? COLOR_SUCCESS : COLOR_WARN, 0);

    lv_obj_set_style_arc_color(s_arc, within_tolerance ? COLOR_SUCCESS : COLOR_WARN, LV_PART_INDICATOR);
    ui_set_phase_label(s_phase_pill,
                        within_tolerance ? "OK COMPLETE" : "!! OUT OF TOLERANCE",  // KOREKSI: '✓'/'⚠' (U+2713/U+26A0) kotak kosong di hardware fisik
                        within_tolerance ? COLOR_SUCCESS : COLOR_WARN,
                        within_tolerance ? lv_color_hex(0x1a2a17) : lv_color_hex(0x2a1c17));

    if (!isnan(g_ui_state.flow_rate_gps)) {
        snprintf(buf, sizeof(buf), "%.2f", g_ui_state.flow_rate_gps);
    } else {
        snprintf(buf, sizeof(buf), "--");  // KOREKSI: em dash kotak kosong di hardware fisik
    }
    lv_label_set_text(lv_obj_get_child(s_flow_stat, 0), buf);

    snprintf(buf, sizeof(buf), "%d", g_ui_state.pulse_count);
    lv_label_set_text(lv_obj_get_child(s_pulse_stat, 0), buf);

    snprintf(buf, sizeof(buf), "%.1fs", g_ui_state.grind_duration_ms / 1000.0f);
    lv_label_set_text(lv_obj_get_child(s_duration_stat, 0), buf);
}
