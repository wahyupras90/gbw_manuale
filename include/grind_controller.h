#pragma once

#include <Arduino.h>
#include "weight_filter.h"
#include "motor_controller.h"
#include "config.h"

// ============================================================
// GrindController -- Early Stop G grind-by-weight controller
// ============================================================
// Motor berhenti earlyStopG_ gram sebelum target, sisa ditutup
// coast natural + post-purge + stepped pulse correction.
//
// State machine: WAIT_STABLE → STARTING → GRINDING → WAIT_SETTLE
// → POST_PURGE → PULSE_CORRECTION → COMPLETE
//
// Stop decision: motor OFF saat berat >= targetAbsoluteG_ - earlyStopG_.
// Sisa gap ditutup coast natural + post-purge + stepped pulse correction.
//
// Pulse correction: stepped fixed duration (tidak pakai P95 flow):
// (P95 flow dihapus sejak v1.0.42 karena flow saat pulse berbeda dari
// flow saat grinding -- feedback berat aktual setelah tiap pulse lebih
// andal): error >0.5g=100ms, >0.3g=60ms, ≤0.3g=40ms.
//
// STALL DETECTION, HARD OVERSHOOT, MAX DURATION, MOTOR OFF FAILURE
// SAFETY tetap berlaku penuh.
// ============================================================

enum class GrindState {
    IDLE,
    VALIDATING,
    WAIT_STABLE,      // tunggu timbangan stabil sebelum auto-tare & motor ON
    STARTING,
    GRINDING,         // motor ON, cek fixed stop percentage setiap sample
    WAIT_SETTLE,
    POST_PURGE,       // getar buang sisa chute setelah settle, sebelum evaluasi
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
    GrindController(WeightFilter* weightFilter, MotorController* motor);

    // Mulai grind ke targetDoseG (dose TAMBAHAN dari berat saat ini --
    // tare otomatis dicatat). Return false kalau ditolak segera
    // (berat belum stabil, sudah ada grind berjalan, dst).
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
    // Early Stop G -- motor berhenti saat berat >= startWeight + dose - earlyStopG_
    // Menggantikan model predictive (latency × flow × coastRatio).
    // Range 0.5–10.0g, default 2.0g. Motor berhenti earlyStopG_ gram sebelum target.
    void setEarlyStopG(float g) {
        if (!isfinite(g) || g < 0.5f || g > 10.0f) g = 2.0f;
        pendingEarlyStopG_ = g;
    }
    // POST_PURGE enable/pulse count
    void setPostPurgeEnabled(bool enabled) { pendingPostPurgeEnabled_ = enabled; }
    void setPostPurgePulseCount(int count) { pendingPostPurgePulseCount_ = count; }
    // BARU -- stability threshold untuk WAIT_STABLE state (pre-grind).
    // Pending pattern sama seperti parameter lain -- berlaku mulai
    // startGrind() berikutnya, bukan langsung mengubah sesi aktif.
    void setStabilityThresholdG(float threshG) { pendingStabilityThresholdG_ = threshG; }

    // Getter parameter EFEKTIF (yang sedang/terakhir dipakai sesi
    // grind, BUKAN pending value dari setter di atas yang belum
    // di-snapshot) -- dipakai UI Settings untuk menampilkan nilai yang
    // benar-benar aktif, bukan sekadar apa yang baru diketik operator.
    float accuracyToleranceG() const { return accuracyToleranceG_; }
    int maxPulseAttempts() const { return maxPulseAttempts_; }
    unsigned long settlingTimeMs() const { return settlingTimeMs_; }
    bool postPurgeEnabled() const { return postPurgeEnabled_; }
    int postPurgePulseCount() const { return postPurgePulseCount_; }
    float stabilityThresholdG() const { return stabilityThresholdG_; }

    // Last grind data -- dibaca main.cpp untuk disimpan ke NVS dan
    // ditampilkan di Debug screen section LAST GRIND.
    float lastGrindWeightAtMotorStop() const { return lastGrindWeightAtMotorStop_; }
    float lastGrindWeightAtMotorOff100ms() const { return lastGrindWeightAtMotorOff100ms_; }
    float lastGrindWeightAtMotorOff300ms() const { return lastGrindWeightAtMotorOff300ms_; }
    float lastGrindEarlyStopG() const { return lastGrindEarlyStopG_; }
    float lastGrindFinalWeightG() const { return lastGrindFinalWeightG_; }
    int lastGrindPulseCount() const { return lastGrindPulseCount_; }

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

    float earlyStopG() const { return earlyStopG_; }
    float lastMotorRttMs() const { return lastMotorRttMs_; }

private:
    WeightFilter* weightFilter_;
    MotorController* motor_;

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

    unsigned long grindStartMs_;
    unsigned long motorStartedMs_;
    unsigned long motorStoppedMs_;


    int pulseAttempts_;
    float lastMotorRttMs_;

    float accuracyToleranceG_;
    int maxPulseAttempts_;
    float pendingAccuracyToleranceG_;
    int pendingMaxPulseAttempts_;
    unsigned long settlingTimeMs_;
    unsigned long pendingSettlingTimeMs_;
    float earlyStopG_;
    float pendingEarlyStopG_;
    bool postPurgeEnabled_;
    bool pendingPostPurgeEnabled_;
    int postPurgePulseCount_;
    int pendingPostPurgePulseCount_;
    int postPurgePulsesRemaining_;
    unsigned long purgeMotorOnMs_;
    unsigned long pulseMotorOnMs_;
    unsigned long pulseDurationMs_;

    float stabilityThresholdG_;
    float pendingStabilityThresholdG_;
    unsigned long waitStableStartMs_;
    unsigned long waitStableOkSinceMs_;
    float waitStableLastWeight_;

    float weightAtMotorStop_;
    float weightAfterSettle_;
    float weightAtMotorOff100ms_;   // berat +100ms setelah relay OFF (in-memory, tidak persist NVS)
    float weightAtMotorOff300ms_;   // berat +300ms setelah relay OFF (in-memory, tidak persist NVS)
    float lastGrindWeightAtMotorStop_;
    float lastGrindWeightAtMotorOff100ms_;  // in-memory only, reset NAN saat boot
    float lastGrindWeightAtMotorOff300ms_;  // in-memory only, reset NAN saat boot
    float lastGrindEarlyStopG_;
    float lastGrindFinalWeightG_;
    int lastGrindPulseCount_;
    void transitionTo(GrindState newState);
    void startMotorAndBeginGrind();
    void doAbort(AbortReason reason);
    void checkStall(unsigned long nowMs);
    void checkTimeout(unsigned long nowMs);
    void checkHardOvershoot(float currentWeight);
    void evaluateGrindProgress(unsigned long sampleTimestampMs);
    void evaluatePulseProgress(unsigned long sampleTimestampMs);
    void startPulse(unsigned long nowMs);
    void startPostPurgePulse();
    void evaluatePostPurgeProgress(unsigned long sampleTimestampMs);
    void finishPostPurgeAndDecide();
    void finishAsComplete();
    bool stopMotorOrAbort();
};
