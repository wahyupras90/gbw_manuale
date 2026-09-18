#include "ui_common.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>  // atof()
#include <math.h>

// ============================================================
// WIZARD KALIBRASI HX711 2-TITIK -- BARU. Menggantikan proses manual
// "timbang beberapa beban, kirim data, hitung scale, edit config.h,
// compile, OTA" (lihat riwayat lengkap di config.h,
// HX711_CALIBRATION_SCALE) dengan alur di layar, tersimpan ke NVS,
// TIDAK perlu compile ulang.
//
// ALUR 5 LANGKAH (state machine sederhana di SATU layar, konten
// berganti sesuai s_step -- BUKAN 5 screen LVGL terpisah, supaya
// tombol Back/Batal konsisten dan tidak perlu daftar create() banyak
// screen ke ui_screen_manager.cpp):
//   STEP_INTRO        -- penjelasan singkat + tombol MULAI
//   STEP_TARE         -- instruksi kosongkan timbangan + tombol LANJUT
//                         (blocking calibWizardTareEmpty() saat ditekan)
//   STEP_WEIGHT1_INPUT-- keypad, operator masukkan berat beban 1
//   STEP_WEIGHT1_PLACE-- instruksi taruh beban 1 + tombol BACA
//                         (blocking calibWizardReadPoint(1, ...))
//   STEP_WEIGHT2_INPUT-- keypad, berat beban 2 (HARUS beda dari beban 1)
//   STEP_WEIGHT2_PLACE-- instruksi ganti ke beban 2 + tombol BACA
//   STEP_RESULT       -- tampilkan scale lama/baru + residual kedua
//                         titik, tombol SIMPAN/BATAL
//
// Backend (main.cpp): calibWizardTareEmpty(), calibWizardReadPoint(),
// calibWizardComputeScale(), calibWizardScaleOld/New(),
// calibWizardResidual1/2(), calibWizardSave().
// ============================================================

typedef enum {
    STEP_INTRO,
    STEP_TARE,
    STEP_WEIGHT1_INPUT,
    STEP_WEIGHT1_PLACE,
    STEP_WEIGHT2_INPUT,
    STEP_WEIGHT2_PLACE,
    STEP_RESULT,
} calib_step_t;

extern void calibWizardTareEmpty();
extern void calibWizardReadPoint(int point, float weightGrams);
extern bool calibWizardComputeScale();
extern float calibWizardScaleOld();
extern float calibWizardScaleNew();
extern float calibWizardResidual1();
extern float calibWizardResidual2();
extern void calibWizardSave();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_content = nullptr;  // container isi, dibersihkan & diisi ulang tiap ganti step
static calib_step_t s_step = STEP_INTRO;

// Buffer keypad -- dipakai bersama STEP_WEIGHT1_INPUT & WEIGHT2_INPUT
// (direset tiap masuk step input baru, lihat render_weight_input()).
static char s_keypad_buf[10] = "";
static float s_weight1 = 0.0f;
static float s_weight2 = 0.0f;

static void render_step(void);  // forward decl -- dipanggil tiap tombol next/back

// ------------------------------------------------------------
// Helper -- bersihkan container isi sebelum render step baru.
// ------------------------------------------------------------
static void clear_content(void) {
    lv_obj_clean(s_content);
}

// ------------------------------------------------------------
// Helper tombol besar generik (dipakai di semua step untuk tombol
// aksi utama LANJUT/MULAI/BACA/SIMPAN, dan BATAL/KEMBALI).
// ------------------------------------------------------------
static lv_obj_t* create_action_btn(lv_obj_t* parent, const char* text, lv_color_t bg,
                                    lv_color_t textColor, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 220, 56);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, textColor, 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t* create_info_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, SCREEN_WIDTH - 48);
    return label;
}

// ============================================================
// STEP_INTRO
// ============================================================
static void intro_start_cb(lv_event_t* e) {
    s_step = STEP_TARE;
    render_step();
}

static void intro_cancel_cb(lv_event_t* e) {
    extern void ui_close_calibration_wizard(lv_event_t*);
    ui_close_calibration_wizard(e);
}

static void render_intro(void) {
    create_info_label(s_content,
        "Kalibrasi ulang timbangan HX711.\n\n"
        "Anda akan diminta menimbang 2 beban berbeda yang sudah "
        "diketahui beratnya (disarankan salah satu >100g untuk "
        "akurasi terbaik).\n\n"
        "Siapkan kedua beban dan timbangan referensi sebelum mulai.");
    lv_obj_t* spacer = lv_obj_create(s_content);
    lv_obj_set_size(spacer, 1, 12);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    create_action_btn(s_content, "MULAI", COLOR_ACCENT, lv_color_hex(0x1a1a1a), intro_start_cb);
    lv_obj_t* spacer2 = lv_obj_create(s_content);
    lv_obj_set_size(spacer2, 1, 8);
    lv_obj_set_style_bg_opa(spacer2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer2, 0, 0);
    create_action_btn(s_content, "BATAL", lv_color_hex(0x1a1a1a), COLOR_TEXT_SECONDARY, intro_cancel_cb);
}

// ============================================================
// STEP_TARE
// ============================================================
static void tare_confirm_cb(lv_event_t* e) {
    calibWizardTareEmpty();  // blocking ~1 detik
    s_step = STEP_WEIGHT1_INPUT;
    render_step();
}

static void render_tare(void) {
    create_info_label(s_content,
        "Kosongkan timbangan sepenuhnya (tidak ada portafilter/beban "
        "apa pun di atasnya).\n\nTekan LANJUT saat sudah kosong.");
    lv_obj_t* spacer = lv_obj_create(s_content);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    create_action_btn(s_content, "LANJUT", COLOR_ACCENT, lv_color_hex(0x1a1a1a), tare_confirm_cb);
}

// ============================================================
// STEP_WEIGHT1_INPUT / STEP_WEIGHT2_INPUT -- keypad numerik custom.
// Tombol besar (72x56), grid 3 kolom, cukup besar untuk disentuh di
// layar 280px (disepakati eksplisit dengan Wahyu -- keypad OS/kecil
// dianggap terlalu kecil, tapi +/- stepper terlalu lambat untuk angka
// 3 digit seperti berat referensi >100g).
// ============================================================
static lv_obj_t* s_keypad_display = nullptr;

static void update_keypad_display(void) {
    char buf[32];
    if (strlen(s_keypad_buf) == 0) {
        lv_label_set_text(s_keypad_display, "0");
        lv_obj_set_style_text_color(s_keypad_display, COLOR_TEXT_SECONDARY, 0);
    } else {
        snprintf(buf, sizeof(buf), "%s", s_keypad_buf);
        lv_label_set_text(s_keypad_display, buf);
        lv_obj_set_style_text_color(s_keypad_display, COLOR_TEXT_PRIMARY, 0);
    }
}

static void keypad_digit_cb(lv_event_t* e) {
    const char* digit = (const char*)lv_event_get_user_data(e);
    size_t len = strlen(s_keypad_buf);
    // Batas: 6 karakter (cukup untuk "999.99"), dan HANYA SATU titik
    // desimal -- cek sebelum menambahkan '.'.
    if (len >= 6) return;
    if (strcmp(digit, ".") == 0 && strchr(s_keypad_buf, '.') != nullptr) return;
    strncat(s_keypad_buf, digit, sizeof(s_keypad_buf) - len - 1);
    update_keypad_display();
}

static void keypad_backspace_cb(lv_event_t* e) {
    size_t len = strlen(s_keypad_buf);
    if (len > 0) s_keypad_buf[len - 1] = '\0';
    update_keypad_display();
}

static void weight1_next_cb(lv_event_t* e) {
    float w = atof(s_keypad_buf);
    if (w < 1.0f) return;  // guard -- berat terlalu kecil/kosong, abaikan tekan LANJUT
    s_weight1 = w;
    s_step = STEP_WEIGHT1_PLACE;
    render_step();
}

static void weight2_next_cb(lv_event_t* e) {
    float w = atof(s_keypad_buf);
    if (w < 1.0f) return;
    // Validasi: beban 2 HARUS beda cukup jauh dari beban 1 (minimal
    // 5g selisih) -- supaya perhitungan scale dari titik basis tidak
    // sensitif ke noise, dan residual verifikasi benar2 independen.
    if (fabsf(w - s_weight1) < 5.0f) {
        // TODO idealnya tampilkan pesan error di layar -- untuk
        // sekarang cukup abaikan tekan (operator akan sadar tombol
        // tidak merespons dan mengganti angka).
        return;
    }
    s_weight2 = w;
    s_step = STEP_WEIGHT2_PLACE;
    render_step();
}

static void render_weight_input(int pointNumber, lv_event_cb_t nextCb) {
    s_keypad_buf[0] = '\0';

    char title[32];
    snprintf(title, sizeof(title), "Berat beban %d (gram)", pointNumber);
    lv_obj_t* title_label = lv_label_create(s_content);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title_label, SCREEN_WIDTH - 48);

    s_keypad_display = lv_label_create(s_content);
    lv_label_set_text(s_keypad_display, "0");
    lv_obj_set_style_text_font(s_keypad_display, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_keypad_display, COLOR_TEXT_SECONDARY, 0);

    // Grid keypad 3 kolom x 4 baris: 1-2-3 / 4-5-6 / 7-8-9 / .-0-<-
    static const char* keys[12] = {"1","2","3","4","5","6","7","8","9",".","0","<"};
    lv_obj_t* grid = lv_obj_create(s_content);
    lv_obj_set_size(grid, SCREEN_WIDTH - 48, 4 * 52 + 3 * 6);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_style_pad_column(grid, 6, 0);

    for (int i = 0; i < 12; i++) {
        lv_obj_t* key = lv_btn_create(grid);
        lv_obj_set_size(key, (SCREEN_WIDTH - 48 - 12) / 3, 52);
        lv_obj_set_style_bg_color(key, COLOR_BG_CARD, 0);
        lv_obj_set_style_radius(key, 10, 0);
        lv_obj_set_style_border_width(key, 0, 0);
        lv_obj_t* label = lv_label_create(key);
        lv_label_set_text(label, keys[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_center(label);
        if (strcmp(keys[i], "<") == 0) {
            lv_obj_add_event_cb(key, keypad_backspace_cb, LV_EVENT_CLICKED, NULL);
        } else {
            lv_obj_add_event_cb(key, keypad_digit_cb, LV_EVENT_CLICKED, (void*)keys[i]);
        }
    }

    create_action_btn(s_content, "LANJUT", COLOR_ACCENT, lv_color_hex(0x1a1a1a), nextCb);
}

// ============================================================
// STEP_WEIGHT1_PLACE / STEP_WEIGHT2_PLACE
// ============================================================
static void place1_read_cb(lv_event_t* e) {
    calibWizardReadPoint(1, s_weight1);  // blocking ~1 detik
    s_step = STEP_WEIGHT2_INPUT;
    render_step();
}

static void place2_read_cb(lv_event_t* e) {
    calibWizardReadPoint(2, s_weight2);  // blocking ~1 detik
    bool ok = calibWizardComputeScale();
    if (!ok) {
        // Gagal hitung (berat basis <1g atau hasil scale tidak valid)
        // -- kembali ke intro dengan pesan, daripada tampilkan hasil
        // yang tidak masuk akal.
        s_step = STEP_INTRO;
        render_step();
        return;
    }
    s_step = STEP_RESULT;
    render_step();
}

static void render_weight_place(float weightGrams, lv_event_cb_t readCb) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Taruh beban %.2f g di timbangan.\n\nTekan BACA saat sudah stabil.", weightGrams);
    create_info_label(s_content, buf);
    lv_obj_t* spacer = lv_obj_create(s_content);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    create_action_btn(s_content, "BACA", COLOR_ACCENT, lv_color_hex(0x1a1a1a), readCb);
}

// ============================================================
// STEP_RESULT
// ============================================================
static void result_save_cb(lv_event_t* e) {
    calibWizardSave();
    extern void ui_close_calibration_wizard(lv_event_t*);
    ui_close_calibration_wizard(e);
}

static void result_discard_cb(lv_event_t* e) {
    extern void ui_close_calibration_wizard(lv_event_t*);
    ui_close_calibration_wizard(e);
}

static void render_result(void) {
    float scaleOld = calibWizardScaleOld();
    float scaleNew = calibWizardScaleNew();
    float resid1 = calibWizardResidual1();
    float resid2 = calibWizardResidual2();

    char buf[256];
    snprintf(buf, sizeof(buf),
        "Scale lama: %.2f\nScale baru: %.2f\n\n"
        "Verifikasi:\nBeban 1: error %+.2f g\nBeban 2: error %+.2f g",
        scaleOld, scaleNew, resid1, resid2);
    lv_obj_t* result_label = create_info_label(s_content, buf);

    // Peringatan kalau residual verifikasi cukup besar (>0.5g) --
    // ambang ini LONGGAR SENGAJA (bukan diseragamkan dengan
    // accuracy_tolerance_g grind yang jauh lebih ketat, biasanya
    // <0.2g) karena kalibrasi wizard ini bekerja di rentang berat
    // jauh lebih besar (100-300g+) daripada dosis grind (<20g) --
    // residual absolut sedikit lebih besar masih wajar di sana.
    // Tujuan ambang ini murni menangkap kasus SALAH TOTAL (beban
    // tertukar, keypad salah ketik, dsb), bukan presisi halus.
    bool residualBesar = (fabsf(resid1) > 0.5f) || (fabsf(resid2) > 0.5f);
    if (residualBesar) {
        lv_obj_t* warn = lv_label_create(s_content);
        lv_label_set_text(warn, "PERINGATAN: error verifikasi besar.\nPeriksa berat yang dimasukkan, lalu ulangi kalau perlu.");
        lv_obj_set_style_text_font(warn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(warn, COLOR_WARN, 0);
        lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(warn, SCREEN_WIDTH - 48);
    }
    (void)result_label;

    lv_obj_t* spacer = lv_obj_create(s_content);
    lv_obj_set_size(spacer, 1, 16);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    create_action_btn(s_content, "SIMPAN", COLOR_SUCCESS, lv_color_hex(0x1a1a1a), result_save_cb);
    lv_obj_t* spacer2 = lv_obj_create(s_content);
    lv_obj_set_size(spacer2, 1, 8);
    lv_obj_set_style_bg_opa(spacer2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer2, 0, 0);
    create_action_btn(s_content, "BATAL", lv_color_hex(0x1a1a1a), COLOR_TEXT_SECONDARY, result_discard_cb);
}

// ============================================================
// DISPATCH
// ============================================================
static void render_step(void) {
    clear_content();
    switch (s_step) {
        case STEP_INTRO:         render_intro(); break;
        case STEP_TARE:          render_tare(); break;
        case STEP_WEIGHT1_INPUT: render_weight_input(1, weight1_next_cb); break;
        case STEP_WEIGHT1_PLACE: render_weight_place(s_weight1, place1_read_cb); break;
        case STEP_WEIGHT2_INPUT: render_weight_input(2, weight2_next_cb); break;
        case STEP_WEIGHT2_PLACE: render_weight_place(s_weight2, place2_read_cb); break;
        case STEP_RESULT:        render_result(); break;
    }
}

lv_obj_t* ui_screen_calibration_wizard_create(void) {
    s_screen = lv_obj_create(NULL);
    ui_apply_screen_bg(s_screen);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_status_bar(s_screen, nullptr);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "KALIBRASI TIMBANGAN");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 14);

    s_content = lv_obj_create(s_screen);
    lv_obj_set_size(s_content, SCREEN_WIDTH, 456 - (STATUS_BAR_HEIGHT + 44) - 16);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 16, 0);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);

    // PENTING -- render step AWAL di sini juga. ui_screen_calibration_wizard_reset()
    // (dipanggil SEBELUM navigate_to() oleh ui_open_calibration_wizard(),
    // lihat ui_screen_manager.cpp) TIDAK BISA render_step() pada
    // KUNJUNGAN PERTAMA -- s_content masih nullptr saat itu (create()
    // ini belum sempat jalan). Tanpa baris ini, kunjungan pertama akan
    // menampilkan layar kosong (cuma judul, tanpa konten step INTRO)
    // sampai operator memicu ganti step secara tidak sengaja.
    render_step();

    return s_screen;
}

// Dipanggil dari ui_open_calibration_wizard() (ui_screen_manager.cpp)
// SETIAP KALI layar ini dibuka -- reset state wizard ke awal, supaya
// kunjungan berikutnya (BATAL lalu buka lagi, atau SIMPAN lalu
// kalibrasi ulang kedua kalinya) selalu mulai bersih dari STEP_INTRO,
// bukan melanjutkan state kunjungan sebelumnya.
void ui_screen_calibration_wizard_reset(void) {
    s_step = STEP_INTRO;
    s_keypad_buf[0] = '\0';
    s_weight1 = 0.0f;
    s_weight2 = 0.0f;
    if (s_content != nullptr) render_step();
}
