#include "grind_controller.h"
#include <math.h>
#include <algorithm>

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
      grindStartMs_(0), motorStartedMs_(0), motorStoppedMs_(0), lastFlowAboveThresholdMs_(0),
      candidateFlowStartMs_(0), flowStartConfirmed_(false), grindLatencyMs_(0), motorStopTargetWeightG_(0.0f), sessionPulseFlowGps_(NAN),
      pulseAttempts_(0), lastMotorRttMs_(0),
      // Default dari config.h -- behavior lama (sebelum UI Settings
      // tersambung) tidak berubah kalau setter tidak pernah dipanggil.
      accuracyToleranceG_(GRIND_ACCURACY_TOLERANCE_G), maxPulseAttempts_(GRIND_MAX_PULSE_ATTEMPTS),
      pendingAccuracyToleranceG_(GRIND_ACCURACY_TOLERANCE_G), pendingMaxPulseAttempts_(GRIND_MAX_PULSE_ATTEMPTS),
      // BARU -- settlingTimeMs_, pola sama, default dari config.h.
      settlingTimeMs_(GRIND_SCALE_PRECISION_SETTLING_TIME_MS), pendingSettlingTimeMs_(GRIND_SCALE_PRECISION_SETTLING_TIME_MS),
      // BARU -- coastRatio_, pola sama, default dari config.h.
      coastRatio_(GRIND_LATENCY_TO_COAST_RATIO), pendingCoastRatio_(GRIND_LATENCY_TO_COAST_RATIO),
      // BARU -- confirmationWindowMs_, pola sama, default dari config.h.
      confirmationWindowMs_(GRIND_LATENCY_CONFIRMATION_MS), pendingConfirmationWindowMs_(GRIND_LATENCY_CONFIRMATION_MS),
      // BARU -- postPurgeEnabled_/postPurgePulseCount_, pola sama.
      // Default OFF (false) -- fitur baru, TIDAK mengubah behavior
      // lama sampai operator eksplisit mengaktifkan lewat Settings.
      postPurgeEnabled_(false), pendingPostPurgeEnabled_(false),
      postPurgePulseCount_(GRIND_POST_PURGE_PULSE_COUNT_DEFAULT), pendingPostPurgePulseCount_(GRIND_POST_PURGE_PULSE_COUNT_DEFAULT),
      postPurgePulsesRemaining_(0) {}

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
    // KONFIRMASI dari 2 audit independen -- WAJIB dicek PALING AWAL,
    // sebelum cek state_ apa pun di bawah. Lihat catatan lengkap di
    // deklarasi motorSafetyLockout_ (grind_controller.h) dan doAbort()
    // (tempat flag ini di-set true). Begitu lockout aktif, TIDAK ADA
    // jalur apa pun (state_ apa pun) yang boleh memulai sesi baru.
    if (motorSafetyLockout_) {
        Serial.println("[GRIND] TOLAK -- SAFETY LOCKOUT aktif (motor OFF gagal dikonfirmasi sebelumnya). Putus daya grinder secara fisik, lalu hidupkan ulang.");
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

    // Cek stabilitas SEBELUM catat tare -- SAMA seperti versi
    // sebelumnya (tidak berubah oleh refactor model real-time ini).
    // flow.valid HARUS true DAN |flow| di bawah threshold -- KEDUANYA.
    FlowRateResult preGrindFlow = weightFilter_->computeFlowRate();
    if (!preGrindFlow.valid) {
        Serial.println("[GRIND] TOLAK -- flow rate belum bisa dihitung (window belum cukup terisi). Tunggu beberapa detik lagi, lalu coba ulang.");
        abortReason_ = AbortReason::UNSTABLE_WEIGHT;
        result_ = GrindResult::ABORTED;
        transitionTo(GrindState::ABORT);
        return false;
    }
    if (fabsf(preGrindFlow.flowRateGps) > GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
        Serial.printf("[GRIND] TOLAK -- berat belum stabil (flow=%.2f gps). Tunggu timbangan settle setelah taruh portafilter/dosing cup, lalu coba lagi.\n",
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
    lastFlowAboveThresholdMs_ = 0;

    // Snapshot parameter dari UI Settings (kalau operator sempat
    // mengubah lewat setAccuracyToleranceG()/setMaxPulseAttempts())
    // KE sesi yang baru dimulai ini -- lihat komentar setter di
    // grind_controller.h untuk alasan snapshot-at-start (perubahan
    // Settings di TENGAH grind yang sedang berjalan TIDAK memengaruhi
    // sesi itu, baru berlaku di startGrind() berikutnya).
    accuracyToleranceG_ = pendingAccuracyToleranceG_;
    maxPulseAttempts_ = pendingMaxPulseAttempts_;
    settlingTimeMs_ = pendingSettlingTimeMs_;  // BARU -- pola sama
    coastRatio_ = pendingCoastRatio_;  // BARU -- pola sama
    confirmationWindowMs_ = pendingConfirmationWindowMs_;  // BARU -- pola sama
    postPurgeEnabled_ = pendingPostPurgeEnabled_;  // BARU -- pola sama
    postPurgePulseCount_ = pendingPostPurgePulseCount_;  // BARU -- pola sama
    postPurgePulsesRemaining_ = 0;  // BARU -- reset counter runtime untuk sesi baru

    // Reset state model real-time untuk sesi baru.
    candidateFlowStartMs_ = 0;
    flowStartConfirmed_ = false;
    grindLatencyMs_ = 0;
    motorStopTargetWeightG_ = 0.0f;
    sessionPulseFlowGps_ = NAN;
    resetFlowHistory();

    Serial.printf("[GRIND] Mulai -- dose=%.2fg tare=%.2fg target_absolut=%.2fg (ratio=%.2f, tolerance=%.3fg, max_pulses=%d, settle=%lums)\n",
                  targetDoseG_, startWeightG_, targetAbsoluteG_, coastRatio_,
                  accuracyToleranceG_, maxPulseAttempts_, settlingTimeMs_);

    transitionTo(GrindState::STARTING);

    MotorResult r = motor_->start();
    lastMotorRttMs_ = r.rttMs;
    if (!r.success) {
        // FIX SAFETY CRITICAL (dipertahankan dari versi sebelumnya):
        // "ON gagal terkirim" TIDAK BERARTI motor pasti mati -- response
        // bisa hilang SETELAH Tasmota/GPIO sebenarnya sudah proses ON.
        Serial.println("[GRIND] Motor ON gagal terkirim -- force-off sebagai jaga-jaga.");
        doAbort(AbortReason::MOTOR_COMMAND_FAILED);
        return false;
    }

    motorStartedMs_ = r.commandSentMs;
    lastFlowAboveThresholdMs_ = motorStartedMs_;  // basis awal stall timer

    // TRANSISI KE WAIT_FLOW_START, BUKAN LANGSUNG GRINDING (fix per
    // review) -- predictive stop TIDAK BOLEH aktif sebelum flow
    // benar-benar confirmed. Lihat evaluateFlowStartConfirmation()
    // untuk transisi ke GRINDING setelah confirmed, dan checkStall()
    // untuk timeout kalau flow tidak pernah confirmed.
    transitionTo(GrindState::WAIT_FLOW_START);
    return true;
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

    if (state_ == GrindState::WAIT_FLOW_START || state_ == GrindState::GRINDING) {
        checkStall(nowMs);
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
        return;  // masih grace period spin-up
    }
    if (nowMs - lastFlowAboveThresholdMs_ >= GRIND_STALL_TIMEOUT_MS) {
        Serial.println("[GRIND] STALL -- flow tidak terdeteksi dalam batas waktu (beans habis/jalur macet?).");
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
        case GrindState::WAIT_FLOW_START:
            evaluateFlowStartConfirmation(sampleTimestampMs);
            break;
        case GrindState::GRINDING:
            evaluateGrindProgress(sampleTimestampMs);
            break;
        case GrindState::WAIT_SETTLE: {
            // GANTI konstanta -> settlingTimeMs_ (BARU, bisa diatur
            // lewat UI Settings -- lihat setSettlingTimeMs() di header).
            // Default tetap GRIND_SCALE_PRECISION_SETTLING_TIME_MS
            // (constructor), behavior lama tidak berubah kalau operator
            // tidak pernah menyentuh setting ini.
            if (millis() - motorStoppedMs_ < settlingTimeMs_) {
                break;
            }

            // BARU -- POST_PURGE disisipkan DI SINI, SEBELUM cek
            // target/keputusan (BUKAN setelah pulsa gagal cukupi
            // target) -- sesuai kesepakatan eksplisit (lihat riwayat
            // diskusi): sisa chute yang mungkin masih tertahan tepat
            // sebelum motor berhenti perlu dirontokkan DULU, supaya
            // angka yang dipakai keputusan (fabsf(errorG) <=
            // accuracyToleranceG_ dst di finishPostPurgeAndDecide())
            // sudah benar-benar final -- tidak akan berubah sendiri
            // lagi kalau ditunggu/digetarkan setelah keputusan
            // terlanjur diambil. postPurgePulsesRemaining_ == 0 di
            // sini SELALU true untuk kunjungan PERTAMA WAIT_SETTLE
            // sesi ini (di-reset 0 di startGrind()) -- guard ini
            // murni jaga-jaga (tidak seharusnya WAIT_SETTLE dikunjungi
            // lagi setelah POST_PURGE, tapi kalau suatu saat state
            // machine berubah, ini mencegah purge terpicu 2x).
            if (postPurgeEnabled_ && postPurgePulsesRemaining_ == 0) {
                Serial.printf("[GRIND] Settle selesai -- mulai POST_PURGE (%d pulsa) sebelum cek target.\n", postPurgePulseCount_);
                postPurgePulsesRemaining_ = postPurgePulseCount_;
                transitionTo(GrindState::POST_PURGE);
                startPostPurgePulse();
                break;
            }

            finishPostPurgeAndDecide();
            break;
        }
        case GrindState::POST_PURGE:
            evaluatePostPurgeProgress(sampleTimestampMs);
            break;
        case GrindState::PULSE_CORRECTION:
            evaluatePulseProgress(sampleTimestampMs);
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------
// evaluateGrindProgress() -- MODEL BARU real-time
// ------------------------------------------------------------
// ------------------------------------------------------------
// evaluateFlowStartConfirmation() -- state WAIT_FLOW_START.
// Implementasi confirmation window 500ms YANG BENAR (fix per review):
// satu sample >= threshold TIDAK LANGSUNG dianggap confirmed. Harus
// TETAP >= threshold selama GRIND_LATENCY_CONFIRMATION_MS berturut-
// turut sejak sample pertama yang melewati threshold (candidate).
// Kalau flow turun di bawah threshold sebelum window selesai,
// candidate DIRESET -- mencegah satu spike noise sesaat salah
// dianggap "flow sudah mulai stabil".
//
// TIDAK ADA evaluasi predictive stop di state ini SAMA SEKALI --
// motorStopTargetWeightG_ tidak dihitung/dipakai di sini. Satu-
// satunya jalan keluar dari WAIT_FLOW_START: (a) confirmed -> pindah
// ke GRINDING, atau (b) checkStall() di update() mendeteksi timeout
// (GRIND_STALL_TIMEOUT_MS sejak motorStartedMs_) -> ABORT(STALL).
// ------------------------------------------------------------
void GrindController::evaluateFlowStartConfirmation(unsigned long sampleTimestampMs) {
    FlowRateResult flow = weightFilter_->computeFlowRate();

    bool aboveThreshold = flow.valid && flow.flowRateGps >= GRIND_FLOW_DETECTION_THRESHOLD_GPS;

    if (aboveThreshold) {
        lastFlowAboveThresholdMs_ = sampleTimestampMs;  // basis stall timer -- flow TERLIHAT, walau belum confirmed penuh

        if (candidateFlowStartMs_ == 0) {
            // Sample pertama yang melewati threshold sejak reset
            // terakhir -- mulai window konfirmasi dari sini.
            candidateFlowStartMs_ = sampleTimestampMs;
            Serial.printf("[GRIND] Flow candidate terdeteksi @ %lums (flow=%.2fgps) -- menunggu konfirmasi %lums...\n",
                          sampleTimestampMs, flow.flowRateGps, confirmationWindowMs_);
        } else if (sampleTimestampMs - candidateFlowStartMs_ >= confirmationWindowMs_) {
            // Window konfirmasi terpenuhi TANPA putus (tidak ada
            // sample di bawah threshold di antaranya, karena kalau
            // ada, candidateFlowStartMs_ sudah direset di branch else
            // di bawah). GANTI konstanta -> confirmationWindowMs_
            // (BARU, bisa diatur lewat UI Settings -- lihat
            // setConfirmationWindowMs() di header. Disepakati setelah
            // observasi gumpalan sisa chute bisa lolos window lama
            // seolah flow sungguhan).
            grindLatencyMs_ = candidateFlowStartMs_ - motorStartedMs_;
            flowStartConfirmed_ = true;
            // KOREKSI (bug ditemukan lewat audit lanjutan, sebelum
            // pio run berikutnya): clamp GRIND_MAX_PREDICTIVE_LATENCY_MS
            // SEBELUMNYA cuma diterapkan di evaluateGrindProgress()
            // (per-sample, state GRINDING) -- titik INISIALISASI
            // PERTAMA motorStopTargetWeightG_ di sini (tepat saat
            // transisi ke GRINDING, SEBELUM evaluateGrindProgress()
            // sempat jalan sekalipun) TIDAK ikut ter-clamp. Kalau
            // grindLatencyMs_ anomali besar (candidateFlowStartMs_
            // telat jauh dari motorStartedMs_), motorStopTargetWeightG_
            // AWAL ini bisa langsung besar SEBELUM sample pertama di
            // evaluateGrindProgress() sempat mengoreksinya -- window
            // singkat tapi nyata. KOREKSI: pakai effectiveLatencyMs
            // yang sama (clamp identik) di sini juga, grindLatencyMs_
            // itu sendiri TETAP tidak diubah (dilaporkan apa adanya).
            float effectiveLatencyMsInit = fminf(grindLatencyMs_, (float)GRIND_MAX_PREDICTIVE_LATENCY_MS);
            // GANTI konstanta -> coastRatio_ (BARU, bisa diatur lewat
            // UI Settings -- lihat setCoastRatio() di header).
            motorStopTargetWeightG_ = flow.flowRateGps * (effectiveLatencyMsInit * coastRatio_) / 1000.0f;
            Serial.printf("[GRIND] Flow start CONFIRMED (window %lums terpenuhi) -- grind_latency=%lums (effective=%.0fms utk model), flow=%.2fgps\n",
                          confirmationWindowMs_, grindLatencyMs_, effectiveLatencyMsInit, flow.flowRateGps);
            transitionTo(GrindState::GRINDING);
        }
        // else: masih dalam window konfirmasi, belum genap confirmationWindowMs_ -- tunggu sample berikutnya.
    } else {
        if (candidateFlowStartMs_ != 0) {
            Serial.printf("[GRIND] Flow candidate BATAL (flow turun di bawah threshold sebelum window %lums selesai) -- reset, menunggu candidate baru.\n",
                          confirmationWindowMs_);
        }
        candidateFlowStartMs_ = 0;  // reset -- kembali menunggu sample pertama berikutnya
    }
}

// ------------------------------------------------------------
// evaluateGrindProgress() -- state GRINDING, HANYA dipanggil setelah
// flowStartConfirmed_ true (dijamin oleh transisi state dari
// evaluateFlowStartConfirmation()). TIDAK ADA LAGI fallback
// motorStopTargetWeightG_ = 0 (itu bug safety yang sudah diperbaiki
// -- predictive stop sekarang HANYA aktif kalau model punya data
// grind_latency_ms yang valid).
// ------------------------------------------------------------
void GrindController::evaluateGrindProgress(unsigned long sampleTimestampMs) {
    FlowRateResult flow = weightFilter_->computeFlowRate();

    if (flow.valid && flow.flowRateGps >= GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
        lastFlowAboveThresholdMs_ = sampleTimestampMs;
        // Rekam flow rate + timestamp ke riwayat sesi -- dipakai nanti
        // untuk P95 pulsa (lihat computeSessionP95()), yang memfilter
        // ke window GRIND_PULSE_P95_WINDOW_MS TERAKHIR sebelum
        // predictive stop.
        pushFlowSample(flow.flowRateGps, sampleTimestampMs);
    }

    // Update motor_stop_target_weight_g TIAP SAMPLE pakai flow_now
    // REAL-TIME (bukan cached dari saat konfirmasi) -- ini yang
    // membuat model reaktif terhadap perubahan flow selama sesi (bean
    // habis melambat, dst). Kalau flow belum valid ATAU flow di bawah
    // threshold deteksi (termasuk kecil/mendekati nol/negatif -- lihat
    // catatan di bawah) di sample ini, motorStopTargetWeightG_
    // dibiarkan pakai nilai TERAKHIR yang valid (bukan direset ke 0 --
    // grindLatencyMs_ sudah pasti valid di state ini karena confirmed).
    //
    // KOREKSI (bug ditemukan lewat audit eksternal, sebelum pio run
    // v18 berikutnya): versi awal cuma cek "flow.valid" di sini,
    // TIDAK ikut cek ">= GRIND_FLOW_DETECTION_THRESHOLD_GPS" seperti
    // guard yang dipakai untuk stall-timer/P95 history di atas (baris
    // if (flow.valid && flow.flowRateGps >= ...) beberapa baris ke
    // atas). Akibatnya flow valid TAPI kecil (atau NEGATIF -- OLS
    // regression bisa hasilkan slope negatif sesaat kalau ada noise/
    // settling singkat di tengah window walau aliran kopi aktual
    // sedang berjalan) ikut menggantikan motorStopTargetWeightG_,
    // membuat stopThreshold bergeser mendekati/melewati target --
    // predictive stop jadi lebih terlambat justru di momen yang
    // paling penting. KOREKSI: guard sekarang IDENTIK dengan guard
    // stall-timer/P95 di atas -- flow harus valid DAN di atas
    // threshold deteksi baru dipakai update model.
    //
    // KOREKSI KEDUA (ditemukan lewat perbandingan eksplisit dengan
    // upstream jaapp/smart-grind-by-weight, dikonfirmasi empiris lewat
    // testing fisik -- overshoot +0.45g di grind 18g vs +0.09g di grind
    // 5g): tambah guard waktu GRIND_FLOW_CALC_DELAY_MS (lihat config.h)
    // -- upstream SENGAJA menunggu flow rate "settle" dulu sebelum
    // dipakai model, bukan langsung dari sample pertama pasca-confirmed.
    // TIDAK ADA sebelumnya di adaptasi project ini -- kemungkinan
    // terlewat, bukan keputusan sadar (tidak ada catatan/diskusi soal
    // ini di config.h/README sebelum penambahan ini).
    if (flow.valid && flow.flowRateGps >= GRIND_FLOW_DETECTION_THRESHOLD_GPS &&
        sampleTimestampMs >= motorStartedMs_ + grindLatencyMs_ + GRIND_FLOW_CALC_DELAY_MS) {
        // KONFIRMASI dari 2 audit independen: grindLatencyMs_ TIDAK
        // pernah di-clamp sebelumnya -- anomali (hopper hampir kosong,
        // biji tersangkut sesaat, dst, sampai batas realistis
        // GRIND_STALL_TIMEOUT_MS sebelum STALL abort terpicu duluan)
        // bisa membuat grindLatencyMs_ jauh lebih besar dari kondisi
        // normal, menghasilkan coastTimeMs yang jauh terlalu besar ->
        // motorStopTargetWeightG_ jauh terlalu besar -> predictive stop
        // mematikan motor JAUH lebih awal dari seharusnya (severe
        // undershoot). effectiveLatencyMs di bawah HANYA membatasi
        // nilai yang dipakai MODEL ini -- grindLatencyMs_ itu sendiri
        // TIDAK diubah/di-clamp (tetap dilaporkan apa adanya lewat
        // getter/Serial log untuk keperluan diagnostik/kalibrasi).
        float effectiveLatencyMs = fminf(grindLatencyMs_, (float)GRIND_MAX_PREDICTIVE_LATENCY_MS);
        // GANTI konstanta -> coastRatio_ (BARU), SAMA variable dengan
        // evaluateFlowStartConfirmation() di atas.
        float coastTimeMs = effectiveLatencyMs * coastRatio_;
        motorStopTargetWeightG_ = flow.flowRateGps * coastTimeMs / 1000.0f;
    }

    float currentWeight = weightFilter_->latestWeight();
    float stopThreshold = targetAbsoluteG_ - motorStopTargetWeightG_;

    if (currentWeight >= stopThreshold) {
        // KOREKSI (bug diagnostik ditemukan lewat audit lanjutan):
        // log SEBELUMNYA mencetak "coast=" dari grindLatencyMs_ mentah
        // (unclamped) * ratio -- TIDAK konsisten dengan
        // motorStopTargetWeightG_ yang ditampilkan di angka
        // terakhir, yang sebenarnya SUDAH dihitung dari
        // effectiveLatencyMs (clamped) beberapa baris di atas. Kalau
        // grindLatencyMs_ mentah anomali besar (misal 3000ms) tapi
        // clamp membatasi ke 1500ms, log lama akan menampilkan
        // "coast=3000ms" padahal motor_stop_target sebenarnya dihitung
        // dari 1500ms -- menyesatkan saat diagnosis/kalibrasi. KOREKSI:
        // log sekarang eksplisit menampilkan KEDUANYA (latency mentah
        // DAN effective/clamped yang benar-benar dipakai model),
        // supaya tidak ambigu yang mana yang menghasilkan
        // motor_stop_target di ujung baris.
        float effectiveLatencyMsLog = fminf(grindLatencyMs_, (float)GRIND_MAX_PREDICTIVE_LATENCY_MS);
        // GANTI konstanta -> coastRatio_ di log ini juga, konsisten
        // dengan nilai yang benar-benar dipakai model di atas.
        Serial.printf("[GRIND] Predictive stop -- berat %.2fg >= threshold %.2fg (grind_latency=%lums effective_latency=%.0fms coast=%.0fms motor_stop_target=%.2fg)\n",
                      currentWeight, stopThreshold, grindLatencyMs_, effectiveLatencyMsLog,
                      effectiveLatencyMsLog * coastRatio_, motorStopTargetWeightG_);

        if (!stopMotorOrAbort()) {
            return;  // doAbort(MOTOR_OFF_FAILED) sudah dipanggil di dalam
        }

        transitionTo(GrindState::WAIT_SETTLE);
    }
}

// ------------------------------------------------------------
// Pulse correction -- P95 sesi (lihat computeSessionP95()), BUKAN
// flow_now real-time per pulsa.
// ------------------------------------------------------------
void GrindController::startPulse(unsigned long nowMs) {
    (void)nowMs;

    // CATATAN ARSITEKTUR (dipertahankan dari versi sebelumnya): delay()
    // di bawah untuk pulsa ON itu BLOCKING (30-250ms). Diterima untuk
    // trial awal ini -- lihat README untuk rencana non-blocking di
    // firmware produksi final.

    if (pulseAttempts_ >= maxPulseAttempts_) {
        finalWeightG_ = weightFilter_->latestWeight();
        Serial.println("[GRIND] Pulse attempts sudah habis sebelum pulsa dimulai -- selesai sebagai INACCURATE.");
        finishAsComplete();
        return;
    }

    float currentWeight = weightFilter_->latestWeight();
    float errorG = targetAbsoluteG_ - currentWeight;  // positif = masih kurang

    if (errorG <= 0) {
        finalWeightG_ = currentWeight;
        finishAsComplete();
        return;
    }

    // Flow untuk pulsa: P95 SESI INI (dihitung sekali di WAIT_SETTLE
    // sebelum pulsa pertama). Fallback ke GRIND_PULSE_FLOW_RATE_FALLBACK_GPS
    // kalau P95 tidak tersedia (mis. tidak ada sample flow terekam
    // sama sekali selama GRINDING -- kondisi anomali).
    float estimatedFlow = !isnan(sessionPulseFlowGps_) ? sessionPulseFlowGps_ : GRIND_PULSE_FLOW_RATE_FALLBACK_GPS;
    if (isnan(sessionPulseFlowGps_)) {
        Serial.println("[GRIND] Pulse -- P95 sesi tidak tersedia, pakai fallback ESTIMASI (bukan hasil pengukuran).");
    }
    // Clamp ke rentang masuk akal -- sama seperti versi sebelumnya &
    // upstream (get_clamped_pulse_flow_rate()).
    if (estimatedFlow < GRIND_FLOW_RATE_MIN_SANE_GPS) {
        estimatedFlow = GRIND_PULSE_FLOW_RATE_FALLBACK_GPS;
    } else if (estimatedFlow > GRIND_FLOW_RATE_MAX_SANE_GPS) {
        estimatedFlow = GRIND_FLOW_RATE_MAX_SANE_GPS;
    }

    float durationMs = (errorG / estimatedFlow) * 1000.0f;
    if (durationMs < GRIND_MIN_PULSE_DURATION_MS) {
        durationMs = GRIND_MIN_PULSE_DURATION_MS;
    }
    if (durationMs > GRIND_MAX_PULSE_DURATION_MS) {
        durationMs = GRIND_MAX_PULSE_DURATION_MS;
    }

    pulseAttempts_++;
    Serial.printf("[GRIND] Pulse #%d/%d -- error=%.3fg P95_flow=%.2fgps duration=%.0fms\n",
                  pulseAttempts_, maxPulseAttempts_, errorG, estimatedFlow, durationMs);

    MotorResult onResult = motor_->start();
    lastMotorRttMs_ = onResult.rttMs;
    if (!onResult.success) {
        Serial.println("[GRIND] Pulse ON gagal terkirim -- force-off sebagai jaga-jaga.");
        doAbort(AbortReason::MOTOR_COMMAND_FAILED);
        return;
    }

    delay((unsigned long)durationMs);

    if (!stopMotorOrAbort()) {
        return;
    }
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

    MotorResult onResult = motor_->start();
    lastMotorRttMs_ = onResult.rttMs;
    if (!onResult.success) {
        Serial.println("[GRIND] Post-purge pulse ON gagal terkirim -- force-off sebagai jaga-jaga.");
        doAbort(AbortReason::MOTOR_COMMAND_FAILED);
        return;
    }

    // CATATAN ARSITEKTUR SAMA seperti startPulse(): delay() blocking
    // di sini DITERIMA untuk trial awal ini (durasi purge jauh lebih
    // pendek dari pulsa koreksi biasa, 80ms vs 30-250ms, jadi dampak
    // blocking-nya lebih kecil lagi).
    delay((unsigned long)GRIND_PURGE_PULSE_DURATION_MS);

    if (!stopMotorOrAbort()) {
        return;
    }

    postPurgePulsesRemaining_--;
}

// Dipanggil TIAP sample selama state POST_PURGE -- tunggu jeda
// GRIND_PURGE_PULSE_GAP_MS setelah pulsa TERAKHIR berhenti (motor
// benar-benar diam dulu, konsisten filosofi settlingTimeMs_ di
// WAIT_SETTLE/evaluatePulseProgress()), lalu: kalau masih ada pulsa
// tersisa -> jalankan pulsa berikutnya; kalau sudah habis -> semua
// purge selesai, lanjut ke keputusan target (finishPostPurgeAndDecide()).
void GrindController::evaluatePostPurgeProgress(unsigned long sampleTimestampMs) {
    (void)sampleTimestampMs;

    if (millis() - motorStoppedMs_ < GRIND_PURGE_PULSE_GAP_MS) {
        return;
    }

    if (postPurgePulsesRemaining_ > 0) {
        startPostPurgePulse();
        return;
    }

    Serial.println("[GRIND] Post-purge selesai (semua pulsa habis) -- lanjut cek target.");
    finishPostPurgeAndDecide();
}

// Logic KEPUTUSAN target (sukses/overshoot/undershoot -> pulse
// correction) -- DIPINDAH dari WAIT_SETTLE lama (SEBELUM POST_PURGE
// ditambahkan), SEKARANG dipanggil dari 2 tempat: (a) WAIT_SETTLE
// LANGSUNG kalau postPurgeEnabled_ == false (behavior lama, tidak
// berubah), (b) evaluatePostPurgeProgress() setelah semua pulsa purge
// selesai (behavior BARU). Logic keputusan ITU SENDIRI TIDAK BERUBAH
// SAMA SEKALI dari versi lama -- cuma DIPINDAH ke fungsi terpisah
// supaya bisa dipanggil dari 2 titik tanpa duplikasi kode.
void GrindController::finishPostPurgeAndDecide() {
    float settledWeight = weightFilter_->latestWeight();
    float errorG = settledWeight - targetAbsoluteG_;

    Serial.printf("[GRIND] Settle selesai -- berat=%.2fg target=%.2fg error=%.3fg\n",
                  settledWeight, targetAbsoluteG_, errorG);

    if (fabsf(errorG) <= accuracyToleranceG_) {
        finalWeightG_ = settledWeight;
        finishAsComplete();
    } else if (errorG > 0) {
        finalWeightG_ = settledWeight;
        Serial.println("[GRIND] Overshoot di luar toleransi -- tidak ada koreksi untuk kelebihan, selesai sebagai INACCURATE.");
        finishAsComplete();
    } else {
        // Kurang dari target -- hitung P95 flow SESI INI dari
        // window GRIND_PULSE_P95_WINDOW_MS TERAKHIR sebelum
        // sekarang (predictive stop baru saja terjadi), lalu
        // mulai pulse correction.
        sessionPulseFlowGps_ = computeSessionP95(millis());
        if (isnan(sessionPulseFlowGps_)) {
            Serial.println("[GRIND] P95 sesi tidak tersedia (tidak ada sample flow terekam) -- pulsa akan pakai fallback.");
        } else {
            Serial.printf("[GRIND] P95 flow sesi ini: %.2f gps (dipakai untuk semua pulsa sesi ini)\n", sessionPulseFlowGps_);
        }
        transitionTo(GrindState::PULSE_CORRECTION);
        startPulse(millis());
    }
}

void GrindController::evaluatePulseProgress(unsigned long sampleTimestampMs) {
    (void)sampleTimestampMs;

    // GANTI konstanta -> settlingTimeMs_ (BARU), SAMA variable dengan
    // WAIT_SETTLE di atas -- sesuai kesepakatan: satu setting untuk
    // kedua tempat ini.
    if (millis() - motorStoppedMs_ < settlingTimeMs_) {
        return;
    }

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
    float errorG = finalErrorG();
    if (fabsf(errorG) <= accuracyToleranceG_) {
        result_ = GrindResult::SUCCESS;
    } else {
        result_ = GrindResult::INACCURATE;
    }

    Serial.printf("[GRIND] SELESAI -- hasil=%s berat_akhir=%.2fg target=%.2fg error=%.3fg pulse_attempts=%d durasi=%lums grind_latency=%lums\n",
                  result_ == GrindResult::SUCCESS ? "SUCCESS" : "INACCURATE",
                  finalWeightG_, targetAbsoluteG_, errorG, pulseAttempts_, grindDurationMs(), grindLatencyMs_);

    transitionTo(GrindState::COMPLETE);
}
