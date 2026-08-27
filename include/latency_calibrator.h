#pragma once

#include <Arduino.h>
#include "weight_filter.h"
#include "motor_controller.h"

// ============================================================
// LatencyCalibrator -- bangun dataset karakteristik coast/decay
// ============================================================
// Tujuan direvisi (sesuai review): BUKAN mencari satu angka
// MOTOR_RESPONSE_LATENCY_MS_DEFAULT, tapi membangun DATASET
// karakteristik coast grinder -- trajectory berat penuh pasca-OFF,
// dari beberapa trial, supaya predictive-stop nanti bisa dianalisis
// dari hubungan flow-rate-saat-OFF -> coast-weight/time, bukan
// diasumsikan konstan.
//
// TIDAK mengukur spin-up latency (microswitch ditekan -> motor mulai
// giling) -- microswitch masih manual, jeda operator tidak
// representatif untuk dijadikan data.
//
// ARSITEKTUR EVENT PER-SAMPLE (revisi terbaru, per review):
// Sebelumnya kalibrator dipanggil sekali per loop() iteration lewat
// update(), yang membaca weightFilter.latestWeight()/computeFlowRate()
// SEKALI meskipun beberapa sample BLE mungkin sudah masuk & diproses
// WeightFilter dalam iterasi loop() yang sama (drainRawSampleQueue()
// bisa pop() beberapa sample sekaligus). Akibatnya:
//   - flowZeroStreak_ (harusnya "N sample BLE berturut flow~0") pada
//     praktiknya menghitung "N kali update() dipanggil" -- BEDA kalau
//     rate BLE != rate loop(), atau queue sempat menumpuk lalu di-drain
//     sekaligus.
//   - CoastSample.timestampMs pakai millis() (waktu loop() memproses),
//     BUKAN timestamp BLE notify yang presisi (RawSample.timestampMs)
//     yang sudah susah payah kita jaga dari notifyCallback() sampai ke
//     WeightFilter.
// Sekarang: main.cpp memanggil onWeightSample(sampleTimestampMs) SEKALI
// PER SAMPLE VALID yang berhasil masuk WeightFilter (bukan sekali per
// loop() iteration), sesudah drainRawSampleQueue() memproses tiap
// sample satu-satu. update() tetap ada untuk command Serial (baca input
// non-blocking), tapi logic state-machine grinding/coasting sekarang
// murni digerakkan oleh onWeightSample(), dengan timestamp asli dari
// BLE notify -- bukan dari kapan loop() kebetulan sempat memprosesnya.
//
// Perbaikan dari versi sebelumnya (per review):
//   1. Tare eksplisit -- startWeightG_ dicatat SEBELUM motor ON, DAN
//      dipakai sebagai OFFSET ke target (targetAbsoluteG_ = startWeightG_
//      + targetDoseG_) -- bukan cuma dicatat untuk laporan. Kalau
//      timbangan sudah 0.00 sebelum mulai, ini tidak berefek; kalau
//      tidak (mis. ada wadah kosong yang belum ditara), target tetap
//      berarti "dose tambahan dari kondisi awal", bukan "berat absolut".
//   2. Trajectory penuh disimpan selama COASTING (weight+flow+waktu
//      per sample BLE ASLI, bukan per loop() iteration).
//   3. Bias sistematis dari window (250ms) + stability streak (5
//      sample BLE ASLI) dikoreksi: waktu TERDETEKSI stabil
//      (flowZeroDetectedMs) dicatat terpisah dari ESTIMASI waktu motor
//      sungguhan berhenti (flowZeroEstimatedMs = timestamp SAMPLE BLE
//      pertama dari streak stabil, bukan waktu loop() memprosesnya).
//   7. Riwayat multi-trial disimpan in-memory (maks MAX_TRIALS),
//      bisa dicetak ulang kapan saja via command 'r'.
//
// Poin 4 (race condition BLE->WeightFilter) & 6 (matikan BLE dump
// saat eksperimen) ditangani di main.cpp/config.h, bukan di sini --
// lihat raw_sample_queue.h dan DEBUG_BLE_PACKET.
//
// PEMISAHAN RAW VS FILTERED (perubahan terakhir sebelum hardware test):
// CoastSample.rawWeightG menyimpan nilai LANGSUNG dari BLE (raw, sebelum
// WeightFilter validasi/filter), BUKAN weightFilter.latestWeight().
// WeightFilter tetap dipakai untuk yang memang jadi tujuannya --
// computeFlowRate() dan deteksi zero-flow (state machine coasting) --
// tapi trajectory yang DISIMPAN untuk analisis nanti harus mencerminkan
// apa yang sebenarnya dikirim BLE, bukan versi yang sudah lolos filter.
// Ini penting karena analisis nanti (STEP 5 dst) mungkin perlu tahu
// persis sample mana yang ditolak filter dan kenapa -- kalau trajectory
// cuma simpan hasil filter, informasi itu hilang.
//
// Alur kerja (arsitektur FINAL -- relay dipasang paralel microswitch,
// microswitch TIDAK PERLU ditekan manual lagi, koreksi dari komentar
// versi lama di sini yang masih mengasumsikan operator menekan
// microswitch fisik secara manual setelah motor_->start() dipanggil --
// itu sudah TIDAK BERLAKU sejak firmware pindah ke GpioMotorController
// + relay paralel microswitch, lihat motor_controller.h/WIRING.md):
//   1. Operator kirim "g <target>" via Serial (target = DOSE tambahan,
//      lihat poin 1 tare di atas)
//   2. Kalibrator catat startWeightG_ (tare) & targetAbsoluteG_, lalu
//      motorController.start() -- INI SUDAH LANGSUNG mengaktifkan
//      relay yang menekan microswitch secara elektrik, TIDAK ADA aksi
//      manual operator yang diperlukan di sini.
//   3. main.cpp panggil onWeightSample() tiap sample HX711 valid masuk
//      WeightFilter (lewat polling langsung, lihat main.cpp)
//   4. Berat >= targetAbsoluteG_ -> motorController.stop(), catat
//      stopCommandMs_, weightAtStop_, flowAtStop_ (flow rate PERSIS
//      sebelum OFF -- buat analisis hubungan flow-saat-off vs
//      coast-weight nanti)
//   5. Kalibrator TERUS terima onWeightSample() + REKAM trajectory
//      (recordCoastSample()) tiap sample baru, sampai flow settle
//   6. Flow <= threshold selama N sample berturut -> selesai, simpan
//      trial ke history, cetak ringkasan
//   7. Operator ulangi beberapa kali ('g <target>' lagi), lalu 'r'
//      untuk cetak ringkasan semua trial + analisis kasar
// ============================================================

enum class CalibratorState {
    IDLE,
    GRINDING,
    COASTING
};

// Satu titik data dalam trajectory coast (dari saat OFF sampai settle).
struct CoastSample {
    unsigned long timestampMs;   // timestamp BLE asli, sama basis dengan stopCommandMs
    float rawWeightG;             // RAW weight langsung dari BLE (parseMyscaleWeight), SEBELUM WeightFilter -- lihat catatan arsitektur di atas
    float flowGps;                 // flow rate saat itu, dari WeightFilter (NAN kalau belum valid -- lihat catatan di .cpp)
    bool flowValid;
};

// Hasil satu trial kalibrasi lengkap.
struct CalibrationTrial {
    float targetDoseG;            // dose yang diminta operator ("g <target>")
    float startWeightG;          // tare -- berat SEBELUM motor ON
    float targetAbsoluteG;        // startWeightG + targetDoseG -- ini yang dibandingkan ke berat aktual
    float weightAtStop;           // berat persis saat command OFF dikirim
    float flowAtStop;             // flow rate persis sebelum OFF (NAN kalau tidak valid)
    unsigned long stopCommandMs;  // timestamp OFF dikirim (basis waktu t=0 relatif)

    float finalWeight;
    unsigned long flowZeroDetectedMs;   // timestamp BLE sample TERAKHIR dari streak stabil (kapan sistem MENDETEKSI flow sudah nol)
    unsigned long flowZeroEstimatedMs;  // timestamp BLE sample PERTAMA dari streak stabil -- ESTIMASI lebih dekat ke kejadian asli (lihat poin 3 di atas)

    float coastWeightG;            // finalWeight - weightAtStop
    unsigned long coastTimeDetectedMs;   // flowZeroDetectedMs - stopCommandMs (bias tinggi, technically "waktu sampai sistem YAKIN berhenti")
    unsigned long coastTimeEstimatedMs;  // flowZeroEstimatedMs - stopCommandMs (bias lebih rendah, estimasi lebih dekat waktu sungguhan)

    size_t sampleCount;             // jumlah CoastSample yang tersimpan untuk trial ini
    static const size_t MAX_SAMPLES_PER_TRIAL = 60;  // ~9 detik pada notify rate ~150ms -- cukup untuk coast normal
    CoastSample samples[MAX_SAMPLES_PER_TRIAL];
};

class LatencyCalibrator {
public:
    LatencyCalibrator(WeightFilter* weightFilter, MotorController* motor);

    // Dipanggil tiap loop() -- HANYA menangani command Serial (baca
    // input non-blocking). TIDAK lagi menjalankan logic state-machine
    // grinding/coasting -- itu sekarang murni digerakkan oleh
    // onWeightSample() supaya granularitasnya per-sample BLE asli,
    // bukan per-iterasi loop(). Lihat catatan arsitektur di atas.
    void update();

    // Dipanggil dari main.cpp SEKALI PER SAMPLE VALID yang berhasil
    // masuk WeightFilter (sesudah weightFilter.pushRawSample() return
    // true). Sumber sample bisa BLE (lewat RawSampleQueue+drainRawSampleQueue(),
    // lihat versi Tasmota/Myscale) ATAU HX711 (polling langsung di
    // loop(), lihat main.cpp versi hardware final ini) -- modul ini
    // AGNOSTIC terhadap sumbernya, cuma terima angka berat + timestamp.
    //   rawWeightG: nilai RAW SEBELUM WeightFilter -- ini yang disimpan
    //     ke CoastSample trajectory (lihat catatan arsitektur di atas).
    //   sampleTimestampMs: timestamp ASLI dari BLE notify
    //     (RawSample.timestampMs), BUKAN millis() saat ini/waktu loop()
    //     memprosesnya -- ini yang menjaga presisi temporal trajectory.
    void onWeightSample(float rawWeightG, unsigned long sampleTimestampMs);

    // Cetak ringkasan seluruh trial yang tersimpan + analisis kasar
    // (rentang coastWeight, rentang coastTime, korelasi kasar dengan
    // flowAtStop). Dipanggil dari command Serial 'r'.
    void printAllTrials() const;

    // Hapus semua riwayat trial. Command Serial 'c'.
    void clearTrials();

    // Hook untuk command Serial yang tidak dikenali kalibrator ini
    // (mis. 'd' untuk rescan Tasmota discovery) -- supaya main.cpp bisa
    // menambah command app-level tanpa LatencyCalibrator perlu tahu
    // soal TasmotaDiscovery/konsep di luar tanggung jawabnya. Kalibrator
    // tetap satu-satunya pembaca Serial (menghindari konflik dua
    // consumer buffer Serial yang sama).
    typedef void (*UnknownCommandHandler)(const String& line);
    void setUnknownCommandHandler(UnknownCommandHandler handler);

    // ------------------------------------------------------------
    // Akses read-only ke riwayat trial -- dipakai GrindController untuk
    // regresi coastWeight~flowAtStop. LatencyCalibrator TETAP satu-
    // satunya pemilik data (tidak ada duplikasi/copy tersembunyi di
    // tempat lain) -- GrindController baca lewat sini setiap kali mau
    // hitung ulang regresi, bukan menyimpan salinan sendiri yang bisa
    // basi.
    // ------------------------------------------------------------
    size_t trialCount() const { return trialCount_; }
    const CalibrationTrial& trial(size_t index) const { return history_[index]; }

private:
    WeightFilter* weightFilter_;
    MotorController* motor_;

    CalibratorState state_;
    float targetDoseG_;

    // Trial yang sedang berjalan (belum masuk history sampai selesai)
    CalibrationTrial current_;

    static const size_t FLOW_ZERO_STABLE_SAMPLES = 5;
    static constexpr float FLOW_ZERO_THRESHOLD_GPS = 0.15f;
    size_t flowZeroStreak_;
    unsigned long streakStartMs_;  // timestamp BLE SAMPLE PERTAMA dalam streak stabil saat ini -- basis flowZeroEstimatedMs

    static const size_t MAX_TRIALS = 10;
    CalibrationTrial history_[MAX_TRIALS];
    size_t trialCount_;

    UnknownCommandHandler unknownCommandHandler_;

    void handleSerialInput();
    void startGrind(float targetDoseG);
    void checkGrindProgress(float rawWeightG, unsigned long sampleTimestampMs);
    void checkCoastProgress(float rawWeightG, unsigned long sampleTimestampMs);
    void finishTrial(unsigned long sampleTimestampMs);
    void recordCoastSample(float rawWeightG, unsigned long sampleTimestampMs);
    void printTrial(const CalibrationTrial& t) const;
};
