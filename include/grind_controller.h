#pragma once

#include <Arduino.h>
#include "weight_filter.h"
#include "motor_controller.h"
#include "latency_calibrator.h"
#include "config.h"

// ============================================================
// GrindController -- state machine predictive grind-by-weight
// ============================================================
// VERSI 2 -- MODEL REAL-TIME (menggantikan regresi statistik versi 1)
//
// Perubahan besar dari versi sebelumnya, hasil verifikasi LANGSUNG ke
// source code upstream (jaapp/smart-grind-by-weight,
// src/controllers/weight_grind_strategy.cpp +
// src/config/grind_control.h) -- BUKAN tebakan dari nama variabel
// atau dokumentasi naratif semata. Keputusan final:
//
// MODEL LAMA (dibuang): coastWeightG = a * flowAtStop + b, regresi
// linear dari dataset trial LatencyCalibrator yang dikumpulkan di
// SATU kondisi grind (grind size/kopi tertentu). Masalah yang
// ditemukan: ganti grind size/kopi mengubah profil flow rate secara
// fundamental, dan regresi yang dilatih di satu kondisi tidak
// otomatis representatif untuk kondisi lain -- bahkan dengan clamp ke
// rentang observed, RELASI flow->overshoot itu sendiri bisa berbeda,
// bukan cuma soal rentang nilai.
//
// MODEL BARU (dipakai sekarang) -- REAL-TIME PER SESI, tidak
// bergantung dataset historis:
//
//   1) grind_latency_ms: diukur SETIAP SESI grind, dari motor ON
//      sampai flow PERTAMA terdeteksi (>= GRIND_FLOW_DETECTION_THRESHOLD_GPS,
//      dikonfirmasi dalam window GRIND_LATENCY_CONFIRMATION_MS = 500ms,
//      PERSIS meniru upstream run_predictive_phase()). Ini T_onset
//      (motor ON -> flow pertama), SECARA FISIK BEDA dari T_stop
//      (keputusan OFF -> motor benar-benar berhenti mengalirkan kopi)
//      -- upstream TIDAK mencampur keduanya, T_onset dipakai sebagai
//      PROXY untuk estimasi coast/T_stop, dikoreksi lewat rasio (lihat
//      poin 2).
//
//      KONFIRMASI 500ms (per review, FIX dari implementasi awal yang
//      SALAH): flow >= threshold TIDAK BOLEH langsung dianggap
//      "confirmed" dari SATU sample. State WAIT_FLOW_START mencatat
//      candidateFlowStartMs_ begitu SATU sample pertama >= threshold
//      terlihat, lalu TERUS memantau -- flow_confirmed_ baru jadi true
//      kalau flow TETAP >= threshold SELAMA GRIND_LATENCY_CONFIRMATION_MS
//      (500ms) BERTURUT-TURUT sejak candidate itu. Kalau flow turun
//      di bawah threshold sebelum window selesai, candidate DIRESET
//      (kembali menunggu sample pertama berikutnya) -- ini mencegah
//      satu spike noise sesaat salah dianggap "flow sudah mulai".
//      grind_latency_ms akhirnya = candidateFlowStartMs_ - motorStartedMs_
//      (waktu SAMPLE PERTAMA yang memulai window konfirmasi yang
//      berhasil, bukan waktu window itu SELESAI).
//
//   2) coast_time_ms = grind_latency_ms * GRIND_LATENCY_TO_COAST_RATIO
//      Rasio ini mengonversi T_onset (mudah diukur) ke estimasi
//      T_stop/coast (yang secara fisik berbeda). FIXED untuk versi
//      pertama (bukan adaptif) -- lihat GRIND_LATENCY_TO_COAST_RATIO
//      di config.h. TIDAK ADA LAGI GRIND_COAST_SAFETY_FACTOR terpisah
//      -- itu sengaja DIHAPUS supaya tidak ada double-margin (T_onset
//      x ratio x safety_factor tanpa dasar empiris jelas). Satu
//      parameter, satu fungsi konversi.
//
//   3) motor_stop_target_weight_g = flow_now_gps * coast_time_ms / 1000
//      flow_now_gps DIUKUR REAL-TIME (bukan dari histori/regresi) --
//      ini yang membuat model otomatis mengikuti kondisi grind SAAT
//      INI (grind size, kopi, kelembapan, dst) tanpa perlu tahu
//      kondisi itu secara eksplisit.
//
//   4) STOP saat currentWeight >= targetWeight - motor_stop_target_weight_g
//      HANYA berlaku setelah flow CONFIRMED (state GRINDING). SELAMA
//      WAIT_FLOW_START (flow belum confirmed), predictive stop TIDAK
//      AKTIF SAMA SEKALI -- lihat poin safety di bawah (perbaikan
//      penting dari implementasi awal yang salah pakai fallback 0).
//
// SAFETY SELAMA WAIT_FLOW_START (per review, FIX dari implementasi
// awal): implementasi awal memakai motorStopTargetWeightG_ = 0
// sebagai fallback sebelum flow confirmed, yang berarti stopThreshold
// = targetAbsoluteG_ persis -- motor akan tetap menyala sampai berat
// MENCAPAI TARGET PENUH kalau flow lambat terdeteksi, BERISIKO
// OVERSHOOT BESAR (predictive stop yang seharusnya berhenti LEBIH
// AWAL dari target malah tidak aktif sama sekali). Ini SALAH dan
// SUDAH DIPERBAIKI: selama WAIT_FLOW_START, GrindController TIDAK
// mengevaluasi predictive stop berdasarkan berat SAMA SEKALI --
// satu-satunya jalan keluar dari state ini adalah (a) flow confirmed
// -> lanjut GRINDING, atau (b) GRIND_STALL_TIMEOUT_MS terlampaui ->
// ABORT(STALL). GRIND_STALL_TIMEOUT_MS (5000ms) HARUS cukup longgar
// untuk mengakomodasi kopi yang butuh waktu lebih lama sebelum flow
// muncul -- ini dipisahkan secara jelas dari GRIND_LATENCY_CONFIRMATION_MS
// (500ms, durasi window konfirmasi setelah flow PERTAMA terlihat,
// BUKAN batas waktu total menunggu flow muncul).
//
// PERAN BARU LatencyCalibrator (BUKAN LAGI sumber model, TIDAK
// mempengaruhi keputusan stop GrindController sama sekali): alat
// EKSPERIMEN terpisah untuk memvalidasi/menentukan
// GRIND_LATENCY_TO_COAST_RATIO -- bandingkan grind_latency_ms terukur
// vs overshoot aktual yang terjadi, cari rasio yang membuat prediksi
// paling akurat. Setelah rasio ditetapkan (via eksperimen manual,
// bukan otomatis), GrindController pakai angka itu sebagai konstanta
// tetap -- BUKAN membaca ulang dataset LatencyCalibrator setiap
// grind. Referensi ke LatencyCalibrator di constructor DIPERTAHANKAN
// murni untuk kompatibilitas API/kemungkinan pemakaian command Serial
// bersama, TAPI computeRegressionStatus()/regresi lama TIDAK ADA LAGI
// dan TIDAK dipanggil dari jalur keputusan mana pun.
//
// PULSE CORRECTION -- flow P95 SESI INI (bukan flow_now real-time per
// pulsa, BEDA dari predictive stop di atas). Alasan (dari perbandingan
// eksplisit dengan upstream): sisa berat kecil + durasi pulsa pendek
// membuat estimasi flow_now sangat rentan noise/jumlah sample
// sedikit. P95 dihitung SEKALI, tepat setelah predictive stop, dari
// sample flow yang PALING BARU dalam window GRIND_PULSE_P95_WINDOW_MS
// (2500ms) SEBELUM predictive stop terjadi (per review, FIX dari
// implementasi awal yang salah pakai SELURUH histori sesi tanpa batas
// window -- itu membuat konstanta GRIND_PULSE_P95_WINDOW_MS tidak
// benar-benar berfungsi). Filtering window ini pakai TIMESTAMP sample
// (bukan sekadar menyimpan nilai float tanpa waktu) -- lihat
// implementasi pushFlowSample()/computeSessionP95() di .cpp.
//
// SEMUA KONSTANTA REGRESI LAMA (GRIND_MIN_CALIBRATION_TRIALS,
// GRIND_MIN_REGRESSION_R2, GRIND_MIN_FLOW_RANGE_GPS,
// GRIND_COAST_SAFETY_FACTOR) DIHAPUS dari config.h -- tidak ada lagi
// gate kesiapan berbasis dataset kalibrasi, karena model baru tidak
// butuh dataset itu. Grind bisa langsung dicoba begitu hardware
// terpasang (dengan risiko rasio default 1.0 belum terkalibrasi --
// lihat README untuk proses kalibrasi rasio yang disarankan).
//
// STALL DETECTION, HARD OVERSHOOT, MAX DURATION, MOTOR OFF FAILURE
// SAFETY -- TIDAK BERUBAH dari versi sebelumnya, tetap berlaku penuh
// (lihat komentar di masing-masing bagian implementasi).
// ============================================================

enum class GrindState {
    IDLE,
    VALIDATING,
    STARTING,
    WAIT_FLOW_START,  // motor ON, menunggu flow >= threshold TERKONFIRMASI selama GRIND_LATENCY_CONFIRMATION_MS -- TIDAK ADA predictive stop di state ini, hanya stall timeout yang berlaku (lihat catatan safety di bawah)
    GRINDING,         // flow sudah confirmed, predictive stop aktif (motor_stop_target_weight_g dihitung real-time)
    WAIT_SETTLE,
    POST_PURGE,       // BARU -- getar buang sisa chute SETELAH settle, SEBELUM cek target/mulai pulse correction. Lihat catatan lengkap di config.h (GRIND_PURGE_PULSE_DURATION_MS dkk).
    PULSE_CORRECTION,
    COMPLETE,
    ABORT
};

enum class GrindResult {
    NONE,           // belum selesai / tidak relevan
    SUCCESS,        // dalam toleransi GRIND_ACCURACY_TOLERANCE_G
    INACCURATE,     // pulse attempts habis, masih di luar toleransi -- tapi AMAN
    ABORTED         // lihat abortReason() untuk detail
};

enum class AbortReason {
    NONE,
    // BUG DITEMUKAN & DIHAPUS lewat audit config/enum: "BLE_DISCONNECTED"
    // sempat ada di sini sebagai sisa nama dari firmware Tasmota+BLE
    // lama, TIDAK PERNAH dipakai di kode manapun (tidak ada
    // doAbort(AbortReason::BLE_DISCONNECTED) di manapun, tidak ada
    // switch statement yang menanganinya secara khusus) -- firmware
    // ini sama sekali tidak pakai BLE (lihat main.cpp), jadi alasan
    // abort ini secara struktural tidak mungkin pernah terjadi.
    // Dihapus untuk mencegah kebingungan siapa pun yang membaca enum
    // ini dan mengira BLE relevan untuk alur abort firmware ini.
    INVALID_WEIGHT,     // tidak ada sample sama sekali dari timbangan
    UNSTABLE_WEIGHT,    // ada sample, tapi flow belum ~0 saat mau catat tare (portafilter baru ditaruh, belum settle)
    HARD_OVERSHOOT,
    STALL,
    TIMEOUT,
    MOTOR_COMMAND_FAILED,   // motor ON gagal terkirim saat startGrind()/pulse
    MOTOR_OFF_FAILED        // motor OFF gagal terkirim -- SAFETY CRITICAL
};

class GrindController {
public:
    GrindController(WeightFilter* weightFilter, MotorController* motor, LatencyCalibrator* calibrator);

    // Mulai predictive grind ke targetDoseG (dose TAMBAHAN dari
    // kondisi berat saat ini -- tare otomatis dicatat dari berat saat
    // command ini dipanggil, sama semantik dengan LatencyCalibrator).
    // Return false kalau ditolak segera (berat belum stabil, sudah
    // ada grind berjalan, dst) -- dalam kasus ini state tidak berubah
    // dan tidak ada motor command dikirim sama sekali.
    //
    // TIDAK ADA LAGI gate "kalibrasi belum cukup" (regresi dihapus) --
    // grind bisa langsung dicoba begitu berat stabil. GRIND_LATENCY_TO_COAST_RATIO
    // default (1.0) dipakai apa adanya sampai dikalibrasi manual (lihat README).
    bool startGrind(float targetDoseG);

    // Dipanggil tiap loop() -- HANYA cek kondisi berbasis waktu murni
    // (timeout, stall, window konfirmasi latency) yang tidak bisa
    // menunggu sample berikutnya.
    void update();

    // Dipanggil main.cpp SEKALI PER SAMPLE VALID (setelah
    // weightFilter.pushRawSample() true).
    void onWeightSample(float rawWeightG, unsigned long sampleTimestampMs);

    // Paksa abort dari luar (mis. kondisi eksternal terdeteksi di
    // main.cpp, di luar alur onWeightSample normal).
    void forceAbort(AbortReason reason);

    // ------------------------------------------------------------
    // Setter parameter yang bisa dikonfigurasi dari UI Settings
    // (Tolerance & Max Pulses). NILAI DI-SNAPSHOT SAAT startGrind()
    // DIPANGGIL (disalin ke accuracyToleranceG_/maxPulseAttempts_ di
    // konstruktor & startGrind()) -- perubahan lewat setter ini SAAT
    // grind sedang berjalan TIDAK mengubah parameter sesi yang sudah
    // dimulai, baru berlaku di sesi startGrind() BERIKUTNYA. Ini
    // mencegah operator mengubah Settings di tengah grinding secara
    // tidak sengaja mengubah target toleransi/max pulsa sesi yang
    // sedang berjalan (mis. race condition antara sentuhan UI dan
    // decision predictive-stop yang sedang berjalan real-time).
    //
    // Kalau tidak pernah dipanggil, default dari GRIND_ACCURACY_TOLERANCE_G
    // / GRIND_MAX_PULSE_ATTEMPTS di config.h tetap dipakai (constructor
    // menginisialisasi accuracyToleranceG_/maxPulseAttempts_ dari
    // situ) -- jadi behavior lama (sebelum UI Settings tersambung)
    // tidak berubah untuk siapa pun yang belum menyentuh Settings.
    void setAccuracyToleranceG(float toleranceG) { pendingAccuracyToleranceG_ = toleranceG; }
    void setMaxPulseAttempts(int maxPulses) { pendingMaxPulseAttempts_ = maxPulses; }
    // BARU -- Settle Time, sesuai kesepakatan sebelumnya: SATU setting
    // untuk GRIND_SCALE_PRECISION_SETTLING_TIME_MS, dipakai di 2 tempat
    // (WAIT_SETTLE setelah predictive-stop, DAN settle antar pulsa di
    // evaluatePulseProgress() -- lihat grind_controller.cpp). Pola
    // snapshot-at-startGrind() SAMA PERSIS dengan tolerance/max pulses
    // di atas -- alasan sama: mencegah perubahan Settings di tengah
    // grinding mengubah timing sesi yang sedang berjalan.
    void setSettlingTimeMs(unsigned long settlingMs) { pendingSettlingTimeMs_ = settlingMs; }
    // BARU -- Coast Ratio (GRIND_LATENCY_TO_COAST_RATIO), disepakati
    // eksplisit setelah investigasi overshoot 18g (lihat riwayat
    // diskusi): model prediktif motorStopTargetWeightG_ = flow_rate *
    // (latency_ms * ratio) -- ratio SEBELUMNYA konstanta tetap 1.0f di
    // config.h, sekarang bisa dituning dari UI Settings tanpa compile
    // ulang tiap coba angka. Pola snapshot-at-startGrind() SAMA
    // PERSIS dengan parameter lain di atas.
    void setCoastRatio(float ratio) { pendingCoastRatio_ = ratio; }
    // BARU -- Confirmation Window (GRIND_LATENCY_CONFIRMATION_MS),
    // disepakati eksplisit setelah observasi: gumpalan sisa chute
    // bisa terdorong jatuh di AWAL grinding, ikut lolos window
    // konfirmasi 500ms yang lama seolah itu flow kopi sungguhan yang
    // sudah stabil -- mencemari grind_latency_ms yang jadi basis
    // Coast Ratio. Range 300-2000ms disepakati (batas atas jauh di
    // bawah GRIND_STALL_TIMEOUT_MS 5000ms, supaya window ini naik
    // tidak sampai bikin grind keburu STALL sebelum sempat confirmed).
    void setConfirmationWindowMs(unsigned long windowMs) { pendingConfirmationWindowMs_ = windowMs; }
    // BARU -- POST_PURGE enable/pulse count, disepakati eksplisit
    // (lihat riwayat diskusi & catatan lengkap di config.h). Durasi/
    // jeda TIAP pulsa TIDAK disetting (hardcode di config.h) --
    // keputusan eksplisit untuk versi pertama fitur ini.
    void setPostPurgeEnabled(bool enabled) { pendingPostPurgeEnabled_ = enabled; }
    void setPostPurgePulseCount(int count) { pendingPostPurgePulseCount_ = count; }

    // Getter parameter EFEKTIF (yang sedang/terakhir dipakai sesi
    // grind, BUKAN pending value dari setter di atas yang belum
    // di-snapshot) -- dipakai UI Settings untuk menampilkan nilai yang
    // benar-benar aktif, bukan sekadar apa yang baru diketik operator.
    float accuracyToleranceG() const { return accuracyToleranceG_; }
    int maxPulseAttempts() const { return maxPulseAttempts_; }
    unsigned long settlingTimeMs() const { return settlingTimeMs_; }  // BARU
    float coastRatio() const { return coastRatio_; }  // BARU
    unsigned long confirmationWindowMs() const { return confirmationWindowMs_; }  // BARU
    bool postPurgeEnabled() const { return postPurgeEnabled_; }  // BARU
    int postPurgePulseCount() const { return postPurgePulseCount_; }  // BARU

    // ------------------------------------------------------------
    // Getter publik -- dibaca main.cpp untuk sync ke UI/command
    // diagnostik.
    // ------------------------------------------------------------
    GrindState state() const { return state_; }
    GrindResult result() const { return result_; }
    AbortReason abortReason() const { return abortReason_; }

    // KONFIRMASI dari 2 audit independen -- lihat catatan lengkap di
    // deklarasi motorSafetyLockout_ (private, di bawah). UI HARUS cek
    // ini (bukan cuma abortReason()==MOTOR_OFF_FAILED) sebelum
    // mengizinkan tombol "New Grind"/navigasi apa pun yang mengarah ke
    // startGrind() lagi -- lihat main.cpp/ui_screen_manager.cpp.
    bool isMotorSafetyLockedOut() const { return motorSafetyLockout_; }

    float targetDoseG() const { return targetDoseG_; }
    float targetAbsoluteG() const { return targetAbsoluteG_; }
    float startWeightG() const { return startWeightG_; }
    float currentWeightG() const;       // dari weightFilter_->latestWeight()
    float currentFlowGps() const;       // dari weightFilter_->computeFlowRate(), NAN kalau belum valid
    float finalWeightG() const { return finalWeightG_; }
    float finalErrorG() const { return finalWeightG_ - targetAbsoluteG_; }
    int pulseAttempts() const { return pulseAttempts_; }
    unsigned long grindDurationMs() const;  // sejak startGrind() dipanggil

    // Diagnostik model real-time -- dibaca command Serial 'gs' supaya
    // operator bisa lihat PERSIS angka yang dipakai keputusan stop
    // sesi grind yang sedang/baru saja berjalan.
    bool flowStartConfirmed() const { return flowStartConfirmed_; }
    unsigned long grindLatencyMs() const { return grindLatencyMs_; }
    float motorStopTargetWeightG() const { return motorStopTargetWeightG_; }
    float sessionPulseFlowGps() const { return sessionPulseFlowGps_; }

    // Estimasi latency motor -- HANYA berarti untuk UI/diagnostik,
    // BUKAN dipakai dalam algoritma predictive-stop (lihat motor_controller.h,
    // ini HTTP-style RTT/GPIO write time, bukan grind_latency_ms).
    float lastMotorRttMs() const { return lastMotorRttMs_; }

private:
    WeightFilter* weightFilter_;
    MotorController* motor_;
    LatencyCalibrator* calibrator_;  // dipertahankan untuk kompatibilitas API -- TIDAK dipakai di jalur keputusan stop, lihat catatan di atas

    GrindState state_;
    GrindResult result_;
    AbortReason abortReason_;

    // KONFIRMASI dari 2 audit independen: MOTOR_OFF_FAILED adalah
    // AbortReason paling kritis secara keselamatan (motor ON gagal
    // dimatikan lewat software), TAPI arsitektur ABORT sebelumnya
    // bukan lockout permanen -- startGrind() (lihat kondisi awal di
    // .cpp) mengizinkan state_==ABORT sebagai starting state yang
    // valid, jadi operator/UI masih bisa memulai sesi grind baru
    // setelah MOTOR_OFF_FAILED terjadi, walau motor mungkin masih
    // menyala secara fisik (relay welded/rusak -- kondisi yang TIDAK
    // BISA dideteksi GpioMotorController::stop() sekarang, karena
    // digitalWrite() selalu melapor sukses terlepas kondisi kontak
    // relay fisik; lihat motor_controller.cpp). motorSafetyLockout_
    // menutup celah itu: begitu true, startGrind() menolak SELAMANYA
    // sampai reboot fisik -- operator harus memutus daya grinder
    // secara fisik, bukan sekadar tekan tombol di UI. Sengaja RAM-only
    // (bukan NVS): masalah relay fisik yang rusak butuh intervensi
    // fisik (cabut steker/ganti relay), bukan "reset" lewat reboot
    // software -- kalau di-reset otomatis lewat reboot, operator bisa
    // mengira masalah sudah selesai padahal relay masih rusak secara
    // fisik.
    bool motorSafetyLockout_;

    float targetDoseG_;
    float targetAbsoluteG_;
    float startWeightG_;
    float finalWeightG_;

    unsigned long grindStartMs_;      // timestamp startGrind() dipanggil -- basis MAX_DURATION & durasi laporan
    unsigned long motorStartedMs_;    // timestamp motor ON dikonfirmasi -- basis grace period stall & basis grind_latency_ms
    unsigned long motorStoppedMs_;    // timestamp motor OFF dikonfirmasi -- basis settle timer (WAIT_SETTLE/pasca-pulse)
    unsigned long lastFlowAboveThresholdMs_;  // basis reset timer stall

    // --- Model real-time (VERSI 2) ---
    // Confirmation window flow-start (fix per review): candidate
    // dicatat begitu SATU sample >= threshold terlihat pertama kali
    // sejak motor ON/sejak reset terakhir. flowStartConfirmed_ baru
    // true kalau flow TETAP >= threshold sampai
    // (candidateFlowStartMs_ + GRIND_LATENCY_CONFIRMATION_MS) tercapai
    // TANPA ada sample di bawah threshold di antaranya -- kalau ada
    // sample di bawah threshold sebelum window selesai,
    // candidateFlowStartMs_ di-reset (unsigned long 0 dipakai sebagai
    // "tidak ada candidate aktif").
    unsigned long candidateFlowStartMs_;
    bool flowStartConfirmed_;              // sudah terkonfirmasi PENUH (window 500ms terpenuhi tanpa putus)?
    unsigned long grindLatencyMs_;         // T_onset: motorStartedMs_ -> candidateFlowStartMs_ yang BERHASIL dikonfirmasi
    float motorStopTargetWeightG_;         // hasil hitung flow_now * coast_time -- HANYA valid & dipakai setelah flowStartConfirmed_ (lihat evaluateGrindProgress())
    float sessionPulseFlowGps_;            // P95 flow SESI INI (window GRIND_PULSE_P95_WINDOW_MS sebelum predictive stop), dihitung sekali, dipakai semua pulsa

    int pulseAttempts_;
    float lastMotorRttMs_;

    // --- Parameter yang bisa dikonfigurasi dari UI Settings ---
    // accuracyToleranceG_/maxPulseAttempts_: nilai EFEKTIF yang
    // dipakai algoritma (diinisialisasi dari config.h di konstruktor,
    // di-snapshot ulang dari pending*_ di setiap startGrind()).
    // pendingAccuracyToleranceG_/pendingMaxPulseAttempts_: nilai yang
    // ditulis lewat setter (mis. dari UI Settings) tapi BELUM
    // di-snapshot ke sesi aktif -- lihat komentar setter di atas untuk
    // alasan snapshot-at-start ini.
    float accuracyToleranceG_;
    int maxPulseAttempts_;
    float pendingAccuracyToleranceG_;
    int pendingMaxPulseAttempts_;
    // BARU -- settlingTimeMs_/pendingSettlingTimeMs_, pola SAMA PERSIS
    // dengan accuracyToleranceG_/maxPulseAttempts_ di atas. Dipakai di
    // 2 tempat: WAIT_SETTLE (setelah predictive-stop) dan settle antar
    // pulsa di evaluatePulseProgress() -- lihat grind_controller.cpp.
    unsigned long settlingTimeMs_;
    unsigned long pendingSettlingTimeMs_;
    // BARU -- coastRatio_/pendingCoastRatio_, pola SAMA PERSIS. Dipakai
    // di evaluateFlowStartConfirmation() (inisialisasi awal
    // motorStopTargetWeightG_) DAN evaluateGrindProgress() (update
    // real-time tiap sample) -- lihat grind_controller.cpp.
    float coastRatio_;
    float pendingCoastRatio_;
    // BARU -- confirmationWindowMs_/pendingConfirmationWindowMs_, pola
    // sama. Dipakai di evaluateFlowStartConfirmation() (lihat
    // grind_controller.cpp).
    unsigned long confirmationWindowMs_;
    unsigned long pendingConfirmationWindowMs_;
    // BARU -- postPurgeEnabled_/postPurgePulseCount_, pola sama
    // (snapshot-at-startGrind). postPurgePulsesRemaining_ BUKAN
    // setting -- ini counter RUNTIME (di-reset tiap kali masuk
    // POST_PURGE, dikurangi tiap pulsa selesai, lihat
    // grind_controller.cpp).
    bool postPurgeEnabled_;
    bool pendingPostPurgeEnabled_;
    int postPurgePulseCount_;
    int pendingPostPurgePulseCount_;
    int postPurgePulsesRemaining_;

    void transitionTo(GrindState newState);
    void doAbort(AbortReason reason);
    void checkStall(unsigned long nowMs);
    void checkTimeout(unsigned long nowMs);
    void checkHardOvershoot(float currentWeight);
    void evaluateFlowStartConfirmation(unsigned long sampleTimestampMs);
    void evaluateGrindProgress(unsigned long sampleTimestampMs);
    void evaluatePulseProgress(unsigned long sampleTimestampMs);
    void startPulse(unsigned long nowMs);
    // BARU -- POST_PURGE, lihat catatan lengkap di config.h
    // (GRIND_PURGE_PULSE_DURATION_MS dkk) dan grind_controller.cpp.
    void startPostPurgePulse();
    void evaluatePostPurgeProgress(unsigned long sampleTimestampMs);
    void finishPostPurgeAndDecide();
    void finishAsComplete();

    // Kirim motor OFF dengan retry sekali di level ini. CATATAN:
    // motor_->stop() untuk GpioMotorController (implementasi final)
    // TIDAK punya retry internal sendiri (1x digitalWrite() murni,
    // beda dari TasmotaMotorController lama yang HTTP-based dan
    // memang retry internal) -- WORST CASE 3x percobaan TOTAL
    // (1x panggilan awal + 1x retry di sini + 1x lagi di doAbort()),
    // BUKAN 4x seperti komentar lama di sini pernah menyiratkan.
    // Lihat komentar lengkap di implementasi stopMotorOrAbort()
    // (grind_controller.cpp) untuk rincian penuh & klarifikasi setelah
    // sempat salah dihitung dalam review external. Kalau TETAP gagal,
    // panggil doAbort(MOTOR_OFF_FAILED) dan return false -- caller
    // HARUS berhenti melanjutkan alur normal (jangan transisi ke
    // WAIT_SETTLE) kalau ini return false.
    bool stopMotorOrAbort();
};
