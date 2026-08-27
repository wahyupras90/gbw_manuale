#include "ui_common.h"
#include <cstdio>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: snprintf() dipakai di bawah tanpa include ini
#include <math.h>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: fabsf()/isnan() dipakai di bawah -- WAJIB <math.h> (C-style, global namespace), BUKAN <cmath> (taruh di std::)
#include <Arduino.h>  // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: Serial dipakai di stop_btn_cb() (diagnostik sementara) tanpa include ini

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_arc = nullptr;
static lv_obj_t* s_weight_label = nullptr;
static lv_obj_t* s_target_sublabel = nullptr;
static lv_obj_t* s_phase_pill = nullptr;
static lv_obj_t* s_flow_stat = nullptr;
static lv_obj_t* s_latency_stat = nullptr;
static lv_obj_t* s_pulse_stat = nullptr;

extern void ui_open_settings(lv_event_t* e);

// grind_force_abort() -- didefinisikan di main.cpp, memanggil
// GrindController::forceAbort() yang sesungguhnya (motor OFF dengan
// retry/safety penuh, lihat grind_controller.h). TIDAK langsung
// navigate ke screen lain di sini -- navigasi ke Done ditangani
// main.cpp lewat ui_transition_to_done() begitu GrindController
// transisi ke state ABORT (lihat handleGrindStateTransitionForUi()),
// supaya screen Done tetap menampilkan angka final yang benar
// (berat/durasi saat abort), bukan langsung lompat ke Set Target
// tanpa operator sempat lihat hasilnya.
extern void grind_force_abort(void);

static void stop_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        grind_force_abort();
    }
}

static lv_obj_t* create_stat_item(lv_obj_t* parent, const char* label_text) {
    lv_obj_t* col = lv_obj_create(parent);
    // KOREKSI (dilaporkan lewat foto hardware fisik -- "LATENCY MS"
    // terpotong jadi "ATENCY M"): lebar kolom SEBELUMNYA 60px, teks
    // "LATENCY MS" di font 12px lebih lebar dari itu dan col ini
    // LV_OBJ_FLAG_SCROLLABLE di-clear (tidak bisa scroll/wrap), jadi
    // kepotong begitu saja tanpa indikasi visual apa pun. KOREKSI:
    // lebar dinaikkan ke 76px -- stats_row (parent) lebar 240px dengan
    // 3 kolom flex SPACE_EVENLY, 76*3=228px masih muat nyaman dengan
    // gap di antaranya.
    lv_obj_set_size(col, 76, 36);
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

lv_obj_t* ui_screen_predictive_grind_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, ui_open_settings);

    // Title (permintaan eksplisit -- "seragamkan dengan yang lain",
    // konsisten dengan "TARGET WEIGHT" di screen_set_target.cpp).
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "GRINDING");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    // Ring warna accent (brass) selama fase predictive grind.
    // Digeser turun (+14 -> +40) untuk memberi ruang ke title di atas.
    s_arc = lv_arc_create(s_screen);
    lv_obj_set_size(s_arc, 184, 184);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 0);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 40);
    lv_obj_set_style_arc_color(s_arc, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);

    s_weight_label = ui_create_weight_label(s_arc);
    lv_obj_align(s_weight_label, LV_ALIGN_CENTER, 0, -10);

    s_target_sublabel = lv_label_create(s_arc);
    lv_obj_set_style_text_font(s_target_sublabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_target_sublabel, COLOR_ACCENT_DIM, 0);
    // KOREKSI (bug sama dengan screen_idle.cpp/screen_done.cpp --
    // lihat catatan lengkap di sana): align ke s_arc, bukan relatif
    // ke s_weight_label yang lebarnya berubah-ubah.
    lv_obj_align(s_target_sublabel, LV_ALIGN_CENTER, 0, 26);
    lv_label_set_text(s_target_sublabel, "TARGET 18.0g");

    // Phase pill -- default "Detecting Flow" saat masuk screen ini
    // (state WAIT_FLOW_START di GrindController) -- lihat catatan
    // ui_screen_predictive_grind_update() untuk transisi ke "Grinding"
    // begitu flow_start_confirmed true. Per keputusan terkini, TIDAK
    // ADA screen/layar terpisah untuk WAIT_FLOW_START -- ring tetap
    // di screen yang sama, cuma diam di 0% dan label pill yang beda.
    s_phase_pill = ui_create_phase_label(s_screen);
    // Digeser turun (+14 -> +40) mengikuti ring yang sudah digeser
    // untuk title "GRINDING" di atasnya.
    lv_obj_align(s_phase_pill, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 40 + 184 + 14);
    ui_set_phase_label(s_phase_pill, "DETECTING FLOW", COLOR_ACCENT, lv_color_hex(0x2a2412));

    lv_obj_t* stats_row = lv_obj_create(s_screen);
    lv_obj_set_size(stats_row, SCREEN_WIDTH - 40, 36);
    lv_obj_align_to(stats_row, s_phase_pill, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

    s_flow_stat = create_stat_item(stats_row, "FLOW G/S");
    s_latency_stat = create_stat_item(stats_row, "LATENCY MS");
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
    // KOREKSI (akar masalah sama dengan LV_SYMBOL_BLUETOOTH/U+25B6 --
    // lihat catatan lengkap di ui_common.h dan screen_idle.cpp):
    // U+25A0 (BLACK SQUARE) kemungkinan besar juga tidak ada di bitmap
    // font Montserrat yang di-generate, sama pola dengan simbol Play
    // yang sudah dikonfirmasi kotak kosong di physical display.
    lv_label_set_text(stop_label, "X STOP");  // X STOP
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);  // DIPERBESAR seiring stop_btn
    lv_obj_set_style_text_color(stop_label, COLOR_WARN, 0);
    lv_obj_center(stop_label);

    return s_screen;
}

// Dipanggil dari main loop tiap ada update berat/flow baru.
void ui_screen_predictive_grind_update(void) {
    if (s_weight_label == nullptr) return;

    char buf[24];
    // Angka besar = DOSE (kopi yang sudah masuk sejak grind mulai),
    // BUKAN berat absolut timbangan -- current_weight_g dikurangi
    // start_weight_g (berat portafilter/wadah yang tercatat saat
    // startGrind() dipanggil). Permintaan eksplisit: operator mau
    // lihat berat kopi, bukan berat total portafilter+kopi.
    float doseNow = g_ui_state.current_weight_g - g_ui_state.start_weight_g;
    snprintf(buf, sizeof(buf), "%.2fg", doseNow);
    lv_label_set_text(s_weight_label, buf);

    snprintf(buf, sizeof(buf), "TARGET %.1fg", g_ui_state.target_weight_g);
    lv_label_set_text(s_target_sublabel, buf);

    // ------------------------------------------------------------
    // WAIT_FLOW_START vs GRINDING -- per keputusan: TIDAK ADA screen
    // terpisah, cukup ring diam di 0% dan phase pill beda label,
    // sampai g_ui_state.flow_start_confirmed true (di-set main.cpp
    // dari GrindController::flowStartConfirmed()).
    // ------------------------------------------------------------
    if (!g_ui_state.flow_start_confirmed) {
        lv_arc_set_value(s_arc, 0);
        ui_set_phase_label(s_phase_pill, "DETECTING FLOW", COLOR_ACCENT, lv_color_hex(0x2a2412));
        lv_label_set_text(lv_obj_get_child(s_flow_stat, 0), "--");
        lv_label_set_text(lv_obj_get_child(s_latency_stat, 0), "--");
    } else {
        // Ring progress = dose SUDAH TERCAPAI / dose TOTAL yang
        // diminta, BUKAN current_weight_g / target_weight_g (BUG
        // LAMA -- itu salah kalau berat awal timbangan bukan 0, mis.
        // portafilter yang sudah ditara tetap punya berat > 0 di
        // pembacaan mentah timbangan sebelum tare berlaku, ATAU kalau
        // target_weight_g adalah dose bukan berat absolut. Progress
        // yang benar: seberapa jauh KENAIKAN berat dari titik mulai
        // (start_weight_g) menuju total kenaikan yang diminta
        // (target_absolute_g - start_weight_g), yang secara matematis
        // sama dengan dose yang sudah masuk / dose total diminta.
        float doseTotal = g_ui_state.target_absolute_g - g_ui_state.start_weight_g;
        float doseSoFar = g_ui_state.current_weight_g - g_ui_state.start_weight_g;
        int pct = (doseTotal > 0.0f) ? (int)((doseSoFar / doseTotal) * 100.0f) : 0;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_arc_set_value(s_arc, pct);

        ui_set_phase_label(s_phase_pill, "GRINDING", COLOR_ACCENT, lv_color_hex(0x2a2412));

        if (!isnan(g_ui_state.flow_rate_gps)) {
            snprintf(buf, sizeof(buf), "%.2f", g_ui_state.flow_rate_gps);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(lv_obj_get_child(s_flow_stat, 0), buf);

        snprintf(buf, sizeof(buf), "%lu", g_ui_state.grind_latency_ms);
        lv_label_set_text(lv_obj_get_child(s_latency_stat, 0), buf);
    }

    snprintf(buf, sizeof(buf), "%d/%d", g_ui_state.pulse_count, g_ui_state.max_pulse_attempts);
    lv_label_set_text(lv_obj_get_child(s_pulse_stat, 0), buf);
}
