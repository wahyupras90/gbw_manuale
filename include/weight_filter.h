#pragma once

#include <Arduino.h>

// ============================================================
// weight_filter -- validasi sample + flow-rate estimator
// ============================================================
//   raw weight (HX711)
//        |
//        v
//   validasi (NaN, delta absurd berbasis Δt)
//        |
//        v
//   ring buffer 16 sample (weight + timestamp)
//        |
//        v
//   window waktu tetap (WEIGHT_FILTER_FLOW_WINDOW_MS)
//        |
//        v
//   flow_rate_gps = slope OLS (ordinary least-squares) linear
//                   regression atas SEMUA sample valid di window,
//                   unweighted -- lihat computeFlowRate() untuk
//                   detail & alasan (KOREKSI dari versi awal yang
//                   cuma pakai 2 titik/deltaWeight-deltaTime, rentan
//                   ke noise HX711 pada satu sample manapun)
//
// Kalau sample dalam window tidak cukup (rentang waktu terlalu
// pendek, ATAU kurang dari 2 titik), flow_rate_valid = false --
// TIDAK memaksakan angka dan TIDAK fallback ke N-sample tetap. Ini
// keputusan sadar: baseline harus sesederhana mungkin supaya hasil
// uji overshoot aktual di grinder asli bisa dipakai untuk memutuskan
// apakah window/smoothing tambahan (EMA, weighted regression, dst)
// perlu ditambah -- bukan ditebak di depan. TIDAK ADA smoothing lain
// selain regresi OLS ini di baseline saat ini.
// ============================================================

// Lebar window waktu untuk hitung flow-rate. 250ms sebagai default;
// ini yang paling mungkin disesuaikan setelah lihat data empiris di
// STEP 5 (misal naik ke 300-400ms kalau flow-rate masih terlalu noisy).
static const unsigned long WEIGHT_FILTER_FLOW_WINDOW_MS = 250;

// Rentang waktu minimum di dalam window supaya flow-rate dianggap
// valid untuk dihitung. Kalau window 250ms cuma berisi sample dalam
// rentang <MIN_WINDOW_SPAN_MS (mis. BLE notify rate ternyata jarang),
// flow_rate_valid = false alih-alih memaksakan angka dari Δt yang
// terlalu kecil (rawan dibagi angka mendekati nol / noise dominan).
static const unsigned long WEIGHT_FILTER_MIN_WINDOW_SPAN_MS = 60;

// Margin pengali di atas GRIND_FLOW_RATE_MAX_SANE_GPS saat menghitung
// batas delta-per-sample. Delta gram yang "wajar" tergantung Δt sejak
// sample sebelumnya (lihat pushRawSample()), margin ini kasih sedikit
// toleransi supaya sample sah di ambang batas tidak ke-reject gara-gara
// noise kecil.
static const float WEIGHT_FILTER_DELTA_MARGIN = 1.3f;

// Floor batas bawah untuk maxDeltaG. Tanpa ini, saat Δt kecil (notify
// BLE cepat, mis. 20ms) batas delta bisa jatuh ke angka sangat kecil
// (mis. 0.078g) dan mulai menolak sample SAH yang kebetulan berubah
// cepat -- ini justru mencemari data karakterisasi yang jadi tujuan
// STEP 2 (kita mau ukur MyScale apa adanya, bukan meng-clip-nya lewat
// filter yang terlalu galak). 0.10g adalah nilai TRIAL, bukan final --
// disesuaikan lagi setelah lihat log nyata dari grinder.
static const float WEIGHT_FILTER_MIN_DELTA_G = 0.10f;

struct WeightSample {
    float weight;
    unsigned long timestampMs;
};

struct FlowRateResult {
    bool valid;
    float flowRateGps;   // gram per detik, hanya valid kalau `valid==true`
    float windowSpanMs;  // rentang waktu aktual yang dipakai (untuk debug/log)
};

// ------------------------------------------------------------
// WeightFilter -- ring buffer + validasi + flow-rate estimator.
// Satu instance per sumber berat (di trial ini: satu-satunya
// instance untuk Myscale BLE).
// ------------------------------------------------------------
class WeightFilter {
public:
    WeightFilter();

    // Proses satu sample mentah dari BLE. Return true kalau sample
    // diterima (lolos validasi) dan masuk ring buffer; false kalau
    // ditolak (NaN atau delta absurd).
    // maxSaneFlowGps: dari GRIND_FLOW_RATE_MAX_SANE_GPS (config.h) --
    // dilewatkan sebagai parameter, bukan hardcode, supaya modul ini
    // tidak bergantung langsung ke config.h (gampang dites terpisah).
    bool pushRawSample(float rawWeight, unsigned long timestampMs, float maxSaneFlowGps);

    // Hitung flow-rate dari window waktu tetap: buffer ditelusuri dari
    // sample TERBARU mundur ke yang lebih lama, berhenti begitu sample
    // sudah lebih tua dari WEIGHT_FILTER_FLOW_WINDOW_MS dari sample
    // terbaru. Flow = (weight terbaru - weight tertua yang MASIH di
    // dalam window) / (selisih waktu keduanya).
    FlowRateResult computeFlowRate() const;

    // Berat valid paling akhir yang diterima (bukan raw asli dari BLE,
    // tapi sample yang sudah lolos validasi -- ini yang harus dipakai
    // GrindController nanti, bukan currentWeight mentah).
    float latestWeight() const;
    bool hasSample() const;

    // Kosongkan buffer & baseline delta-check SEPENUHNYA -- WAJIB
    // dipanggil tepat setelah HX711Reader::setCalibration() dengan
    // offset baru (auto-tare, lihat grind_start() di main.cpp).
    // Tanpa ini, sample pertama pasca-tare bisa dibandingkan ke
    // lastWeight_ dari SEBELUM tare (basis offset lama) di
    // pushRawSample() -- selisihnya bisa melebihi maxDeltaG (delta
    // outlier absurd) dan DITOLAK terus-menerus (lastWeight_/
    // hasLastSample_ tidak pernah ter-update saat sample ditolak,
    // lihat komentar di pushRawSample()), sehingga hasSample()/
    // computeFlowRate() macet memakai data basi sampai drift alami
    // kebetulan kembali dekat nilai lama.
    void reset();

private:
    static const size_t BUFFER_SIZE = 16;
    WeightSample samples_[BUFFER_SIZE];
    size_t head_;   // index penulisan berikutnya
    size_t count_;  // jumlah sample valid (maks BUFFER_SIZE)

    bool hasLastSample_;
    float lastWeight_;
    unsigned long lastTimestampMs_;

    void pushToBuffer(float weight, unsigned long timestampMs);
};
