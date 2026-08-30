#include "ui_common.h"
#include "../../include/config.h"
#include "../../include/grind_controller.h"   // AbortReason -- lihat ui_show_grind_reject_reason() di screen_idle.cpp
#include <math.h>   // NAN -- default motor_stop_target_weight_g di g_ui_state

// Deklarasi create function tiap screen
extern lv_obj_t* ui_screen_set_target_create(void);
extern lv_obj_t* ui_screen_idle_create(void);
extern lv_obj_t* ui_screen_predictive_grind_create(void);
extern lv_obj_t* ui_screen_pulse_correction_create(void);
extern lv_obj_t* ui_screen_done_create(void);
extern lv_obj_t* ui_screen_settings_create(void);
extern lv_obj_t* ui_screen_manual_grind_create(void);
extern void ui_screen_manual_grind_update(void);
extern lv_obj_t* ui_screen_debug_create(void);
extern void ui_screen_debug_update(void);

// Update function tiap screen (dipanggil dari main loop saat screen itu aktif)
extern void ui_screen_idle_update(void);
extern void ui_screen_predictive_grind_update(void);
extern void ui_screen_pulse_correction_update(void);
extern void ui_screen_done_update(void);

// Debug cepat alasan tolak grind (tanpa Serial) -- lihat screen_idle.cpp
extern void ui_show_grind_reject_reason(AbortReason reason);
extern void ui_clear_grind_reject_reason(void);

// ============================================================
// STATE GLOBAL BERSAMA -- didefinisikan di sini (extern di ui_common.h)
// ============================================================
ui_shared_state_t g_ui_state = {
    .target_weight_g = 18.0f,
    .target_absolute_g = 18.0f,  // default = target_weight_g sebelum grind pertama dimulai (start_weight_g masih 0) -- akan diisi benar oleh syncGrindControllerToUi() begitu startGrind() pertama dipanggil
    .start_weight_g = 0.0f,
    .current_weight_g = 0.0f,
    .flow_rate_gps = 0.0f,
    .flow_start_confirmed = false,
    .grind_latency_ms = 0,
    .motor_stop_target_weight_g = 0.0f,  // konsisten dengan default motorStopTargetWeightG_ = 0.0f di GrindController (BUKAN NAN)
    .pulse_count = 0,
    .pulse_error_g = 0.0f,
    .grind_duration_ms = 0,
    .grind_success = false,
    .wifi_connected = false,
    .ble_connected = false,
    .accuracy_tolerance_g = GRIND_ACCURACY_TOLERANCE_G,
    .max_pulse_attempts = GRIND_MAX_PULSE_ATTEMPTS,
    .settle_time_ms = GRIND_SCALE_PRECISION_SETTLING_TIME_MS,  // BARU
};

static ui_screen_id_t s_current_screen = UI_SCREEN_IDLE;
static ui_screen_id_t s_screen_before_settings = UI_SCREEN_IDLE;  // untuk tombol back di Settings

static lv_obj_t* s_screens[8] = {nullptr};  // dinaikkan 7->8 -- UI_SCREEN_DEBUG ditambahkan

// ------------------------------------------------------------
// GETTER BARU -- dipakai main.cpp (handleGrindStateTransitionForUi())
// untuk memperbaiki BUG DITEMUKAN LEWAT TESTING SISTEMATIS: pencet
// Start saat startGrind() DITOLAK (mis. HX711 belum tersambung) ->
// GrindController transisi ke ABORT (BUKAN balik ke IDLE seperti
// yang SEMPAT DIKIRA sebelumnya -- IDLE=0, ABORT=8 di enum
// GrindState, ABORT tidak pernah auto-reset ke IDLE) -> UI langsung
// LOMPAT ke screen Done/"Finish Grind" walau operator TIDAK PERNAH
// meninggalkan screen Idle/Set Target sama sekali (karena fix
// ui_desync sebelumnya SUDAH mencegah navigasi ke Predictive Grind
// saat gagal -- tapi handleGrindStateTransitionForUi() TIDAK tahu
// itu, dia cuma lihat "state berubah ke ABORT" lalu asal navigasi ke
// Done, padahal tidak ada sesi grind aktif yang hasilnya perlu
// ditampilkan).
//
// FIX: main.cpp sekarang cek ui_get_current_screen() dulu -- HANYA
// navigasi ke Done kalau screen SEKARANG memang Predictive
// Grind/Pulse Correction (artinya operator memang sedang di tengah
// sesi grind aktif yang hasilnya perlu ditampilkan). Kalau screen
// sekarang Idle/Set Target (grind ditolak sebelum sempat mulai),
// transisi ke ABORT diabaikan untuk keperluan navigasi UI -- operator
// tetap di layar yang sama, cukup lihat pesan TOLAK di Serial.
ui_screen_id_t ui_get_current_screen(void) {
    return s_current_screen;
}

// Dipanggil dari main.cpp untuk trigger mulai grind -- lihat
// grind_controller.h GrindController::startGrind(). Nama fungsi
// dipertahankan "grind_start" untuk kompatibilitas dengan komentar
// lama di README (lihat catatan migrasi di sana).
extern bool grind_start(float target_g);  // return value ditambahkan -- lihat catatan bug di main.cpp grind_start()
extern AbortReason grind_last_abort_reason();  // debug cepat alasan tolak tanpa Serial -- lihat main.cpp

static lv_obj_t* get_or_create_screen(ui_screen_id_t id) {
    if (s_screens[id] != nullptr) return s_screens[id];

    switch (id) {
        case UI_SCREEN_SET_TARGET:       s_screens[id] = ui_screen_set_target_create(); break;
        case UI_SCREEN_IDLE:             s_screens[id] = ui_screen_idle_create(); break;
        case UI_SCREEN_PREDICTIVE_GRIND: s_screens[id] = ui_screen_predictive_grind_create(); break;
        case UI_SCREEN_PULSE_CORRECTION: s_screens[id] = ui_screen_pulse_correction_create(); break;
        case UI_SCREEN_DONE:             s_screens[id] = ui_screen_done_create(); break;
        case UI_SCREEN_SETTINGS:         s_screens[id] = ui_screen_settings_create(); break;
        case UI_SCREEN_MANUAL_GRIND:     s_screens[id] = ui_screen_manual_grind_create(); break;
        case UI_SCREEN_DEBUG:            s_screens[id] = ui_screen_debug_create(); break;
    }
    return s_screens[id];
}

static void navigate_to(ui_screen_id_t id) {
    s_current_screen = id;
    lv_obj_t* scr = get_or_create_screen(id);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

// ============================================================
// SWIPE-TO-HOME (permintaan eksplisit) -- swipe ke KANAN pada layar
// mengembalikan ke UI_SCREEN_SET_TARGET (Home yang baru, lihat
// ui_init()). SENGAJA HANYA dipasang di layar yang statusnya "aman"
// (Set Target, Idle, Done, Settings) -- TIDAK pernah dipasang di
// UI_SCREEN_PREDICTIVE_GRIND atau UI_SCREEN_PULSE_CORRECTION (saat
// motor sedang aktif menggiling), supaya operator tidak bisa tidak
// sengaja "kabur" dari layar itu lewat swipe selagi grind berjalan --
// pembatalan sesi yang sedang aktif harus tetap eksplisit lewat
// tombol Stop (stop_btn_cb -> grind_force_abort()), bukan gesture
// yang gampang ke-trigger tanpa sengaja.
// Counter total ui_go_home() terpanggil sejak boot -- BARU,
// ditambahkan untuk diagnosis laporan "layar tiba-tiba lompat ke Set
// Target SAAT GRINDING, sesekali/random, kedipan lebih cepat dari
// reboot". Kalau counter ini naik BERBARENGAN dengan kejadian tsb
// (dicek lewat Debug screen sesudahnya), itu mengarah ke phantom
// gesture (LVGL salah mendeteksi LV_DIR_RIGHT dari noise touch),
// BUKAN reboot -- lihat catatan lengkap di debug_snapshot.h. Dibaca
// lewat ui_home_gesture_count() (dipanggil grind_get_debug_snapshot()
// di main.cpp). RAM-only, cukup untuk diagnosis satu sesi pemakaian.
static unsigned long s_homeGestureCount = 0;

void ui_go_home(lv_event_t* e) {
    s_homeGestureCount++;
    navigate_to(UI_SCREEN_SET_TARGET);
}

unsigned long ui_home_gesture_count(void) {
    return s_homeGestureCount;
}

static void gesture_to_home_cb(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_get_act();
    if (indev == nullptr) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT) {
        ui_go_home(e);
    }
}

// Dipanggil dari screen_*_create() UNTUK LAYAR AMAN SAJA (lihat
// catatan di atas) -- memasang gesture swipe-kanan-untuk-home ke
// objek screen itu sendiri. TIDAK dipanggil dari
// screen_predictive_grind.cpp maupun screen_pulse_correction.cpp,
// itu keputusan sadar bukan kelalaian.
void ui_enable_swipe_home(lv_obj_t* screen) {
    lv_obj_add_event_cb(screen, gesture_to_home_cb, LV_EVENT_GESTURE, NULL);
}

// ============================================================
// CALLBACK IMPLEMENTASI -- dipanggil dari file screen_*.cpp
// ============================================================

void ui_open_settings(lv_event_t* e) {
    s_screen_before_settings = s_current_screen;
    navigate_to(UI_SCREEN_SETTINGS);
}

void ui_close_settings(lv_event_t* e) {
    navigate_to(s_screen_before_settings);
}

// ------------------------------------------------------------
// Manual Grind (test motor/kalibrasi) -- akses dari Settings (baris
// baru, scroll). SELALU kembali ke UI_SCREEN_SETTINGS (bukan
// s_screen_before_settings seperti ui_close_settings) -- konteks
// pemakaiannya konsisten cuma dari Settings, tidak perlu general
// "kembali ke layar sebelumnya" seperti Settings sendiri.
//
// ui_close_manual_grind() TIDAK dipanggil langsung dari tombol Back
// di screen_manual_grind.cpp -- tombol itu memanggil
// manual_grind_close_request() (extern dari main.cpp) DULU untuk
// memastikan motor mati kalau masih menyala (lihat catatan lengkap
// di screen_manual_grind.cpp), BARU fungsi ini dipanggil untuk
// navigasi. Dipisah supaya urutan "matikan motor DULU, baru
// navigasi" selalu konsisten, tidak bisa tertukar urutannya.
void ui_open_manual_grind(lv_event_t* e) {
    navigate_to(UI_SCREEN_MANUAL_GRIND);
}

void ui_close_manual_grind(lv_event_t* e) {
    navigate_to(UI_SCREEN_SETTINGS);
}

// Debug (read-only, tidak ada motor/state yang perlu "dibereskan"
// dulu seperti Manual Grind) -- akses dari Settings, SELALU kembali
// ke UI_SCREEN_SETTINGS.
void ui_open_debug(lv_event_t* e) {
    navigate_to(UI_SCREEN_DEBUG);
}

void ui_close_debug(lv_event_t* e) {
    navigate_to(UI_SCREEN_SETTINGS);
}

void ui_confirm_target(float target_g) {
    g_ui_state.target_weight_g = target_g;
    // target_absolute_g -- BELUM ada nilai valid dari GrindController
    // sampai startGrind() benar-benar dipanggil (start_weight_g/tare
    // belum diketahui). Isi dengan best-effort preview (asumsikan
    // start_weight_g saat ini sebagai perkiraan tare) supaya field ini
    // tidak menampilkan angka basi dari target dose SEBELUMNYA kalau
    // operator ganti-ganti target di Set Target screen sebelum grind
    // pertama dijalankan -- akan ditimpa dengan nilai SEBENARNYA oleh
    // syncGrindControllerToUi() begitu startGrind() dipanggil.
    g_ui_state.target_absolute_g = g_ui_state.start_weight_g + target_g;
    navigate_to(UI_SCREEN_IDLE);
}

void ui_start_grind(lv_event_t* e) {
    // FIX BUG UI-DESYNC (ditemukan lewat testing sistematis) -- SEBELUMNYA
    // navigate_to(UI_SCREEN_PREDICTIVE_GRIND) dipanggil DULUAN, sebelum
    // tahu apakah grind_start()/startGrind() benar-benar berhasil. Kalau
    // GrindController menolak (mis. belum ada sample berat valid dari
    // timbangan), ia langsung balik ke IDLE dalam sekejap, TAPI UI sudah
    // kadung menampilkan screen Predictive Grind ("Detecting Flow") --
    // layar itu lalu nyangkut selamanya (GrindController sudah IDLE,
    // tombol Stop/forceAbort() jadi early-return tanpa efek karena
    // "tidak ada apa-apa yang sedang berjalan untuk di-abort").
    // FIX: cek hasilnya dulu, cuma navigasi kalau benar-benar berhasil.
    // Pesan TOLAK spesifik (kenapa gagal) sudah di-print GrindController
    // sendiri ke Serial (lihat startGrind()) -- tetap di screen Set
    // Target/Idle di sini supaya operator bisa coba lagi.
    ui_clear_grind_reject_reason();  // bersihkan pesan tolak percobaan SEBELUMNYA (kalau ada)
    if (grind_start(g_ui_state.target_weight_g)) {
        navigate_to(UI_SCREEN_PREDICTIVE_GRIND);
    } else {
        // DEBUG CEPAT tanpa Serial (permintaan eksplisit Wahyu, case
        // sudah tertutup fisik) -- tampilkan alasan tolak di layar
        // Idle. grindController TIDAK diekspos langsung ke UI layer
        // (lihat pola project ini -- akses selalu lewat main.cpp), tapi
        // grind_start() ini SENDIRI ada di main.cpp dan memanggil
        // grindController.startGrind(), jadi grindController.abortReason()
        // hanya bisa dibaca dari main.cpp -- lihat grind_start() di
        // main.cpp untuk bagaimana alasan ini diteruskan ke sini.
        ui_show_grind_reject_reason(grind_last_abort_reason());
    }
}

void ui_new_grind(lv_event_t* e) {
    navigate_to(UI_SCREEN_SET_TARGET);
}

// ============================================================
// DIPANGGIL DARI GRIND STATE MACHINE (main.cpp) saat fase berubah --
// bukan dari UI event, tapi dari logic grind (GrindController::state()
// berubah). main.cpp yang memantau perubahan state dan memanggil ini.
// ============================================================
void ui_transition_to_pulse_correction(void) {
    navigate_to(UI_SCREEN_PULSE_CORRECTION);
}

void ui_transition_to_done(void) {
    // URUTAN PENTING (BUG YANG DIPERBAIKI): navigate_to() HARUS
    // dipanggil DULU, baru ui_screen_done_update(). Alasan: navigate_to()
    // -> get_or_create_screen() yang MEMBUAT widget Done screen kalau
    // belum pernah dibuat sebelumnya (mis. pada grind PERTAMA sejak
    // boot). Draft sebelumnya memanggil ui_screen_done_update() DULU --
    // pada grind pertama, semua widget Done (s_weight_label dkk) masih
    // nullptr saat itu, jadi guard "if (s_weight_label == nullptr)
    // return;" di ui_screen_done_update() langsung keluar tanpa mengisi
    // apa pun -- operator akan melihat teks default/mockup dari
    // ui_screen_done_create() pada grind PERTAMA, bukan hasil aktual
    // (grind kedua dan seterusnya kebetulan benar, karena widget sudah
    // ada dari grind pertama -- itu sebabnya bug ini gampang lolos
    // review kalau cuma dites lebih dari sekali).
    navigate_to(UI_SCREEN_DONE);
    ui_screen_done_update();  // isi nilai final SETELAH screen (dan widget-nya) pasti sudah ada
}

// ============================================================
// DIPANGGIL DARI MAIN LOOP -- update tampilan screen yang sedang aktif
// sesuai data terbaru di g_ui_state.
// ============================================================
void ui_tick(void) {
    switch (s_current_screen) {
        case UI_SCREEN_IDLE:             ui_screen_idle_update(); break;
        case UI_SCREEN_PREDICTIVE_GRIND: ui_screen_predictive_grind_update(); break;
        case UI_SCREEN_PULSE_CORRECTION: ui_screen_pulse_correction_update(); break;
        case UI_SCREEN_MANUAL_GRIND:     ui_screen_manual_grind_update(); break;
        case UI_SCREEN_DEBUG:            ui_screen_debug_update(); break;
        default: break;  // Set Target, Done, Settings tidak perlu tick berkala
    }

    // Update icon WiFi/BLE status bar screen yang sedang aktif --
    // status bar SELALU child index 0 dari tiap screen (semua
    // screen_*_create() memanggil ui_create_status_bar() paling
    // pertama). Settings screen sengaja tidak punya gear_cb (nullptr)
    // tapi status bar-nya tetap ada di index 0 yang sama, jadi ini
    // tetap aman dipanggil untuk semua screen termasuk Settings.
    lv_obj_t* current = s_screens[s_current_screen];
    if (current != nullptr) {
        lv_obj_t* status_bar = lv_obj_get_child(current, 0);
        ui_update_status_bar_dots(status_bar);
    }
}

// ============================================================
// INIT -- dipanggil sekali dari setup() di main.cpp
// ============================================================
void ui_init(void) {
    // PERUBAHAN (permintaan eksplisit): Home/screen pertama saat boot
    // sekarang UI_SCREEN_SET_TARGET (bukan UI_SCREEN_IDLE lagi) --
    // operator mengatur dosis dulu sebelum masuk ke layar siap-grind.
    // s_current_screen sudah default UI_SCREEN_IDLE di deklarasi
    // static di atas -- TIDAK diubah di sana supaya ui_confirm_target()
    // dan alur "Set Target -> Confirm -> Idle" tetap konsisten seperti
    // sebelumnya (Set Target SELALU transit ke Idle lewat Confirm,
    // tidak pernah jadi tujuan balik dari layar lain kecuali lewat
    // swipe/back -- lihat ui_go_home()).
    navigate_to(UI_SCREEN_SET_TARGET);
}
