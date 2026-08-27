// ============================================================
// GBW FIRMWARE -- HARDWARE FINAL (GPIO motor + HX711 load cell + WiFi OTA)
// ============================================================
// Menggantikan Tasmota HTTP (WiFi kontrol) + Myscale (BLE) sepenuhnya
// di firmware ini -- kontrol grind murni GPIO + polling HX711, TIDAK
// bergantung WiFi/BLE sama sekali. Project TERPISAH dari firmware
// Tasmota+BLE (yang tetap dipertahankan utuh sebagai referensi/
// cadangan) -- lihat catatan migrasi di README.
//
// WiFi DITAMBAHKAN KEMBALI di firmware ini, TAPI HANYA untuk OTA
// update firmware (lihat ota_manager.h) -- SAMA SEKALI TIDAK dipakai
// untuk jalur kontrol grind. Motor & timbangan tetap berfungsi normal
// walau WiFi gagal connect atau sedang OTA. BLE TIDAK ada di firmware
// ini (dibahas terpisah nanti untuk remote monitoring/app companion,
// di luar scope saat ini).
//
// PERINGATAN KESELAMATAN -- BACA WIRING.md SEBELUM MENYAMBUNG APA PUN
// SECARA FISIK. Motor Eureka WAJIB dikontrol lewat relay module yang
// dipasang PARALEL dengan microswitch fisik (bukan sinyal 3.3V/5V
// langsung ke PCB kontrol seperti referensi upstream Specialita) --
// sisi kontak relay HARUS dianggap AC MAINS sampai diverifikasi
// sebaliknya. Lihat WIRING.md untuk detail & cara verifikasi.
//
// KALIBRASI HX711 WAJIB -- lihat README bagian "Cara kalibrasi
// HX711" sebelum HX711_CALIBRATION_OFFSET/SCALE di config.h diisi
// angka asli (placeholder 0 SENGAJA membuat readWeightGrams()
// menolak dipakai, lihat hx711_reader.h).
//
// MODEL PREDICTIVE STOP -- VERSI 2, REAL-TIME (bukan lagi regresi
// dari dataset kalibrasi terpisah). Predictive stop sekarang memakai
// grind_latency_ms (T_onset, diukur SETIAP SESI grind) x
// GRIND_LATENCY_TO_COAST_RATIO x flow_now REAL-TIME -- lihat
// grind_controller.h untuk penjelasan lengkap & alasan perubahan.
// GRIND_LATENCY_TO_COAST_RATIO (config.h, default 1.0) adalah TITIK
// AWAL EKSPERIMEN, WAJIB dikalibrasi dari data GPIO+HX711 -- lihat
// README bagian "Kalibrasi GRIND_LATENCY_TO_COAST_RATIO".
// LatencyCalibrator TETAP ADA tapi perannya BERUBAH: sekarang alat
// VALIDASI rasio (bandingkan grind_latency_ms vs overshoot aktual),
// BUKAN lagi sumber model keputusan stop GrindController.
//
// Command Serial -- SAMA PERSIS seperti firmware Tasmota+BLE (supaya
// alur kerja operator tidak berubah): 'g <target>' kalibrasi
// (sekarang untuk validasi rasio, bukan sumber model), 'x' batal, 'r'
// lihat trial, 'c' hapus riwayat, 'grind <target>' predictive grind
// (model real-time), 'gs' lihat angka model real-time sesi
// saat ini/terakhir, 'stop' paksa abort grind yang sedang berjalan
// (setara tombol Stop di UI -- lihat grind_force_abort()). TIDAK ADA
// lagi 'd'/'t'/'tr' (command diagnostik jaringan Tasmota) karena tidak
// ada Tasmota di firmware ini.
//
// UI LVGL -- SEKARANG DISAMBUNG (per keputusan urutan kerja section 5
// brief: item 1-6 sudah selesai). main.cpp adalah SATU-SATUNYA
// jembatan antara GrindController (business logic) dan g_ui_state
// (data UI) -- lihat syncGrindControllerToUi()/
// handleGrindStateTransitionForUi() di bawah, dipanggil tiap loop()
// SETELAH grindController.update(). Tombol Start di screen Idle
// memanggil grind_start() (extern function di file ini), tombol Stop
// di screen Predictive Grind/Pulse Correction memanggil
// grind_force_abort() (extern function baru, menggantikan TODO
// placeholder yang sebelumnya cuma navigate_to() tanpa hentikan
// motor -- lihat perubahan di screen_predictive_grind.cpp/
// screen_pulse_correction.cpp).
//
// build_src_filter di platformio.ini sudah diubah supaya src/ui/
// ikut dikompilasi -- LVGL + driver display/touch AMOLED (Arduino_GFX
// Arduino_CO5300 QSPI + FT6146 I2C touch, sesuai board
// ESP32-C6-Touch-AMOLED-1.64) ada di src/ui/lv_port.h/.cpp.
//
// SOAL PIN QSPI/I2C TOUCH: pin sudah diverifikasi dari source resmi
// vendor (clone github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.64,
// dua sumber independen -- contoh Arduino & BSP ESP-IDF -- sepakat
// pada angka yang sama; lihat komentar lengkap & tabel pin di
// src/ui/lv_port.h). Pin BELUM diverifikasi secara FISIK pada unit
// board yang benar-benar terpasang (mis. lewat multimeter/continuity
// test langsung ke board) -- verifikasi sejauh ini murni dari
// dokumen/source resmi vendor, bukan pengukuran fisik unit Anda
// sendiri. Compile akan tetap SUKSES walau ada perbedaan revisi
// board yang tidak terduga -- kesalahan baru kelihatan saat uji fisik
// (display/touch tidak berfungsi). Tetap ikuti langkah uji bertahap
// di WIRING.md/README (nyalakan display dulu tanpa motor/HX711
// tersambung ke listrik mains) sebelum menganggap semua pin benar.
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "weight_filter.h"
#include "motor_controller.h"
#include "hx711_reader.h"
#include "latency_calibrator.h"
#include "grind_controller.h"
#include "ota_manager.h"

// ============================================================
// UI LVGL -- WIRING GrindController <-> g_ui_state (lihat brief
// section 5 poin 1-6). ui/ SEKARANG di-include ke build (lihat
// platformio.ini, baris exclude -<ui/> sudah dihapus).
//
// main.cpp TETAP satu-satunya "otak" yang tahu tentang
// GrindController -- ui_*.cpp TIDAK PERNAH include grind_controller.h
// langsung (dependency arahnya SATU ARAH: GrindController -> main.cpp
// -> g_ui_state -> UI, bukan sebaliknya). Ini menjaga UI murni
// presentation layer, sesuai catatan arsitektur di ui_common.h.
// ============================================================
#include "ui/ui_common.h"
#include "ui/lv_port.h"

extern void ui_init(void);
extern void ui_tick(void);
extern void ui_transition_to_pulse_correction(void);
extern void ui_transition_to_done(void);
extern ui_screen_id_t ui_get_current_screen(void);  // BARU -- lihat catatan bug di ui_screen_manager.cpp dan handleGrindStateTransitionForUi() di bawah

static WeightFilter weightFilter;
static GpioMotorController motorController(MOTOR_GPIO_PIN, MOTOR_GPIO_ACTIVE_HIGH);
static HX711Reader hx711(HX711_DOUT_PIN, HX711_SCK_PIN, HX711_GAIN);
static LatencyCalibrator latencyCalibrator(&weightFilter, &motorController);
static GrindController grindController(&weightFilter, &motorController, &latencyCalibrator);
static OtaManager otaManager;

// Dipakai untuk deteksi PERUBAHAN state (edge-triggered) supaya
// ui_transition_to_pulse_correction()/ui_transition_to_done() cuma
// dipanggil SEKALI per transisi, bukan tiap loop selama state itu
// aktif (navigate_to() di ui_screen_manager.cpp memicu animasi fade
// tiap dipanggil -- kalau dipanggil berulang tiap loop, animasi akan
// retrigger terus dan screen jadi tidak bisa dipakai/di-tap).
static GrindState s_lastGrindState = GrindState::IDLE;

// ------------------------------------------------------------
// Dipanggil ui_screen_manager.cpp (tombol Start di screen Idle) --
// nama function DIPERTAHANKAN "grind_start" sesuai komentar lama di
// ui_screen_manager.cpp/README (lihat catatan kompatibilitas nama).
//
// RETURN VALUE ditambahkan (bool, sebelumnya void) -- BUG DITEMUKAN
// LEWAT TESTING SISTEMATIS (skenario: board standalone tanpa HX711,
// tombol Start ditekan -> startGrind() DITOLAK & GrindController
// balik ke IDLE, TAPI UI sudah kadung navigasi ke screen Predictive
// Grind sebelum tahu hasilnya -- lihat ui_start_grind() di
// ui_screen_manager.cpp). Tanpa return value ini, caller (UI) tidak
// pernah tahu startGrind() gagal, sehingga layar "Detecting Flow"
// nyangkut selamanya: GrindController sudah IDLE, forceAbort() dari
// tombol Stop jadi early-return tanpa efek (lihat guard di
// GrindController::forceAbort()), padahal secara visual UI masih di
// screen Predictive Grind.
// ------------------------------------------------------------
bool grind_start(float target_g) {
    return grindController.startGrind(target_g);
}

// ------------------------------------------------------------
// Dipanggil ui_screen_manager.cpp / screen_predictive_grind.cpp /
// screen_pulse_correction.cpp (tombol Stop) -- forceAbort() yang
// SESUNGGUHNYA, menggantikan TODO placeholder yang sebelumnya cuma
// navigate_to(UI_SCREEN_SET_TARGET) tanpa benar-benar menghentikan
// motor. AbortReason NONE dipakai di sini murni sebagai penanda
// "dibatalkan manual oleh operator lewat tombol Stop", BUKAN kondisi
// error -- forceAbort() sendiri yang menentukan urutan safety
// (doAbort()/stopMotorOrAbort(), lihat grind_controller.cpp).
// ------------------------------------------------------------
void grind_force_abort(void) {
    grindController.forceAbort(AbortReason::NONE);
}

// ------------------------------------------------------------
// Sinkron ARAH SEBALIKNYA: g_ui_state (Settings screen, ditulis
// operator lewat stepper +/- tolerance & max pulses) -> GrindController
// (lewat setter, snapshot-at-start -- lihat komentar lengkap di
// grind_controller.h setAccuracyToleranceG()/setMaxPulseAttempts()).
// Dipanggil TIAP loop() SEBELUM syncGrindControllerToUi() (arah
// sebaliknya) -- urutan ini tidak signifikan secara fungsional
// (kedua arah menulis field yang berbeda), tapi dikelompokkan di sini
// supaya jelas "UI menulis ke controller" dan "controller menulis ke
// UI" adalah dua alur terpisah yang tidak saling tumpang tindih
// field-nya.
//
// CATATAN PENTING: sebelum wiring ini ada, g_ui_state.accuracy_tolerance_g/
// max_pulse_attempts HANYA nilai tampilan yang TIDAK memengaruhi
// algoritma sama sekali (GrindController membaca konstanta
// GRIND_ACCURACY_TOLERANCE_G/GRIND_MAX_PULSE_ATTEMPTS langsung dari
// config.h) -- operator bisa mengubah Settings dan mengira sudah
// berefek padahal tidak. INI SUDAH DIPERBAIKI: setter di bawah
// dipanggil tiap loop, jadi perubahan stepper di Settings screen
// benar-benar tersalur ke GrindController (walau baru berlaku efektif
// di sesi grind BERIKUTNYA, bukan sesi yang sedang berjalan -- lihat
// snapshot-at-start di startGrind()).
// ------------------------------------------------------------
static void syncUiSettingsToGrindController() {
    grindController.setAccuracyToleranceG(g_ui_state.accuracy_tolerance_g);
    grindController.setMaxPulseAttempts(g_ui_state.max_pulse_attempts);
}

// ------------------------------------------------------------
// Sinkron SATU ARAH: GrindController (sumber kebenaran) -> g_ui_state
// (data murni untuk presentation). Dipanggil TIAP loop() SEBELUM
// ui_tick(), supaya UI yang sedang aktif selalu lihat angka terbaru.
// Field yang TIDAK disentuh di sini: target_weight_g (diisi user
// lewat Set Target screen, murni input belum ada grind berjalan),
// wifi_connected (diisi terpisah dari OtaManager, lihat di bawah).
// accuracy_tolerance_g/max_pulse_attempts JUGA TIDAK disentuh di sini
// (arah sebaliknya -- UI ke controller -- lihat
// syncUiSettingsToGrindController() di atas; kalau disentuh di sini
// juga, akan terjadi tarik-menarik dua arah yang membingungkan: UI
// menulis nilai baru, lalu fungsi ini langsung menimpanya balik dari
// controller di loop yang sama).
// ------------------------------------------------------------
static void syncGrindControllerToUi() {
    g_ui_state.current_weight_g = grindController.currentWeightG();
    // target_absolute_g/start_weight_g -- BARU, memperbaiki bug
    // semantik dose-vs-absolute (lihat komentar lengkap di ui_common.h
    // pada field target_weight_g). GrindController::targetAbsoluteG()/
    // startWeightG() masih 0 sebelum startGrind() pertama pernah
    // dipanggil (lihat konstruktor GrindController) -- itu SENGAJA
    // konsisten dengan default g_ui_state.target_absolute_g di
    // ui_screen_manager.cpp (diisi sama dengan target_weight_g awal),
    // supaya screen yang membaca field ini sebelum grind pertama tidak
    // menerima 0 secara tiba-tiba.
    if (grindController.state() != GrindState::IDLE || grindController.result() != GrindResult::NONE) {
        // Sudah pernah/sedang startGrind() -- nilai targetAbsoluteG()/
        // startWeightG() valid, aman disalin.
        g_ui_state.target_absolute_g = grindController.targetAbsoluteG();
        g_ui_state.start_weight_g = grindController.startWeightG();
    }
    // Kalau masih IDLE murni (belum pernah grind sama sekali sejak
    // boot), g_ui_state.target_absolute_g/start_weight_g TETAP pakai
    // default dari ui_screen_manager.cpp -- tidak ditimpa 0 di sini.
    g_ui_state.flow_rate_gps = grindController.currentFlowGps();
    g_ui_state.flow_start_confirmed = grindController.flowStartConfirmed();
    g_ui_state.grind_latency_ms = grindController.grindLatencyMs();
    g_ui_state.pulse_count = grindController.pulseAttempts();
    // pulse_error_g -- selama PULSE_CORRECTION, "error saat ini" =
    // currentWeight - target (BUKAN finalErrorG(), yang baru valid
    // setelah COMPLETE/ABORT). Dihitung di sini karena GrindController
    // tidak expose getter khusus untuk error live selama pulsa --
    // targetAbsoluteG()/currentWeightG() sudah public, cukup dari sini
    // tanpa perlu tambah getter baru ke GrindController.
    g_ui_state.pulse_error_g = grindController.currentWeightG() - grindController.targetAbsoluteG();
    g_ui_state.grind_duration_ms = grindController.grindDurationMs();

    // grind_success -- hanya benar-benar berarti begitu result() bukan
    // NONE (grind selesai/aborted). Screen Done yang membaca field ini
    // (lihat ui_screen_done_update()) SUDAH menghitung ulang
    // within_tolerance dari current_weight_g/target_absolute_g/
    // accuracy_tolerance_g sendiri (DIPERBAIKI dari target_weight_g --
    // lihat catatan bug dose-vs-absolute di ui_common.h) -- field
    // grind_success di sini dipertahankan untuk kompatibilitas struct
    // (mengikuti definisi di ui_common.h), diisi dari
    // GrindResult::SUCCESS supaya konsisten.
    g_ui_state.grind_success = (grindController.result() == GrindResult::SUCCESS);

    g_ui_state.wifi_connected = otaManager.isWifiConnected();
    // ble_connected SENGAJA TIDAK PERNAH disentuh di sini -- tetap
    // false selamanya (placeholder), lihat catatan ui_common.h/
    // config.h soal BLE belum diimplementasikan.
}

// ------------------------------------------------------------
// Deteksi transisi state grind (edge-triggered) -> panggil fungsi
// navigasi UI yang sesuai SEKALI saja per transisi. Dipanggil TIAP
// loop() setelah syncGrindControllerToUi().
// ------------------------------------------------------------
static void handleGrindStateTransitionForUi() {
    GrindState now = grindController.state();
    if (now == s_lastGrindState) {
        s_lastGrindState = now;
        return;
    }

    if (now == GrindState::PULSE_CORRECTION) {
        ui_transition_to_pulse_correction();
    } else if (now == GrindState::COMPLETE || now == GrindState::ABORT) {
        // FIX BUG (ditemukan lewat testing sistematis, dilaporkan
        // sebagai "pencet Start langsung lompat ke Finish Grind" saat
        // HX711 belum tersambung): GUARD BARU -- cuma navigasi ke Done
        // kalau screen SEKARANG memang Predictive Grind/Pulse
        // Correction (operator memang sedang di tengah sesi grind
        // aktif). SEBELUM fix ini, transisi ke ABORT (state 8, BUKAN
        // IDLE=0 -- IDLE dan ABORT sempat tertukar dalam analisis
        // sebelumnya) SELALU memicu navigasi ke Done, termasuk saat
        // startGrind() ditolak sebelum operator sempat meninggalkan
        // screen Idle/Set Target sama sekali (grind_start() gagal,
        // fix ui_desync sebelumnya sudah mencegah navigasi KE
        // Predictive Grind saat itu, tapi fungsi ini tidak tahu itu
        // dan tetap asal navigasi KELUAR ke Done).
        ui_screen_id_t current = ui_get_current_screen();
        if (current == UI_SCREEN_PREDICTIVE_GRIND || current == UI_SCREEN_PULSE_CORRECTION) {
            ui_transition_to_done();
        }
        // Kalau current BUKAN salah satu dari itu (mis. masih Idle/Set
        // Target karena grind ditolak sebelum sempat mulai), TIDAK
        // ADA navigasi -- operator tetap di layar yang sama, cukup
        // lihat pesan TOLAK di Serial (sudah di-print GrindController
        // sendiri di startGrind()).
    }
    // Transisi lain (VALIDATING/STARTING/WAIT_FLOW_START/GRINDING/
    // WAIT_SETTLE) TIDAK butuh navigasi screen terpisah -- semuanya
    // tetap di screen Predictive Grind (sudah dinavigasi duluan oleh
    // ui_start_grind() saat tombol Start ditekan, lihat
    // ui_screen_manager.cpp), cukup diupdate datanya lewat
    // syncGrindControllerToUi() + ui_tick().

    s_lastGrindState = now;
}

// ------------------------------------------------------------
// Command Serial -- 'grind'/'gs' ditangani di sini dulu (function
// pointer, dipasang sebagai unknown-command-handler ke
// LatencyCalibrator, SAMA POLA seperti firmware Tasmota+BLE) supaya
// LatencyCalibrator tetap satu-satunya pembaca Serial (menghindari
// dua consumer buffer Serial yang sama, lihat catatan arsitektur di
// latency_calibrator.h).
// ------------------------------------------------------------
static void handleGrindCommand(const String& line) {
    if (line.startsWith("grind ") || line.startsWith("GRIND ")) {
        String targetStr = line.substring(6);
        targetStr.trim();
        if (targetStr.length() == 0) {
            Serial.println("[GRIND] Format: grind <target_gram>, contoh: grind 18");
            return;
        }
        float target = targetStr.toFloat();
        if (target <= 0) {
            Serial.println("[GRIND] Target harus > 0 gram.");
            return;
        }
        grindController.startGrind(target);
    } else if (line == "stop" || line == "STOP" || line == "abort" || line == "ABORT") {
        // Setara tombol Stop di UI (screen Predictive Grind/Pulse
        // Correction) -- berguna untuk debug/uji tanpa UI terpasang.
        Serial.println("[GRIND] Force abort diminta lewat Serial.");
        grind_force_abort();
    } else if (line == "gs" || line == "GS") {
        // Diagnostik model real-time (VERSI 2) -- BUKAN lagi status
        // regresi/kesiapan kalibrasi (itu sudah dihapus, lihat
        // grind_controller.h). Tampilkan angka yang dipakai sesi
        // grind SAAT INI/TERAKHIR, supaya operator bisa lihat persis
        // apa yang terjadi.
        Serial.println("========================================");
        Serial.println("[GRIND STATUS -- MODEL REAL-TIME]");
        Serial.printf("  GRIND_LATENCY_TO_COAST_RATIO (config.h) : %.3f\n", (float)GRIND_LATENCY_TO_COAST_RATIO);
        Serial.printf("  Flow start confirmed sesi ini            : %s\n", grindController.flowStartConfirmed() ? "YA" : "BELUM");
        Serial.printf("  grind_latency_ms (T_onset) sesi ini      : %lu ms\n", grindController.grindLatencyMs());
        Serial.printf("  motor_stop_target_weight_g (saat ini)    : %.3f g\n", grindController.motorStopTargetWeightG());
        Serial.printf("  P95 flow sesi (untuk pulsa)               : %.2f gps\n", grindController.sessionPulseFlowGps());
        // finalWeightG/finalErrorG -- BARU ditambahkan (sebelumnya
        // tidak ditampilkan 'gs' sama sekali), supaya prosedur
        // kalibrasi ratio yang benar (lihat README, pakai grindLatencyMs
        // + error akhir dari SESI GRIND YANG SAMA lewat 'grind
        // <target>' + 'gs', BUKAN campur data dengan LatencyCalibrator
        // 'g <target>' yang terpisah) bisa benar-benar diikuti operator
        // tanpa perlu hitung manual dari currentWeightG()/targetAbsoluteG().
        // finalWeightG_ hanya valid setelah result() bukan NONE (grind
        // sudah COMPLETE/ABORTED) -- NAN selama grind masih berjalan,
        // dicetak apa adanya (termasuk NAN) supaya operator tahu belum
        // ada hasil final untuk sesi yang sedang berlangsung.
        Serial.printf("  final_weight_g (sesi ini/terakhir)        : %.3f g\n", grindController.finalWeightG());
        Serial.printf("  final_error_g (final - target_absolut)    : %+.3f g\n", grindController.finalErrorG());
        Serial.println("========================================");
        Serial.printf("  State grind sekarang: %d, result: %d, abortReason: %d\n",
                      (int)grindController.state(), (int)grindController.result(), (int)grindController.abortReason());
        Serial.println("  CATATAN: GRIND_LATENCY_TO_COAST_RATIO adalah TITIK AWAL (1.0), belum tentu akurat untuk hardware ini.");
        Serial.println("  Kalibrasi: pakai 'grind <target>' berulang + 'gs' setelah TIAP sesi (grind_latency_ms & final_error_g di atas dari sesi YANG SAMA) -- lihat README, JANGAN campur dengan data 'g <target>' (LatencyCalibrator, sesi terpisah).");
    } else if (line == "raw" || line == "RAW") {
        // Debug -- baca beberapa sample mentah HX711 langsung (blocking),
        // berguna untuk verifikasi wiring/kalibrasi tanpa perlu proses
        // kalibrasi penuh.
        Serial.println("[HX711] Baca 10 sample mentah (rata-rata)...");
        long raw = hx711.readRawAverage(10);
        Serial.printf("[HX711] Raw average: %ld\n", raw);
        if (HX711_CALIBRATION_SCALE != 0.0f) {
            float grams = (raw - HX711_CALIBRATION_OFFSET) / HX711_CALIBRATION_SCALE;
            Serial.printf("[HX711] Dengan kalibrasi config.h saat ini: %.2f g\n", grams);
        } else {
            Serial.println("[HX711] Kalibrasi (HX711_CALIBRATION_SCALE) belum diisi di config.h.");
        }
    } else {
        Serial.println("[CALIB] Perintah: 'g <target_gram>' mulai, 'x' batal, 'r' lihat semua trial, 'c' hapus riwayat.");
        Serial.println("[GRIND] Perintah: 'grind <target_gram>' predictive grind (model real-time), 'gs' lihat angka model sesi saat ini/terakhir, 'stop' paksa abort.");
        Serial.println("[HX711] Perintah: 'raw' baca sample mentah (debug wiring/kalibrasi).");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== GBW Firmware -- HARDWARE FINAL (GPIO + HX711 + WiFi OTA) ===");
    Serial.println("Kontrol grind: GPIO motor + polling HX711, TIDAK bergantung WiFi.");
    Serial.println("WiFi HANYA untuk OTA update firmware (lihat ota_manager.h).");

    Serial.printf("[MOTOR] GPIO pin %d (activeHigh=%s) -- pastikan sudah lewat relay module (paralel microswitch), lihat WIRING.md\n",
                  MOTOR_GPIO_PIN, MOTOR_GPIO_ACTIVE_HIGH ? "true" : "false");
    // begin() -- inisialisasi hardware SEBENARNYA (pinMode + pastikan
    // motor OFF sejak awal) dipanggil EKSPLISIT di sini, SETELAH
    // Serial.begin() di atas (per review: constructor GpioMotorController
    // TIDAK LAGI menyentuh hardware/Serial sama sekali, lihat
    // motor_controller.h/.cpp untuk alasan lengkap -- motorController
    // dideklarasikan `static` global, constructor-nya berjalan
    // SEBELUM setup(), jadi akses hardware/Serial harus ditunda
    // sampai di sini).
    motorController.begin();

    Serial.println("[HX711] Init...");
    if (!hx711.begin()) {
        Serial.println("[HX711] GAGAL init -- cek wiring DOUT/SCK/VCC/GND (lihat WIRING.md). Firmware tetap lanjut, tapi berat tidak akan terbaca.");
    }
    hx711.setCalibration(HX711_CALIBRATION_OFFSET, HX711_CALIBRATION_SCALE);
    if (HX711_CALIBRATION_SCALE == 0.0f) {
        Serial.println("[HX711] PERINGATAN -- kalibrasi (HX711_CALIBRATION_OFFSET/SCALE) BELUM diisi di config.h.");
        Serial.println("[HX711] Ikuti README bagian 'Cara kalibrasi HX711' sebelum kalibrasi/grind dipakai.");
    }

    latencyCalibrator.setUnknownCommandHandler(handleGrindCommand);

    // WiFi+OTA dimulai TERAKHIR (setelah motor/HX711/calibrator siap)
    // -- non-blocking, tidak menunda inisialisasi komponen kontrol
    // grind yang jauh lebih kritis. Kalau WiFi lambat/gagal connect,
    // firmware tetap lanjut normal (lihat catatan isolasi di
    // ota_manager.h).
    otaManager.begin();

    // UI LVGL diinit PALING TERAKHIR -- setelah semua komponen kontrol
    // grind & OTA siap. lv_port_init() HARUS dipanggil SEBELUM
    // ui_init() (LVGL core + display/touch driver harus siap sebelum
    // ui_init() membuat screen pertama lewat lv_obj_create()/dkk).
    // ui_init() sendiri TIDAK menyentuh GrindController/HX711/motor
    // sama sekali (murni bikin display + navigate ke screen Idle
    // pertama kali), jadi urutan ini aman.
    // g_ui_state.target_weight_g/accuracy_tolerance_g/
    // max_pulse_attempts sudah punya default dari config.h (lihat
    // inisialisasi struct di ui_screen_manager.cpp) sebelum operator
    // sempat ubah lewat Set Target/Settings screen.
    Serial.println("[UI] Init LVGL + display/touch driver...");
    lv_port_init();
    ui_init();
    Serial.println("[UI] LVGL siap.");

    Serial.println("\nSetup selesai.");
    Serial.println("[CALIB] Kalibrasi coast/decay: 'g <target>' mulai, 'x' batal, 'r' lihat trial, 'c' hapus riwayat.");
    Serial.println("[GRIND] Predictive grind (model real-time): 'grind <target>' mulai, 'gs' cek status/angka model.");
    Serial.println("[HX711] 'raw' baca sample mentah untuk debug wiring/kalibrasi.");
    Serial.println("[OTA] Status WiFi/OTA akan tercetak begitu connect (lihat log di atas beberapa detik lagi).");
}

void loop() {
    // HX711 dibaca via POLLING (bukan callback async seperti BLE) --
    // cek isReady() dulu supaya loop() TIDAK PERNAH blocking menunggu
    // HX711 (beda dari BLE yang sample-nya masuk sendiri lewat NimBLE
    // task terpisah). Kalau belum ready, skip iterasi ini, lanjut ke
    // command Serial & grindController.update() seperti biasa.
    if (hx711.isReady()) {
        float rawWeight = hx711.readWeightGrams();
        unsigned long sampleTimestampMs = millis();

        if (!isnan(rawWeight)) {
            bool accepted = weightFilter.pushRawSample(rawWeight, sampleTimestampMs, GRIND_FLOW_RATE_MAX_SANE_GPS);
            if (accepted) {
                // SEKALI PER SAMPLE VALID -- sama pola arsitektur
                // seperti firmware BLE (lihat catatan di
                // latency_calibrator.h/grind_controller.h), cuma
                // sumbernya polling HX711 di sini, bukan BLE queue.
                latencyCalibrator.onWeightSample(rawWeight, sampleTimestampMs);
                grindController.onWeightSample(rawWeight, sampleTimestampMs);
            }
        }
    }

    latencyCalibrator.update();
    grindController.update();

    // OTA update() SETELAH kontrol grind -- prioritas urutan: kontrol
    // grind dulu (HX711 + calibrator + grindController), OTA
    // belakangan. allowOtaHandling = true HANYA kalau GrindController
    // sedang IDLE atau sudah selesai (COMPLETE/ABORT) -- SELAMA grind
    // aktif berjalan (VALIDATING s/d PULSE_CORRECTION), OTA di-skip
    // total pada iterasi ini (lihat komentar lengkap di ota_manager.h)
    // supaya upload OTA tidak bisa mengganggu timing predictive-stop,
    // bukan lagi cuma soal disiplin operator.
    bool grindIsIdle = (grindController.state() == GrindState::IDLE ||
                         grindController.state() == GrindState::COMPLETE ||
                         grindController.state() == GrindState::ABORT);
    otaManager.update(grindIsIdle);

    // ------------------------------------------------------------
    // UI LVGL -- PALING TERAKHIR di loop(), setelah semua keputusan
    // kontrol grind sudah diambil untuk iterasi ini. Urutan:
    //   0) sync g_ui_state Settings (tolerance/max pulses) -> GrindController
    //      (arah UI->controller, satu-satunya arah ini di seluruh
    //      sinkronisasi -- lihat syncUiSettingsToGrindController())
    //   1) sync data terbaru GrindController -> g_ui_state (arah
    //      sebaliknya, controller->UI, untuk field lain semuanya)
    //   2) cek transisi state (edge-triggered) -> navigasi screen
    //      kalau perlu (PULSE_CORRECTION/COMPLETE/ABORT)
    //   3) ui_tick() -- update angka/redraw widget di screen yang
    //      sedang aktif berdasarkan g_ui_state (murni set_text/
    //      set_value dkk, TIDAK menyentuh LVGL timer/render internal)
    //   4) lv_port_tick() -- proses render+animasi LVGL sesungguhnya
    //      (lv_tick_inc() + lv_timer_handler()), HARUS dipanggil
    //      setelah ui_tick() supaya perubahan widget di atas ikut
    //      ter-render di siklus yang sama.
    // UI TIDAK PERNAH mempengaruhi KEPUTUSAN kontrol grind yang sudah
    // diambil di atas pada iterasi loop() yang sama (murni konsumen
    // data untuk itu) -- TAPI operator BISA mengubah parameter
    // tolerance/max-pulses lewat Settings, yang tersalur ke
    // GrindController lewat langkah 0) di atas dan baru berlaku efektif
    // di sesi startGrind() BERIKUTNYA (snapshot-at-start, lihat
    // grind_controller.h).
    // ------------------------------------------------------------
    syncUiSettingsToGrindController();
    syncGrindControllerToUi();
    handleGrindStateTransitionForUi();
    ui_tick();
    lv_port_tick();

    delay(5);  // jeda kecil -- HX711 default rate ~10-80Hz, tidak perlu polling lebih cepat dari itu
}
