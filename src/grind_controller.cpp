#include "grind_controller.h"
#include <math.h>
#include <algorithm>
#include <cstdio>

// saveCheckpoint() didefinisikan di main.cpp (namespace NVS "gbwdiag")
// -- dipakai di seluruh file ini untuk mencatat titik eksekusi ke
// flash agar bisa dibaca lewat Debug screen setelah reboot/freeze.
extern void saveCheckpoint(const char* label);

// Kapasitas riwayat flow rate per sesi -- dipakai untuk hitung P95
// pulsa (lihat startGrind()/onWeightSample()). Ukuran tetap di stack,
// bukan heap growable -- cukup untuk sesi grind normal (~5-20 detik
// pada laju sample HX711 ~10-80Hz, direkam tiap onWeightSample() saat
// GRINDING, jadi ratusan entri paling banyak; dibuang FIFO (ring
// buffer) kalau penuh -- lihat pushFlowSample()).
struct FlowHistoryEntry {
    float flowGps;
    unsigned long timestampMs;
};
static const size_t GRIND_FLOW_HISTORY_CAPACITY = 512;
static FlowHistoryEntry g_flowHistoryBuf[GRIND_FLOW_HISTORY_CAPACITY];
static size_t g_flowHistoryHead = 0;   // index tulis berikutnya (ring buffer)
static size_t g_flowHistoryCount = 0;  // jumlah entri terisi (maks CAPACITY)

static void resetFlowHistory() {
    g_flowHistoryHead = 0;
    g_flowHistoryCount = 0;
}

static void pushFlowSample(float flowGps, unsigned long timestampMs) {
    g_flowHistoryBuf[g_flowHistoryHead] = {flowGps, timestampMs};
    g_flowHistoryHead = (g_flowHistoryHead + 1) % GRIND_FLOW_HISTORY_CAPACITY;
    if (g_flowHistoryCount < GRIND_FLOW_HISTORY_CAPACITY) {
        g_flowHistoryCount++;
    }
    // Ring buffer -- kalau penuh, entri TERTUA ditimpa (bukan
    // dibuang begitu saja tanpa slot baru) -- tapi karena kita cuma
    // butuh window GRIND_PULSE_P95_WINDOW_MS (2500ms) TERAKHIR
    // sebelum predictive stop, entri lama di luar window itu memang
    // tidak relevan lagi, jadi perilaku timpa ini aman.
}

// P95 dari window GRIND_PULSE_P95_WINDOW_MS (2500ms) TERAKHIR SEBELUM
// nowMs (per review, FIX dari implementasi awal yang salah pakai
// SELURUH histori sesi tanpa batas window -- window filtering ini
// yang membuat GRIND_PULSE_P95_WINDOW_MS benar-benar berfungsi).
// nowMs biasanya waktu predictive stop terjadi (dipanggil dari
// WAIT_SETTLE, lihat onWeightSample()). Return NAN kalau tidak ada
// sample dalam window itu.
//
// HEAP-FREE (per review, DIPERBAIKI dari draft sebelumnya): fungsi ini
// SEBELUMNYA memakai std::vector<float> untuk `inWindow` -- itu
// KONTRADIKSI dengan komentar buffer penyimpanan di atas
// (g_flowHistoryBuf) yang menegaskan "bukan heap growable", karena
// fungsi PEMROSESAN ini (bukan buffer penyimpanannya) tetap melakukan
// alokasi heap (std::vector::reserve()/push_back()) tiap kali dipanggil.
// Dampaknya kecil dalam praktik (fungsi ini hanya dipanggil SEKALI per
// sesi grind, saat transisi ke PULSE_CORRECTION, bukan di jalur
// real-time onWeightSample() yang dipanggil berkali-kali per detik),
// TAPI untuk konsistensi penuh dengan prinsip "tidak ada heap alloc di
// jalur grinding" yang didokumentasikan di atas, SEKARANG diganti
// array fixed (ukuran sama dengan GRIND_FLOW_HISTORY_CAPACITY, di
// stack) + std::sort langsung di array C-style -- tidak ada alokasi
// heap sama sekali di fungsi ini.
static float computeSessionP95(unsigned long nowMs) {
    if (g_flowHistoryCount == 0) {
        return NAN;
    }

    unsigned long windowStart = (nowMs > GRIND_PULSE_P95_WINDOW_MS) ? (nowMs - GRIND_PULSE_P95_WINDOW_MS) : 0;

    static float inWindow[GRIND_FLOW_HISTORY_CAPACITY];  // static: dialokasikan sekali, bukan per-panggilan (menghindari re-zeroing stack besar tiap panggil, walau fungsi ini jarang dipanggil)
    size_t inWindowCount = 0;
    for (size_t i = 0; i < g_flowHistoryCount; i++) {
        // g_flowHistoryHead menunjuk slot TULIS berikutnya -- entri
        // terisi berada di [head-count, head) modulo CAPACITY.
        size_t idx = (g_flowHistoryHead + GRIND_FLOW_HISTORY_CAPACITY - g_flowHistoryCount + i) % GRIND_FLOW_HISTORY_CAPACITY;
        if (g_flowHistoryBuf[idx].timestampMs >= windowStart && g_flowHistoryBuf[idx].timestampMs <= nowMs) {
            inWindow[inWindowCount++] = g_flowHistoryBuf[idx].flowGps;
        }
    }

    if (inWindowCount == 0) {
        return NAN;
    }

    std::sort(inWindow, inWindow + inWindowCount);
    size_t idx = (size_t)(0.95f * (inWindowCount - 1));
    return inWindow[idx];
}

GrindController::GrindController(WeightFilter* weightFilter, MotorController* motor, LatencyCalibrator* calibrator)
    : weightFilter_(weightFilter), motor_(motor), calibrator_(calibrator),
      state_(GrindState::IDLE), result_(GrindResult::NONE), abortReason_(AbortReason::NONE),
      motorSafetyLockout_(false),
      targetDoseG_(0), targetAbsoluteG_(0), startWeightG_(0), finalWeightG_(0),
      grindStartMs_(0), motorStartedMs_(0), motorStoppedMs_(0),
      pulseAttempts_(0), lastMotorRttMs_(0),
      accuracyToleranceG_(GRIND_ACCURACY_TOLERANCE_G), maxPulseAttempts_(GRIND_MAX_PULSE_ATTEMPTS),
      pendingAccuracyToleranceG_(GRIND_ACCURACY_TOLERANCE_G), pendingMaxPulseAttempts_(GRIND_MAX_PULSE_ATTEMPTS),
      settlingTimeMs_(GRIND_SCALE_PRECISION_SETTLING_TIME_MS), pendingSettlingTimeMs_(GRIND_SCALE_PRECISION_SETTLING_TIME_MS),
      earlyStopG_(2.0f), pendingEarlyStopG_(2.0f),
      postPurgeEnabled_(false), pendingPostPurgeEnabled_(false),
      postPurgePulseCount_(GRIND_POST_PURGE_PULSE_COUNT_DEFAULT), pendingPostPurgePulseCount_(GRIND_POST_PURGE_PULSE_COUNT_DEFAULT),
      postPurgePulsesRemaining_(0), purgeMotorOnMs_(0),
      pulseMotorOnMs_(0), pulseDurationMs_(0),
      stabilityThresholdG_(0.3f), pendingStabilityThresholdG_(0.3f),
      waitStableStartMs_(0), waitStableOkSinceMs_(0), waitStableLastWeight_(NAN),
      weightAtMotorStop_(NAN), weightAfterSettle_(NAN),
      weightAtMotorOff100ms_(NAN), weightAtMotorOff300ms_(NAN),
      lastGrindWeightAtMotorStop_(NAN), lastGrindActualCoast_(NAN),
      lastGrindWeightAtMotorOff100ms_(NAN), lastGrindWeightAtMotorOff300ms_(NAN),
      lastGrindEarlyStopG_(NAN), lastGrindFinalWeightG_(NAN), lastGrindPulseCount_(0) {}

// ------------------------------------------------------------
// Getter kecil
// ------------------------------------------------------------
float GrindController::currentWeightG() const {
    return weightFilter_->latestWeight();
}

float GrindController::currentFlowGps() const {
    FlowRateResult f = weightFilter_->computeFlowRate();
    return f.valid ? f.flowRateGps : NAN;
}

unsigned long GrindController::grindDurationMs() const {
    if (grindStartMs_ == 0) return 0;
    return millis() - grindStartMs_;
}

// ------------------------------------------------------------
// State transition helper
// ------------------------------------------------------------
void GrindController::transitionTo(GrindState newState) {
    Serial.printf("[GRIND] state: -> %d\n", (int)newState);
    state_ = newState;
}

void GrindController::doAbort(AbortReason reason) {
    // Force-off SEGERA -- tidak peduli hasil sukses/gagal, ini abort,
    // motor tidak boleh menyala lebih lama dari yang seharusnya.
    MotorResult r = motor_->stop();
    lastMotorRttMs_ = r.rttMs;
    if (!r.success) {
        Serial.println("[GRIND] PERINGATAN -- motor OFF saat abort TIDAK terkonfirmasi berhasil terkirim!");
    }

    abortReason_ = reason;
    result_ = GrindResult::ABORTED;
    finalWeightG_ = weightFilter_->hasSample() ? weightFilter_->latestWeight() : NAN;

    // KONFIRMASI dari 2 audit independen: MOTOR_OFF_FAILED WAJIB
    // mengunci sistem secara permanen (sampai reboot fisik), TIDAK
    // boleh sekadar masuk state ABORT biasa yang bisa di-restart lewat
    // UI. Lihat catatan lengkap di deklarasi motorSafetyLockout_
    // (grind_controller.h). Sekali true, TIDAK PERNAH di-set false
    // lagi oleh kode manapun -- keluar dari lockout ini HANYA lewat
    // reboot fisik (power-cycle), bukan lewat tombol UI/command Serial
    // apa pun.
    if (reason == AbortReason::MOTOR_OFF_FAILED) {
        motorSafetyLockout_ = true;
        Serial.println("[GRIND] SAFETY LOCKOUT -- motor OFF gagal dikonfirmasi. Sesi grind baru DITOLAK sampai daya grinder diputus & dihidupkan ulang secara fisik.");
    }

    Serial.printf("[GRIND] ABORT -- reason=%d, berat saat ini=%.2fg, target=%.2fg\n",
                  (int)reason, finalWeightG_, targetAbsoluteG_);

    // Checkpoint NVS -- label encode reason number supaya bisa dibedakan
    // (STALL vs HARD_OVERSHOOT vs MOTOR_OFF_FAILED dst) dari Debug screen.
    char abortLabel[15];
    snprintf(abortLabel, sizeof(abortLabel), "abort_r%d", (int)reason);
    saveCheckpoint(abortLabel);

    transitionTo(GrindState::ABORT);
}

void GrindController::forceAbort(AbortReason reason) {
    if (state_ == GrindState::IDLE || state_ == GrindState::COMPLETE || state_ == GrindState::ABORT) {
        return;  // tidak ada apa-apa yang sedang berjalan untuk di-abort
    }
    doAbort(reason);
}

// ------------------------------------------------------------
// startGrind()
// ------------------------------------------------------------
bool GrindController::startGrind(float targetDoseG) {
    if (motorSafetyLockout_) {
        Serial.println("[GRIND] TOLAK -- SAFETY LOCKOUT aktif. Putus daya grinder secara fisik, lalu hidupkan ulang.");
        return false;
    }

    if (state_ != GrindState::IDLE && state_ != GrindState::COMPLETE && state_ != GrindState::ABORT) {
        Serial.println("[GRIND] Tolak -- grind lain sedang berjalan.");
        return false;
    }

    transitionTo(GrindState::VALIDATING);

    if (!weightFilter_->hasSample()) {
        Serial.println("[GRIND] TOLAK -- belum ada sample berat valid dari timbangan.");
        abortReason_ = AbortReason::INVALID_WEIGHT;
        result_ = GrindResult::ABORTED;
        transitionTo(GrindState::ABORT);
        return false;
    }

    // Guard OLS window -- pastikan weightFilter sudah punya cukup data
    // sebelum motor nyala. Tanpa ini, flow detection di awal grind
    // tidak akurat dan latency terukur tidak konsisten.
    FlowRateResult preGrindFlow = weightFilter_->computeFlowRate();
    if (!preGrindFlow.valid) {
        Serial.println("[GRIND] TOLAK -- window OLS belum cukup terisi. Tunggu beberapa detik, lalu coba ulang.");
        abortReason_ = AbortReason::UNSTABLE_WEIGHT;
        result_ = GrindResult::ABORTED;
        transitionTo(GrindState::ABORT);
        return false;
    }
    if (fabsf(preGrindFlow.flowRateGps) > GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
        Serial.printf("[GRIND] TOLAK -- berat belum stabil (flow=%.2f gps). Tunggu timbangan settle, lalu coba lagi.\n",
                      preGrindFlow.flowRateGps);
        abortReason_ = AbortReason::UNSTABLE_WEIGHT;
        result_ = GrindResult::ABORTED;
        transitionTo(GrindState::ABORT);
        return false;
    }

    targetDoseG_ = targetDoseG;
    startWeightG_ = weightFilter_->latestWeight();
    targetAbsoluteG_ = startWeightG_ + targetDoseG_;
    finalWeightG_ = NAN;
    pulseAttempts_ = 0;
    result_ = GrindResult::NONE;
    abortReason_ = AbortReason::NONE;
    grindStartMs_ = millis();
    motorStartedMs_ = 0;
    motorStoppedMs_ = 0;

    // Snapshot parameter dari pending*_
    accuracyToleranceG_  = pendingAccuracyToleranceG_;
    maxPulseAttempts_    = pendingMaxPulseAttempts_;
    settlingTimeMs_      = pendingSettlingTimeMs_;
    earlyStopG_          = pendingEarlyStopG_;
    postPurgeEnabled_    = pendingPostPurgeEnabled_;
    postPurgePulseCount_ = pendingPostPurgePulseCount_;
    postPurgePulsesRemaining_ = 0;
    purgeMotorOnMs_  = 0;
    pulseMotorOnMs_  = 0;
    pulseDurationMs_ = 0;
    stabilityThresholdG_ = pendingStabilityThresholdG_;
    waitStableStartMs_   = millis();
    waitStableOkSinceMs_ = 0;
    waitStableLastWeight_ = NAN;

    weightAtMotorStop_      = NAN;
    weightAfterSettle_      = NAN;
    weightAtMotorOff100ms_  = NAN;
    weightAtMotorOff300ms_  = NAN;

    resetFlowHistory();
    sessionPulseFlowGps_ = NAN;

    Serial.printf("[GRIND] Mulai -- dose=%.2fg tare=%.2fg target=%.2fg (earlyStop=%.2fg, tolerance=%.3fg, max_pulses=%d, settle=%lums)\n",
                  targetDoseG_, startWeightG_, targetAbsoluteG_, earlyStopG_,
                  accuracyToleranceG_, maxPulseAttempts_, settlingTimeMs_);

    transitionTo(GrindState::WAIT_STABLE);
    return true;
}

void GrindController::startMotorAndBeginGrind() {
    MotorResult r = motor_->start();
    lastMotorRttMs_ = r.rttMs;
    if (!r.success) {
        Serial.println("[GRIND] Motor ON gagal terkirim -- force-off sebagai jaga-jaga.");
        doAbort(AbortReason::MOTOR_COMMAND_FAILED);
        return;
    }

    motorStartedMs_ = r.commandSentMs;
    saveCheckpoint("grind_start");
    transitionTo(GrindState::STARTING);
    transitionTo(GrindState::GRINDING);
}

// ------------------------------------------------------------
// update()
// ------------------------------------------------------------
void GrindController::update() {
    if (state_ == GrindState::IDLE || state_ == GrindState::COMPLETE || state_ == GrindState::ABORT) {
        return;
    }

    unsigned long nowMs = millis();
    checkTimeout(nowMs);
    if (state_ == GrindState::ABORT) return;

    if (state_ == GrindState::GRINDING) {
        checkStall(nowMs);
    }

    if (state_ == GrindState::POST_PURGE) {
        evaluatePostPurgeProgress(nowMs);
    } else if (state_ == GrindState::PULSE_CORRECTION) {
        evaluatePulseProgress(nowMs);
    }
}

void GrindController::checkTimeout(unsigned long nowMs) {
    if (grindStartMs_ == 0) return;
    if (nowMs - grindStartMs_ >= GRIND_MAX_DURATION_MS) {
        Serial.println("[GRIND] TIMEOUT -- durasi grind melebihi batas maksimum.");
        doAbort(AbortReason::TIMEOUT);
    }
}

void GrindController::checkStall(unsigned long nowMs) {
    if (motorStartedMs_ == 0) return;
    if (nowMs - motorStartedMs_ < GRIND_MOTOR_STARTUP_GRACE_MS) {
        return;
    }
    // Dengan fixed stop percentage, stall = motor sudah nyala lebih dari
    // GRIND_STALL_TIMEOUT_MS tapi berat belum mencapai stop threshold.
    // Cek sederhana: berat belum bergerak naik signifikan dari startWeightG_.
    float currentWeight = weightFilter_ ? weightFilter_->latestWeight() : startWeightG_;
    if ((currentWeight - startWeightG_) < 0.5f &&
        (nowMs - motorStartedMs_) >= GRIND_STALL_TIMEOUT_MS) {
        Serial.println("[GRIND] STALL -- berat tidak naik dalam batas waktu (beans habis/jalur macet?).");
        doAbort(AbortReason::STALL);
    }
}

void GrindController::checkHardOvershoot(float currentWeight) {
    if (currentWeight >= targetAbsoluteG_ + GRIND_HARD_OVERSHOOT_G) {
        Serial.printf("[GRIND] HARD OVERSHOOT -- berat %.2fg jauh melebihi target %.2fg (batas %.2fg).\n",
                      currentWeight, targetAbsoluteG_, GRIND_HARD_OVERSHOOT_G);
        doAbort(AbortReason::HARD_OVERSHOOT);
    }
}

// ------------------------------------------------------------
// stopMotorOrAbort()
//
// JUMLAH ATTEMPT WORST-CASE -- KLARIFIKASI PENTING (ditambahkan
// setelah kesalahpahaman soal ini dalam review external): worst case
// SEBENARNYA di firmware ini adalah 3x motor_->stop() (bukan 4 seperti
// disebut brief lama, ATAU 6 seperti sempat dihitung dalam satu
// review -- keduanya salah karena mengasumsikan motor_->stop() sendiri
// masih punya retry internal 2x seperti TasmotaMotorController lama).
//
// Rinciannya untuk GpioMotorController (implementasi FINAL yang
// dipakai firmware ini, lihat motor_controller.cpp):
//   motor_->stop() [panggilan #1, baris di bawah]
//     -> HANYA 1x digitalWrite(), TIDAK ADA retry internal (beda dari
//        TasmotaMotorController lama yang HTTP-based dan punya retry
//        sendiri -- GPIO digitalWrite() tidak punya failure mode
//        seperti HTTP timeout, jadi retry internal tidak relevan lagi)
//   motor_->stop() [panggilan #2, retry di GrindController]
//     -> 1x digitalWrite() lagi kalau panggilan #1 gagal (untuk GPIO,
//        "gagal" secara software nyaris mustahil terjadi -- r.success
//        SELALU true untuk GpioMotorController, lihat motor_controller.h
//        -- retry ini sebenarnya jaring pengaman untuk skenario masa
//        depan kalau MotorController lain yang BISA gagal dipakai lagi)
//   doAbort() -> motor_->stop() [panggilan #3]
//     -> 1x digitalWrite() lagi sebagai force-off terakhir
//
// TOTAL: 3x digitalWrite(), bukan 4 atau 6. Kalau MotorController lain
// (mis. HTTP-based lagi di masa depan) dipakai ulang dan punya retry
// internal sendiri, angka ini perlu dihitung ulang -- JANGAN
// mengasumsikan retry internal MotorController tanpa cek implementasi
// aktual yang dipakai (lihat main.cpp untuk implementasi yang benar-
// benar diinstansiasi saat ini).
// ------------------------------------------------------------
bool GrindController::stopMotorOrAbort() {
    MotorResult r = motor_->stop();
    lastMotorRttMs_ = r.rttMs;
    if (r.success) {
        motorStoppedMs_ = r.responseRecvMs;
        return true;
    }

    Serial.println("[GRIND] Motor OFF gagal terkirim -- coba sekali lagi (SAFETY CRITICAL)...");
    MotorResult retry = motor_->stop();
    lastMotorRttMs_ = retry.rttMs;
    if (retry.success) {
        motorStoppedMs_ = retry.responseRecvMs;
        return true;
    }

    Serial.println("[GRIND] MOTOR OFF GAGAL TOTAL setelah retry -- motor mungkin MASIH MENYALA. Operator HARUS cek manual.");
    doAbort(AbortReason::MOTOR_OFF_FAILED);
    return false;
}

// ------------------------------------------------------------
// onWeightSample() -- jantung state machine
// ------------------------------------------------------------
void GrindController::onWeightSample(float rawWeightG, unsigned long sampleTimestampMs) {
    (void)rawWeightG;  // keputusan kontrol tetap pakai weightFilter (filtered)

    if (state_ == GrindState::IDLE || state_ == GrindState::COMPLETE || state_ == GrindState::ABORT) {
        return;
    }
    if (!weightFilter_->hasSample()) {
        return;
    }

    float currentWeight = weightFilter_->latestWeight();

    checkHardOvershoot(currentWeight);
    if (state_ == GrindState::ABORT) return;

    switch (state_) {
        case GrindState::WAIT_STABLE: {
            if (!isnan(waitStableLastWeight_)) {
                float delta = fabsf(currentWeight - waitStableLastWeight_);
                if (delta <= stabilityThresholdG_) {
                    if (waitStableOkSinceMs_ == 0) {
                        waitStableOkSinceMs_ = sampleTimestampMs;
                    } else if (sampleTimestampMs - waitStableOkSinceMs_ >= 500UL) {
                        Serial.printf("[GRIND] Stabil (delta=%.3fg) -- mulai grind.\n", delta);
                        saveCheckpoint("stable_ok");
                        startMotorAndBeginGrind();
                        break;
                    }
                } else {
                    waitStableOkSinceMs_ = 0;
                }
            }
            waitStableLastWeight_ = currentWeight;
            if (millis() - waitStableStartMs_ >= settlingTimeMs_) {
                Serial.println("[GRIND] Stability timeout -- grind tetap mulai.");
                saveCheckpoint("stable_timeout");
                startMotorAndBeginGrind();
            }
            break;
        }
        case GrindState::GRINDING: {
            // Early Stop G -- motor berhenti earlyStopG_ gram sebelum target.
            // Lebih prediktif dari persentase: jarak ke target selalu sama
            // terlepas dari besar dose (2g untuk 18g = 2g untuk 20g).
            float stopThreshold = targetAbsoluteG_ - earlyStopG_;

            // Rekam flow ke history untuk P95 pulse correction
            FlowRateResult flow = weightFilter_->computeFlowRate();
            if (flow.valid && flow.flowRateGps >= GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
                pushFlowSample(flow.flowRateGps, sampleTimestampMs);
            }

            if (currentWeight >= stopThreshold) {
                Serial.printf("[GRIND] Early stop -- berat %.2fg >= threshold %.2fg (earlyStop=%.2fg, target=%.2fg).\n",
                              currentWeight, stopThreshold, earlyStopG_, targetAbsoluteG_);
                saveCheckpoint("motor_stop");

                // Hitung P95 flow untuk pulse correction
                sessionPulseFlowGps_ = computeSessionP95(millis());

                if (!stopMotorOrAbort()) return;

                saveCheckpoint("wait_settle");
                transitionTo(GrindState::WAIT_SETTLE);
            }
            break;
        }
        case GrindState::WAIT_SETTLE: {
            unsigned long elapsed = millis() - motorStoppedMs_;

            // Capture +100ms setelah motor OFF (in-memory, tidak persist NVS)
            if (isnan(weightAtMotorOff100ms_) && elapsed >= 100) {
                weightAtMotorOff100ms_ = weightFilter_ ? weightFilter_->latestWeight() : NAN;
            }
            // Capture +300ms setelah motor OFF (in-memory, tidak persist NVS)
            if (isnan(weightAtMotorOff300ms_) && elapsed >= 300) {
                weightAtMotorOff300ms_ = weightFilter_ ? weightFilter_->latestWeight() : NAN;
            }

            if (elapsed < settlingTimeMs_) {
                break;
            }

            // Capture berat setelah full settling (weightAfterSettle_).
            // weightAtMotorStop_ = weightAfterSettle_ karena tidak ada
            // cara akurat mengukur berat tepat saat relay OFF
            // (motor masih bergetar). Ini trade-off: coast selalu 0,
            // tapi baseline pulse correction benar.
            weightAfterSettle_  = weightFilter_ ? weightFilter_->latestWeight() : NAN;
            weightAtMotorStop_  = weightAfterSettle_;

            if (postPurgeEnabled_ && postPurgePulsesRemaining_ == 0) {
                Serial.printf("[GRIND] Settle selesai -- mulai POST_PURGE (%d pulsa).\n", postPurgePulseCount_);
                postPurgePulsesRemaining_ = postPurgePulseCount_;
                transitionTo(GrindState::POST_PURGE);
                startPostPurgePulse();
                break;
            }

            finishPostPurgeAndDecide();
            break;
        }
        case GrindState::POST_PURGE:
            // Timer motor pulse ditangani di update() -- independen dari HX711 sample
            break;
        case GrindState::PULSE_CORRECTION:
            // Timer motor pulse ditangani di update() -- independen dari HX711 sample
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------
// Pulse correction -- P95 sesi (lihat computeSessionP95()), BUKAN
// flow_now real-time per pulsa.
// ------------------------------------------------------------
void GrindController::startPulse(unsigned long nowMs) {
    (void)nowMs;
    saveCheckpoint("pulse_entry");

    if (pulseAttempts_ >= maxPulseAttempts_) {
        finalWeightG_ = weightFilter_->latestWeight();
        Serial.println("[GRIND] Pulse attempts sudah habis sebelum pulsa dimulai -- selesai sebagai INACCURATE.");
        finishAsComplete();
        return;
    }

    float currentWeight = weightFilter_->latestWeight();
    float errorG = targetAbsoluteG_ - currentWeight;

    if (errorG <= 0) {
        finalWeightG_ = currentWeight;
        finishAsComplete();
        return;
    }

    // Stepped pulse duration berdasarkan error aktual.
    // Lebih prediktif dari P95 flow untuk pulse correction karena
    // flow saat pulse (motor start dari diam) berbeda dari flow saat grinding.
    // Setelah tiap pulse, berat diukur ulang dan error dihitung ulang.
    unsigned long durationMs;
    if (errorG > 0.5f)      durationMs = 100;
    else if (errorG > 0.3f) durationMs =  60;
    else                    durationMs =  40;

    pulseAttempts_++;
    Serial.printf("[GRIND] Pulse #%d/%d -- error=%.3fg duration=%lums (stepped)\n",
                  pulseAttempts_, maxPulseAttempts_, errorG, durationMs);

    saveCheckpoint("pulse_motor_on");
    MotorResult onResult = motor_->start();
    lastMotorRttMs_ = onResult.rttMs;
    if (!onResult.success) {
        Serial.println("[GRIND] Pulse ON gagal terkirim -- force-off sebagai jaga-jaga.");
        doAbort(AbortReason::MOTOR_COMMAND_FAILED);
        return;
    }

    // Non-blocking: simpan timestamp dan durasi, motor OFF dilakukan
    // di evaluatePulseProgress() setelah durationMs berlalu.
    // TIDAK pakai delay() yang memblokir LVGL task -> PANIC.
    pulseMotorOnMs_ = millis();
    pulseDurationMs_ = (unsigned long)durationMs;
}

// ------------------------------------------------------------
// POST_PURGE -- BARU. Lihat catatan lengkap alasan/desain di
// config.h (GRIND_PURGE_PULSE_DURATION_MS dkk) dan di penyisipan
// transisi POST_PURGE pada WAIT_SETTLE (update(), di atas).
// ------------------------------------------------------------

// Jalankan SATU pulsa purge (motor ON durasi TETAP GRIND_PURGE_
// PULSE_DURATION_MS, lalu OFF) -- BEDA dari startPulse() (pulse
// correction biasa): durasi TETAP, BUKAN proporsional error (tujuan
// cuma getar merontokkan sisa, bukan menambah dosis terarah), dan
// TIDAK increment pulseAttempts_/maxPulseAttempts_ (supaya tidak
// tercampur statistik pulse correction biasa -- purge pulses punya
// counter TERPISAH, postPurgePulsesRemaining_).
void GrindController::startPostPurgePulse() {
    Serial.printf("[GRIND] Post-purge pulse -- %d pulsa tersisa (durasi tetap %lums)\n",
                  postPurgePulsesRemaining_, GRIND_PURGE_PULSE_DURATION_MS);
    saveCheckpoint("purge_start");

    saveCheckpoint("purge_motor_on");
    MotorResult onResult = motor_->start();
    lastMotorRttMs_ = onResult.rttMs;
    if (!onResult.success) {
        Serial.println("[GRIND] Post-purge pulse ON gagal terkirim -- force-off sebagai jaga-jaga.");
        doAbort(AbortReason::MOTOR_COMMAND_FAILED);
        return;
    }

    // Non-blocking: simpan timestamp motor ON, motor OFF dilakukan
    // di evaluatePostPurgeProgress() setelah GRIND_PURGE_PULSE_DURATION_MS
    // berlalu -- TIDAK pakai delay() yang memblokir LVGL task.
    purgeMotorOnMs_ = millis();
}

void GrindController::evaluatePostPurgeProgress(unsigned long sampleTimestampMs) {
    (void)sampleTimestampMs;

    // NON-BLOCKING: kalau motor purge sedang ON (purgeMotorOnMs_ != 0),
    // tunggu GRIND_PURGE_PULSE_DURATION_MS berlalu lalu matikan motor.
    // Ini menggantikan delay() lama yang memblokir LVGL task -> PANIC.
    if (purgeMotorOnMs_ != 0) {
        if (millis() - purgeMotorOnMs_ < (unsigned long)GRIND_PURGE_PULSE_DURATION_MS) {
            return;  // belum waktunya OFF
        }
        // Durasi tercapai -- matikan motor
        purgeMotorOnMs_ = 0;
        saveCheckpoint("purge_motor_off");
        if (!stopMotorOrAbort()) {
            return;
        }
        postPurgePulsesRemaining_--;
        // Lanjut ke pengecekan gap/settling di bawah
    }

    if (postPurgePulsesRemaining_ > 0) {
        // Masih ada pulsa berikutnya -- tunggu GAP pendek saja (80ms
        // supaya motor benar-benar OFF sebelum nyala lagi, bukan untuk
        // settling berat -- kopi dari pulsa ini belum perlu settle
        // karena pulsa berikutnya akan menggerakkan berat lagi).
        if (millis() - motorStoppedMs_ < GRIND_PURGE_PULSE_GAP_MS) {
            return;
        }
        saveCheckpoint("purge_eval");
        startPostPurgePulse();
        return;
    }

    // Pulse TERAKHIR sudah selesai -- tunggu settlingTimeMs_ PENUH
    // (bukan cuma GRIND_PURGE_PULSE_GAP_MS = 150ms) sebelum baca
    // berat dan ambil keputusan final.
    if (millis() - motorStoppedMs_ < settlingTimeMs_) {
        return;
    }

    saveCheckpoint("purge_eval");
    Serial.println("[GRIND] Post-purge selesai (semua pulsa habis, settling selesai) -- lanjut cek target.");
    finishPostPurgeAndDecide();
}

void GrindController::finishPostPurgeAndDecide() {
    saveCheckpoint("purge_decide");
    float settledWeight = weightFilter_->latestWeight();
    float errorG = settledWeight - targetAbsoluteG_;

    Serial.printf("[GRIND] Settle selesai -- berat=%.2fg target=%.2fg error=%.3fg\n",
                  settledWeight, targetAbsoluteG_, errorG);

    if (fabsf(errorG) <= accuracyToleranceG_) {
        finalWeightG_ = settledWeight;
        saveCheckpoint("purge_done_ok");
        finishAsComplete();
    } else if (errorG > 0) {
        finalWeightG_ = settledWeight;
        Serial.println("[GRIND] Overshoot di luar toleransi -- tidak ada koreksi untuk kelebihan, selesai sebagai INACCURATE.");
        saveCheckpoint("purge_done_over");
        finishAsComplete();
    } else {
        sessionPulseFlowGps_ = computeSessionP95(millis());
        if (isnan(sessionPulseFlowGps_)) {
            Serial.println("[GRIND] P95 sesi tidak tersedia (tidak ada sample flow terekam) -- pulsa akan pakai fallback.");
        } else {
            Serial.printf("[GRIND] P95 flow sesi ini: %.2f gps (dipakai untuk semua pulsa sesi ini)\n", sessionPulseFlowGps_);
        }
        saveCheckpoint("purge_to_pulse");
        transitionTo(GrindState::PULSE_CORRECTION);
        startPulse(millis());
    }
}

void GrindController::evaluatePulseProgress(unsigned long sampleTimestampMs) {
    (void)sampleTimestampMs;

    // NON-BLOCKING: kalau motor pulse sedang ON (pulseMotorOnMs_ != 0),
    // tunggu pulseDurationMs_ berlalu lalu matikan motor.
    if (pulseMotorOnMs_ != 0) {
        if (millis() - pulseMotorOnMs_ < pulseDurationMs_) {
            return;  // belum waktunya OFF
        }
        // Durasi tercapai -- matikan motor
        pulseMotorOnMs_ = 0;
        pulseDurationMs_ = 0;
        saveCheckpoint("pulse_motor_off");
        if (!stopMotorOrAbort()) {
            return;
        }
        // Lanjut ke settling check di bawah
    }

    // GANTI konstanta -> settlingTimeMs_ (BARU), SAMA variable dengan
    // WAIT_SETTLE di atas -- sesuai kesepakatan: satu setting untuk
    // kedua tempat ini.
    if (millis() - motorStoppedMs_ < settlingTimeMs_) {
        return;
    }

    saveCheckpoint("pulse_eval");
    float currentWeight = weightFilter_->latestWeight();
    float errorG = currentWeight - targetAbsoluteG_;

    Serial.printf("[GRIND] Evaluasi pasca-pulse #%d -- berat=%.2fg target=%.2fg error=%.3fg\n",
                  pulseAttempts_, currentWeight, targetAbsoluteG_, errorG);

    if (fabsf(errorG) <= accuracyToleranceG_) {
        finalWeightG_ = currentWeight;
        finishAsComplete();
        return;
    }

    if (pulseAttempts_ >= maxPulseAttempts_) {
        finalWeightG_ = currentWeight;
        Serial.println("[GRIND] Pulse attempts habis, masih di luar toleransi -- selesai sebagai INACCURATE (AMAN, motor OFF).");
        finishAsComplete();
        return;
    }

    startPulse(millis());
}

void GrindController::finishAsComplete() {
    if (state_ == GrindState::COMPLETE || state_ == GrindState::ABORT ||
        state_ == GrindState::IDLE) {
        return;
    }

    float errorG = finalErrorG();
    if (fabsf(errorG) <= accuracyToleranceG_) {
        result_ = GrindResult::SUCCESS;
        saveCheckpoint("done_success");
    } else {
        result_ = GrindResult::INACCURATE;
        saveCheckpoint("done_inaccurate");
    }

    Serial.printf("[GRIND] SELESAI -- hasil=%s berat_akhir=%.2fg target=%.2fg error=%.3fg pulse_attempts=%d durasi=%lums earlyStop=%.2fg\n",
                  result_ == GrindResult::SUCCESS ? "SUCCESS" : "INACCURATE",
                  finalWeightG_, targetAbsoluteG_, errorG, pulseAttempts_, grindDurationMs(), earlyStopG_);

    // Last Grind Data
    lastGrindWeightAtMotorStop_ = weightAtMotorStop_;
    lastGrindActualCoast_       = (isnan(weightAtMotorStop_) || isnan(weightAfterSettle_))
                                  ? NAN
                                  : weightAfterSettle_ - weightAtMotorStop_;
    lastGrindWeightAtMotorOff100ms_ = weightAtMotorOff100ms_;
    lastGrindWeightAtMotorOff300ms_ = weightAtMotorOff300ms_;
    lastGrindEarlyStopG_        = earlyStopG_;
    lastGrindFinalWeightG_      = finalWeightG_;
    lastGrindPulseCount_        = pulseAttempts_;

    transitionTo(GrindState::COMPLETE);
}
