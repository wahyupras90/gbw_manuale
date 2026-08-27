#include "latency_calibrator.h"
#include <math.h>

LatencyCalibrator::LatencyCalibrator(WeightFilter* weightFilter, MotorController* motor)
    : weightFilter_(weightFilter), motor_(motor), state_(CalibratorState::IDLE),
      targetDoseG_(0.0f), current_{}, flowZeroStreak_(0), streakStartMs_(0), trialCount_(0),
      unknownCommandHandler_(nullptr) {}

void LatencyCalibrator::setUnknownCommandHandler(UnknownCommandHandler handler) {
    unknownCommandHandler_ = handler;
}

void LatencyCalibrator::update() {
    // HANYA command Serial sekarang -- logic state-machine grinding/
    // coasting dipindah ke onWeightSample(), dipanggil main.cpp per
    // sample BLE valid, bukan di sini. Lihat catatan arsitektur di
    // latency_calibrator.h.
    handleSerialInput();
}

void LatencyCalibrator::onWeightSample(float rawWeightG, unsigned long sampleTimestampMs) {
    switch (state_) {
        case CalibratorState::GRINDING:
            checkGrindProgress(rawWeightG, sampleTimestampMs);
            break;
        case CalibratorState::COASTING:
            checkCoastProgress(rawWeightG, sampleTimestampMs);
            break;
        case CalibratorState::IDLE:
        default:
            break;
    }
}

void LatencyCalibrator::handleSerialInput() {
    if (!Serial.available()) {
        return;
    }

    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
        return;
    }

    if (line.startsWith("g ") || line.startsWith("G ")) {
        float target = line.substring(2).toFloat();
        if (target <= 0.0f) {
            Serial.println("[CALIB] Target tidak valid, harus > 0. Contoh: g 24.0");
            return;
        }
        if (state_ == CalibratorState::GRINDING || state_ == CalibratorState::COASTING) {
            Serial.println("[CALIB] Masih ada sesi kalibrasi berjalan, tunggu selesai (atau kirim 'x' untuk batal).");
            return;
        }
        startGrind(target);
    } else if (line == "x" || line == "X") {
        if (state_ == CalibratorState::GRINDING || state_ == CalibratorState::COASTING) {
            Serial.println("[CALIB] Dibatalkan manual, mengirim motor OFF.");
            motor_->stop();
            state_ = CalibratorState::IDLE;
        }
    } else if (line == "r" || line == "R") {
        printAllTrials();
    } else if (line == "c" || line == "C") {
        clearTrials();
    } else if (unknownCommandHandler_ != nullptr) {
        unknownCommandHandler_(line);
    } else {
        Serial.println("[CALIB] Perintah: 'g <target_gram>' mulai, 'x' batal, 'r' lihat semua trial, 'c' hapus riwayat.");
    }
}

void LatencyCalibrator::startGrind(float targetDoseG) {
    targetDoseG_ = targetDoseG;
    flowZeroStreak_ = 0;

    // Poin 1/8: tare eksplisit DIPAKAI SEBAGAI OFFSET, bukan cuma
    // dicatat untuk laporan. targetAbsoluteG = startWeight + dose --
    // kalau timbangan sudah 0.00 sebelum mulai, ini tidak berefek;
    // kalau tidak (mis. ada wadah kosong belum ditara), target tetap
    // berarti "dose tambahan dari kondisi awal".
    float startWeight = weightFilter_->hasSample() ? weightFilter_->latestWeight() : 0.0f;
    if (!weightFilter_->hasSample()) {
        Serial.println("[CALIB] PERINGATAN: belum ada sample berat sama sekali -- tare dicatat sebagai 0.0g, mungkin tidak akurat.");
    }

    current_ = CalibrationTrial{};
    current_.targetDoseG = targetDoseG;
    current_.startWeightG = startWeight;
    current_.targetAbsoluteG = startWeight + targetDoseG;
    current_.sampleCount = 0;

    Serial.printf("[CALIB] Dose %.2f g (tare awal %.2f g, target absolut %.2f g) -- mengirim motor ON.\n",
                  targetDoseG, startWeight, current_.targetAbsoluteG);
    MotorResult r = motor_->start();
    if (!r.success) {
        // KONSISTENSI SAFETY dengan GrindController (per review): "ON
        // gagal terkirim" TIDAK BOLEH diasumsikan berarti motor pasti
        // mati -- response bisa hilang PADAHAL command sempat sampai
        // dan motor sempat menyala (sama seperti alasan doAbort() di
        // GrindController, lihat grind_controller.cpp). Draft
        // sebelumnya di sini TIDAK force-off, cuma batal sesi --
        // untuk GpioMotorController saat ini r.success SELALU true
        // (lihat motor_controller.h), jadi baris ini praktis tidak
        // pernah terpicu SEKARANG, TAPI kalau MotorController lain
        // yang bisa gagal dipakai lagi di masa depan (mis. HTTP-based),
        // invariant safety ini WAJIB ada supaya tidak diam-diam
        // menyisakan motor menyala.
        Serial.println("[CALIB] Motor ON gagal terkirim -- force-off sebagai jaga-jaga, batal sesi.");
        motor_->stop();
        return;
    }

    state_ = CalibratorState::GRINDING;
}

void LatencyCalibrator::checkGrindProgress(float rawWeightG, unsigned long sampleTimestampMs) {
    (void)rawWeightG;         // tidak dipakai di GRINDING -- keputusan stop HARUS pakai weightFilter (filtered), bukan raw yang belum tervalidasi (lihat catatan di bawah)
    (void)sampleTimestampMs;  // belum dipakai di GRINDING -- tersedia untuk konsistensi API & kebutuhan masa depan (mis. catat kapan spesifik target tercapai)

    if (!weightFilter_->hasSample()) {
        return;
    }

    // PENTING: keputusan "target tercapai, kirim OFF" tetap pakai
    // weightFilter_->latestWeight() (FILTERED), BUKAN rawWeightG.
    // Kontrol motor tidak boleh bereaksi ke satu sample raw yang belum
    // tervalidasi (bisa jadi outlier/noise sensor) -- WeightFilter
    // sudah punya logic khusus untuk itu (lihat weight_filter.h).
    // Raw weight hanya dipakai untuk TRAJECTORY LOGGING di COASTING
    // (recordCoastSample()), bukan untuk keputusan kontrol di sini.
    float currentWeight = weightFilter_->latestWeight();
    if (currentWeight >= current_.targetAbsoluteG) {
        current_.weightAtStop = currentWeight;

        // Flow rate PERSIS sebelum OFF -- data penting untuk analisis
        // hubungan flow-saat-off vs coast-weight nanti (poin 8 di header).
        FlowRateResult flowNow = weightFilter_->computeFlowRate();
        current_.flowAtStop = flowNow.valid ? flowNow.flowRateGps : NAN;

        Serial.printf("[CALIB] Berat %.2f g >= target absolut %.2f g (flow %.2f g/s) -- mengirim motor OFF.\n",
                      currentWeight, current_.targetAbsoluteG,
                      flowNow.valid ? flowNow.flowRateGps : -1.0f);

        MotorResult r = motor_->stop();
        if (!r.success) {
            Serial.println("[CALIB] motor OFF gagal terkirim! Coast time di bawah dihitung dari titik command DIKIRIM, bukan diam-diam batal.");
        }
        current_.stopCommandMs = r.commandSentMs;

        flowZeroStreak_ = 0;
        streakStartMs_ = 0;
        current_.sampleCount = 0;
        state_ = CalibratorState::COASTING;
    }
}

void LatencyCalibrator::recordCoastSample(float rawWeightG, unsigned long sampleTimestampMs) {
    if (current_.sampleCount >= CalibrationTrial::MAX_SAMPLES_PER_TRIAL) {
        return;  // trajectory buffer penuh -- kehilangan sample terbaru, bukan yang tertua (coast biasanya makin stabil di akhir, awal lebih penting)
    }

    // rawWeightG disimpan APA ADANYA -- ini nilai langsung dari BLE
    // (RawSample.weight), bukan weightFilter.latestWeight(). flowGps
    // tetap dari WeightFilter, karena flow rate memang tujuan
    // WeightFilter dibuat -- lihat catatan pemisahan raw/filtered di
    // latency_calibrator.h.
    FlowRateResult flow = weightFilter_->computeFlowRate();
    CoastSample s;
    s.timestampMs = sampleTimestampMs;  // timestamp BLE ASLI, bukan millis() waktu loop() memproses
    s.rawWeightG = rawWeightG;
    s.flowValid = flow.valid;
    s.flowGps = flow.valid ? flow.flowRateGps : NAN;

    current_.samples[current_.sampleCount] = s;
    current_.sampleCount++;
}

void LatencyCalibrator::checkCoastProgress(float rawWeightG, unsigned long sampleTimestampMs) {
    if (!weightFilter_->hasSample()) {
        return;
    }

    // Poin 2: rekam trajectory PENUH per sample BLE asli (raw weight),
    // bukan cuma titik akhir dan bukan per loop() iteration.
    recordCoastSample(rawWeightG, sampleTimestampMs);

    // Deteksi zero-flow tetap pakai WeightFilter -- ini memang
    // tujuannya (computeFlowRate() perlu ring buffer terfilter untuk
    // hasil yang stabil, bukan raw sample yang bisa noise).
    FlowRateResult flow = weightFilter_->computeFlowRate();

    if (!flow.valid) {
        // Sample belum representatif -- jangan hitung sebagai "flow nol"
        // ATAUPUN reset streak yang sudah terbentuk secara paksa; window
        // yang belum penuh bukan berarti flow berubah, cuma belum bisa
        // dipastikan. Diamkan streak apa adanya, tunggu sample berikutnya.
        return;
    }

    if (fabsf(flow.flowRateGps) <= FLOW_ZERO_THRESHOLD_GPS) {
        if (flowZeroStreak_ == 0) {
            // Sample BLE PERTAMA dari streak stabil -- catat timestamp
            // aslinya (poin 3/7: basis flowZeroEstimatedMs, bukan
            // timestamp sample TERAKHIR/waktu loop() memproses).
            streakStartMs_ = sampleTimestampMs;
        }
        flowZeroStreak_++;
    } else {
        flowZeroStreak_ = 0;
    }

    // Poin 3: FLOW_ZERO_STABLE_SAMPLES sekarang benar-benar menghitung
    // N sample BLE berturut-turut (karena checkCoastProgress dipanggil
    // sekali per sample lewat onWeightSample(), bukan sekali per
    // loop() iteration) -- tidak lagi bergantung rate loop() vs rate BLE.
    if (flowZeroStreak_ >= FLOW_ZERO_STABLE_SAMPLES) {
        finishTrial(sampleTimestampMs);
    }
}

void LatencyCalibrator::finishTrial(unsigned long sampleTimestampMs) {
    // flowZeroDetectedMs = timestamp BLE sample TERAKHIR dari streak
    // (sampleTimestampMs saat ini) -- kapan sistem MENDETEKSI flow
    // sudah nol. flowZeroEstimatedMs = timestamp BLE sample PERTAMA
    // dari streak (streakStartMs_) -- estimasi lebih dekat ke kejadian
    // asli. Keduanya sekarang timestamp BLE asli, bukan millis() loop().
    current_.flowZeroDetectedMs = sampleTimestampMs;
    current_.flowZeroEstimatedMs = streakStartMs_;
    current_.finalWeight = weightFilter_->latestWeight();

    current_.coastWeightG = current_.finalWeight - current_.weightAtStop;
    current_.coastTimeDetectedMs = current_.flowZeroDetectedMs - current_.stopCommandMs;
    current_.coastTimeEstimatedMs = current_.flowZeroEstimatedMs - current_.stopCommandMs;

    printTrial(current_);

    if (trialCount_ < MAX_TRIALS) {
        history_[trialCount_] = current_;
        trialCount_++;
        Serial.printf("[CALIB] Trial disimpan (%d/%d). Kirim 'r' untuk lihat semua, atau 'g <target>' lagi.\n",
                      (int)trialCount_, (int)MAX_TRIALS);
    } else {
        Serial.println("[CALIB] Riwayat penuh (10 trial) -- trial ini TIDAK disimpan. Kirim 'c' untuk hapus riwayat dulu.");
    }

    state_ = CalibratorState::IDLE;
}

void LatencyCalibrator::printTrial(const CalibrationTrial& t) const {
    Serial.println("----------------------------------------");
    Serial.printf("  Dose target   : %.2f g (tare awal %.2f g, target absolut %.2f g)\n",
                  t.targetDoseG, t.startWeightG, t.targetAbsoluteG);
    Serial.printf("  Berat saat OFF: %.2f g  (flow saat itu: %s)\n",
                  t.weightAtStop, isnan(t.flowAtStop) ? "n/a" : String(t.flowAtStop, 2).c_str());
    Serial.printf("  Berat akhir   : %.2f g\n", t.finalWeight);
    Serial.printf("  Coast weight  : %.2f g\n", t.coastWeightG);
    Serial.printf("  Coast time (terdeteksi) : %lu ms  -- waktu sampai SISTEM YAKIN flow=0 (bias tinggi, termasuk window+streak delay)\n",
                  t.coastTimeDetectedMs);
    Serial.printf("  Coast time (estimasi)   : %lu ms  -- estimasi waktu SUNGGUHAN flow mulai ~0 (lebih dekat kejadian asli)\n",
                  t.coastTimeEstimatedMs);
    Serial.printf("  Jumlah sample trajectory tersimpan: %d\n", (int)t.sampleCount);
}

void LatencyCalibrator::printAllTrials() const {
    if (trialCount_ == 0) {
        Serial.println("[CALIB] Belum ada trial tersimpan.");
        return;
    }

    Serial.println("========================================");
    Serial.printf("[CALIB] RINGKASAN %d TRIAL\n", (int)trialCount_);
    Serial.println("========================================");

    float minCoastW = 0.0f, maxCoastW = 0.0f;
    unsigned long minCoastT = 0, maxCoastT = 0;
    bool first = true;

    for (size_t i = 0; i < trialCount_; i++) {
        Serial.printf("\n[Trial %d]\n", (int)i + 1);
        printTrial(history_[i]);

        if (first) {
            minCoastW = maxCoastW = history_[i].coastWeightG;
            minCoastT = maxCoastT = history_[i].coastTimeEstimatedMs;
            first = false;
        } else {
            if (history_[i].coastWeightG < minCoastW) minCoastW = history_[i].coastWeightG;
            if (history_[i].coastWeightG > maxCoastW) maxCoastW = history_[i].coastWeightG;
            if (history_[i].coastTimeEstimatedMs < minCoastT) minCoastT = history_[i].coastTimeEstimatedMs;
            if (history_[i].coastTimeEstimatedMs > maxCoastT) maxCoastT = history_[i].coastTimeEstimatedMs;
        }
    }

    Serial.println("\n========================================");
    Serial.printf("[CALIB] Rentang coast weight (estimasi): %.2f -- %.2f g\n", minCoastW, maxCoastW);
    Serial.printf("[CALIB] Rentang coast time (estimasi)  : %lu -- %lu ms\n", minCoastT, maxCoastT);
    Serial.println("[CALIB] Analisis hubungan flow-saat-OFF vs coast-weight: lihat data mentah di atas per trial.");
    Serial.println("[CALIB] Ini dataset karakteristik, BUKAN satu konstanta -- pakai untuk desain predictive-stop, bukan look-up value tunggal.");
    Serial.println("========================================");
}

void LatencyCalibrator::clearTrials() {
    trialCount_ = 0;
    Serial.println("[CALIB] Riwayat trial dihapus.");
}
