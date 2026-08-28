#include "ui_common.h"
#include "../../include/config.h"   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL DI KOMPUTER USER: PORTAFILTER_DETECT_THRESHOLD_G dipakai di bawah tanpa include ini -- file ini sebelumnya tidak butuh apa pun dari config.h
#include "../../include/grind_controller.h"   // AbortReason -- dipakai ui_show_grind_reject_reason() untuk debug cepat tanpa Serial (case tertutup fisik)
#include <cstdio>   // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: snprintf() dipakai di bawah tanpa include ini

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_arc = nullptr;
static lv_obj_t* s_weight_label = nullptr;
static lv_obj_t* s_target_sublabel = nullptr;
static lv_obj_t* s_phase_pill = nullptr;
static lv_obj_t* s_flow_stat = nullptr;
static lv_obj_t* s_latency_stat = nullptr;
static lv_obj_t* s_pulse_stat = nullptr;
static lv_obj_t* s_reject_label = nullptr;

extern void ui_open_settings(lv_event_t* e);
extern void ui_start_grind(lv_event_t* e);
extern void ui_enable_swipe_home(lv_obj_t* screen);  // swipe kanan -> Set Target (Home)

static void start_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_start_grind(e);
    }
}

// Helper bikin satu kolom stat (value besar + label kecil di bawah),
// sesuai mockup .stat-item.
static lv_obj_t* create_stat_item(lv_obj_t* parent, const char* label_text) {
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, 76, 36);  // KOREKSI: 60px kepotong untuk label panjang (LATENCY MS dst), lihat catatan lengkap di screen_predictive_grind.cpp
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* value = lv_label_create(col);
    lv_label_set_text(value, "--");  // KOREKSI: em dash (U+2014) kotak kosong di physical display, ganti ASCII
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(value, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* label = lv_label_create(col);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);

    return col;  // caller ambil child 0 (value label) untuk update
}

lv_obj_t* ui_screen_idle_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, ui_open_settings);

    // Title (permintaan eksplisit -- "seragamkan dengan yang lain").
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "IDLE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 18);

    // Ring besar di tengah, 0% saat idle
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

    // Label berat + target di tengah ring
    s_weight_label = ui_create_weight_label(s_arc);
    lv_obj_align(s_weight_label, LV_ALIGN_CENTER, 0, -10);

    s_target_sublabel = lv_label_create(s_arc);
    lv_obj_set_style_text_font(s_target_sublabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_target_sublabel, COLOR_ACCENT_DIM, 0);
    // KOREKSI (permintaan eksplisit -- "Target 18g tidak center"):
    // SEBELUMNYA di-align relatif ke s_weight_label
    // (LV_ALIGN_OUT_BOTTOM_MID) -- secara teori LVGL menghitung titik
    // tengah horizontal widget acuan dengan benar, tapi lebar
    // s_weight_label berubah-ubah tergantung teks (mis. "0.00g" vs
    // "128.45g", lebar karakter font Montserrat tidak seragam per
    // digit), jadi titik tengah yang dipakai bisa sedikit bergeser
    // dari pusat visual ring. KOREKSI: align langsung ke s_arc
    // (parent, ukuran TETAP 184x184, tidak pernah berubah), dengan
    // offset Y manual dari titik tengah arc supaya posisinya stabil
    // terlepas dari lebar teks weight_label di atasnya.
    lv_obj_align(s_target_sublabel, LV_ALIGN_CENTER, 0, 26);
    lv_label_set_text(s_target_sublabel, "TARGET 18.0g");

    // Phase pill "Ready"
    // Digeser turun (+14 -> +40) mengikuti ring.
    s_phase_pill = ui_create_phase_label(s_screen);
    lv_obj_align(s_phase_pill, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 40 + 184 + 14);
    ui_set_phase_label(s_phase_pill, "READY", COLOR_TEXT_SECONDARY, lv_color_hex(0x2a2a2a));

    // Stats row: Flow / Latency / Pulses (semua placeholder "—" saat idle)
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
    lv_label_set_text(lv_obj_get_child(s_pulse_stat, 0), "0/0");  // diisi angka asli di ui_screen_idle_update()

    // Label alasan tolak grind -- DEBUG CEPAT (permintaan eksplisit
    // Wahyu, tanpa Serial/USB karena case sudah tertutup fisik).
    // Kosong secara default, diisi ui_show_grind_reject_reason() SEKALI
    // tiap kali grind_start() gagal (lihat ui_start_grind() di
    // ui_screen_manager.cpp). SENGAJA TIDAK dikosongkan otomatis di
    // ui_screen_idle_update() (dipanggil terus-menerus tiap update
    // berat baru) -- kalau begitu, pesan akan hilang dalam sepersekian
    // detik, operator tidak sempat baca. Dikosongkan HANYA di
    // ui_clear_grind_reject_reason() (ui_screen_manager.cpp), dipanggil
    // ui_start_grind() SEBELUM setiap percobaan Start baru -- supaya
    // pesan lama tidak nyangkut kalau percobaan berikutnya berhasil
    // atau gagal dengan alasan berbeda. SENGAJA diletakkan di celah
    // 30px antara stats_row & start_btn (dihitung eksplisit lewat
    // kalkulasi Python: ring+pill+stats_row bottom ~338px, start_btn
    // top ~368px), font 12px supaya muat.
    s_reject_label = lv_label_create(s_screen);
    lv_label_set_text(s_reject_label, "");
    lv_obj_set_style_text_font(s_reject_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_reject_label, COLOR_WARN, 0);
    lv_obj_set_width(s_reject_label, SCREEN_WIDTH - 32);
    lv_obj_set_style_text_align(s_reject_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_reject_label, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(s_reject_label, stats_row, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    // Tombol Start di bawah
    lv_obj_t* start_btn = lv_btn_create(s_screen);
    // STANDARISASI (permintaan eksplisit -- semua tombol bawah harus
    // identik ukuran & posisi Y): 220x60, offset -28 dari bawah -- ini
    // jadi SPEC TUNGGAL yang dipakai di SEMUA layar (screen_idle.cpp,
    // screen_predictive_grind.cpp, screen_pulse_correction.cpp,
    // screen_set_target.cpp, screen_done.cpp). Kalau ukuran ini
    // diubah lagi, ubah di KELIMA file sekaligus supaya tetap konsisten.
    lv_obj_set_size(start_btn, 220, 60);
    lv_obj_align(start_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(start_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(start_btn, 30, 0);
    lv_obj_add_event_cb(start_btn, start_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* start_label = lv_label_create(start_btn);
    // KOREKSI (ditemukan lewat foto hardware fisik yang sedang jalan):
    // karakter Unicode U+25B6 (BLACK RIGHT-POINTING TRIANGLE) tampil
    // sebagai kotak kosong di physical display -- sama akar masalah
    // dengan LV_SYMBOL_BLUETOOTH di ui_common.h (lihat catatan di
    // sana): bitmap font Montserrat yang di-generate LVGL tidak
    // menyertakan SEMUA kemungkinan karakter Unicode, cuma ASCII +
    // subset LV_SYMBOL_* tertentu. Diganti ke ">" (ASCII biasa,
    // dijamin didukung font manapun) alih-alih simbol segitiga.
    lv_label_set_text(start_label, "> START");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);  // DIPERBESAR seiring start_btn
    lv_obj_set_style_text_color(start_label, lv_color_hex(0x1a1305), 0);
    lv_obj_center(start_label);

    ui_enable_swipe_home(s_screen);  // layar aman -- swipe kanan boleh aktif (lihat catatan lengkap di ui_screen_manager.cpp)

    return s_screen;
}

// Dipanggil dari ui_start_grind() (ui_screen_manager.cpp) SETIAP kali
// grind_start() gagal -- tampilkan alasan tolak singkat di layar Idle,
// TANPA perlu Serial/USB (debug cepat, case sudah tertutup fisik).
// Mapping AbortReason -> teks: hanya dua alasan yang REALISTIS terjadi
// tepat setelah tombol Start ditekan (INVALID_WEIGHT/UNSTABLE_WEIGHT,
// lihat startGrind() di grind_controller.cpp) -- alasan lain
// (HARD_OVERSHOOT/STALL/TIMEOUT/MOTOR_*_FAILED) hanya terjadi SETELAH
// grind sudah berjalan, jadi ditampilkan generik kalau somehow muncul
// di sini (seharusnya tidak pernah, tapi jangan diam-diam salah tampil
// kalau asumsi ini ternyata keliru).
void ui_show_grind_reject_reason(AbortReason reason) {
    if (s_reject_label == nullptr) return;
    switch (reason) {
        case AbortReason::INVALID_WEIGHT:
            lv_label_set_text(s_reject_label, "Timbangan belum baca berat -- cek load cell/HX711 tersambung");
            break;
        case AbortReason::UNSTABLE_WEIGHT:
            lv_label_set_text(s_reject_label, "Berat belum stabil -- tunggu beberapa detik, coba lagi");
            break;
        default:
            // Seharusnya TIDAK PERNAH terjadi tepat setelah Start ditekan
            // dari Idle (lihat komentar di ui_start_grind()) -- fallback
            // aman kalau asumsi itu ternyata keliru. TIDAK menyebut
            // Serial secara spesifik (mungkin tidak bisa diakses, lihat
            // kasus Wahyu: power dari buck 5V, USB tidak tersambung).
            lv_label_set_text(s_reject_label, "Grind ditolak -- coba lagi, atau restart kalau berulang");
            break;
    }
}

// Dipanggil ui_start_grind() SEBELUM grind_start() -- bersihkan pesan
// tolak SEBELUMNYA (kalau ada) tiap kali operator mencoba lagi, supaya
// pesan lama tidak nyangkut kalau percobaan baru ini berhasil atau
// gagal dengan alasan berbeda.
void ui_clear_grind_reject_reason(void) {
    if (s_reject_label == nullptr) return;
    lv_label_set_text(s_reject_label, "");
}

// Dipanggil dari main loop tiap ada update berat baru, supaya label &
// ring tetap sinkron dengan g_ui_state.
void ui_screen_idle_update(void) {
    if (s_weight_label == nullptr) return;

    // Layar Idle SELALU tampilkan 0.00g -- ini BUKAN dose (belum ada
    // sesi grind aktif, start_weight_g dari sesi sebelumnya tidak
    // relevan di sini), murni status "siap, menunggu mulai". Indikator
    // portafilter terpasang/tidak ditunjukkan lewat WARNA label (bukan
    // angka), berdasarkan current_weight_g ABSOLUT dibanding
    // PORTAFILTER_DETECT_THRESHOLD_G -- lihat catatan di config.h.
    // Ini murni kosmetik, TIDAK memengaruhi validasi startGrind() sama
    // sekali (itu tetap dari GrindController, independen dari sini).
    lv_label_set_text(s_weight_label, "0.00g");
    bool portafilterDetected = g_ui_state.current_weight_g >= PORTAFILTER_DETECT_THRESHOLD_G;
    lv_obj_set_style_text_color(s_weight_label, portafilterDetected ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY, 0);

    char buf[24];
    snprintf(buf, sizeof(buf), "TARGET %.1fg", g_ui_state.target_weight_g);
    lv_label_set_text(s_target_sublabel, buf);

    snprintf(buf, sizeof(buf), "0/%d", g_ui_state.max_pulse_attempts);
    lv_label_set_text(lv_obj_get_child(s_pulse_stat, 0), buf);

    // Ring tetap 0% di Idle -- tidak merefleksikan berat, cuma indikator status siap
}
