#include "weight_filter.h"
#include <math.h>

WeightFilter::WeightFilter()
    : head_(0), count_(0), hasLastSample_(false), lastWeight_(0.0f), lastTimestampMs_(0) {}

void WeightFilter::pushToBuffer(float weight, unsigned long timestampMs) {
    samples_[head_] = {weight, timestampMs};
    head_ = (head_ + 1) % BUFFER_SIZE;
    if (count_ < BUFFER_SIZE) {
        count_++;
    }
}

bool WeightFilter::pushRawSample(float rawWeight, unsigned long timestampMs, float maxSaneFlowGps) {
    // 1) Buang NaN / non-finite (data BLE tidak lengkap, lihat weight_parser.h)
    if (isnan(rawWeight) || !isfinite(rawWeight)) {
        return false;
    }

    // 2) Buang lonjakan absurd -- threshold delta gram dihitung dari
    //    Δt sejak sample valid terakhir, BUKAN angka gps langsung.
    //    Kalau notify BLE datang jarang-jarang, delta gram yang wajar
    //    ikut lebih besar; pakai gps sebagai batas delta tetap akan
    //    salah-tolak sample sah saat Δt melebar (lihat diskusi di
    //    percakapan project).
    //
    //    Floor WEIGHT_FILTER_MIN_DELTA_G dipakai supaya saat Δt kecil
    //    (notify cepat), threshold tidak jatuh ke angka yang lebih
    //    kecil dari noise wajar timbangan -- kalau tidak, filter mulai
    //    menolak sample SAH dan mencemari data karakterisasi STEP 2.
    if (hasLastSample_) {
        unsigned long dtMs = timestampMs - lastTimestampMs_;  // asumsi millis() tidak wrap dalam sesi trial
        float dtSec = dtMs / 1000.0f;
        float maxDeltaG = maxSaneFlowGps * dtSec * WEIGHT_FILTER_DELTA_MARGIN;
        if (maxDeltaG < WEIGHT_FILTER_MIN_DELTA_G) {
            maxDeltaG = WEIGHT_FILTER_MIN_DELTA_G;
        }

        float deltaG = fabsf(rawWeight - lastWeight_);
        if (deltaG > maxDeltaG) {
            // Outlier -- tolak sample ini, JANGAN update lastWeight_/lastTimestampMs_
            // supaya sample berikutnya tetap dibandingkan terhadap baseline yang sah,
            // bukan terhadap outlier yang baru saja ditolak.
            return false;
        }
    }

    lastWeight_ = rawWeight;
    lastTimestampMs_ = timestampMs;
    hasLastSample_ = true;

    pushToBuffer(rawWeight, timestampMs);
    return true;
}

float WeightFilter::latestWeight() const {
    return lastWeight_;
}

bool WeightFilter::hasSample() const {
    return hasLastSample_;
}

FlowRateResult WeightFilter::computeFlowRate() const {
    FlowRateResult result{false, 0.0f, 0.0f};

    if (count_ < 2) {
        return result;  // belum cukup sample sama sekali
    }

    // Ring buffer disusun secara logis dari yang paling lama ke paling
    // baru dengan menelusuri mundur dari head_. Sample "terakhir" adalah
    // yang paling baru masuk (index head_-1, wrap-around).
    size_t newestIdx = (head_ + BUFFER_SIZE - 1) % BUFFER_SIZE;
    unsigned long newestTs = samples_[newestIdx].timestampMs;

    // ------------------------------------------------------------
    // KOREKSI (per keputusan terkunci -- baca sebelum mengubah lagi):
    // versi SEBELUMNYA cuma pakai 2 titik (sample tertua & terbaru di
    // window) untuk hitung flow = deltaWeight/deltaTime. Noise HX711
    // pada SATU sample (baik yang tertua maupun terbaru) langsung
    // mencemari seluruh estimasi flow, karena tidak ada titik lain
    // yang meredam pengaruhnya.
    //
    // SEKARANG: OLS (ordinary least-squares) linear regression atas
    // SEMUA sample yang ada di window aktif (WEIGHT_FILTER_FLOW_WINDOW_MS,
    // TIDAK diubah) -- weight = a*time + b, slope 'a' dipakai sebagai
    // flowRateGps. Ini HANYA mengganti metode estimasi, BUKAN
    // memperlebar/mempersempit window itu sendiri, BUKAN menambah
    // smoothing/EMA terpisah di atas regresi ini (unweighted, semua
    // sample berbobot sama -- keputusan sadar, bukan langkah sementara).
    //
    // TIDAK mengubah: state machine, timing motor, predictive-stop,
    // pulse duration, ataupun WeightFilter secara arsitektural (ring
    // buffer, validasi pushRawSample(), window constants semua tetap).
    // ------------------------------------------------------------

    // Pass 1: kumpulkan index seluruh sample di dalam window
    // [newestTs - WEIGHT_FILTER_FLOW_WINDOW_MS, newestTs], sekaligus
    // hitung windowSpanMs dari sample tertua yang ditemukan -- identik
    // dengan logika sebelumnya, cuma sekarang SEMUA index di window
    // disimpan (bukan cuma titik tertua) untuk dipakai regresi.
    size_t windowIdx[BUFFER_SIZE];
    size_t windowN = 0;
    unsigned long oldestTsInWindow = newestTs;

    size_t idx = newestIdx;
    for (size_t i = 0; i < count_; i++) {
        unsigned long ts = samples_[idx].timestampMs;
        unsigned long age = newestTs - ts;  // asumsi millis() tidak wrap dalam sesi trial

        if (age > WEIGHT_FILTER_FLOW_WINDOW_MS) {
            break;  // sample ini (dan yang lebih lama lagi) sudah di luar window
        }

        windowIdx[windowN++] = idx;
        oldestTsInWindow = ts;

        idx = (idx + BUFFER_SIZE - 1) % BUFFER_SIZE;
    }

    unsigned long windowSpanMs = newestTs - oldestTsInWindow;

    // Rentang waktu di dalam window terlalu pendek -- flow-rate TIDAK
    // dihitung. TIDAK berubah dari versi sebelumnya (lihat header file
    // untuk alasan lengkap: sengaja tidak fallback ke N-sample tetap).
    if (windowSpanMs < WEIGHT_FILTER_MIN_WINDOW_SPAN_MS || windowN < 2) {
        result.windowSpanMs = static_cast<float>(windowSpanMs);
        return result;
    }

    // Pass 2: OLS regression atas windowN titik (t, w) yang sudah
    // dikumpulkan. t dinormalisasi relatif ke newestTs (detik) supaya
    // angka yang di-kuadratkan tetap kecil (menghindari presisi float
    // jelek akibat timestamp absolut yang besar, mis. millis() setelah
    // uptime lama) -- ini murni teknik numerik, TIDAK mengubah hasil
    // slope secara matematis.
    //
    // slope (a) dari weight = a*t + b:
    //   a = (n*Sum(t*w) - Sum(t)*Sum(w)) / (n*Sum(t*t) - Sum(t)^2)
    double sumT = 0.0, sumW = 0.0, sumTT = 0.0, sumTW = 0.0;
    for (size_t i = 0; i < windowN; i++) {
        const WeightSample& s = samples_[windowIdx[i]];
        // KOREKSI (bug ditemukan lewat audit eksternal, sebelum pio run
        // v18 berikutnya): versi awal cast ke (long) sebelum kurangi --
        // (double)((long)s.timestampMs - (long)newestTs). Kalau salah
        // satu timestamp melebihi LONG_MAX (~24.85 hari uptime dalam
        // ms), cast ke long bisa overflow DULUAN sebelum pengurangan
        // sempat terjadi, menghasilkan angka absurd (bukan selisih kecil
        // yang sebenarnya). KOREKSI: pakai pengurangan unsigned relatif
        // -- POLA SAMA seperti "age = newestTs - ts" di window traversal
        // Pass 1 di atas, yang sudah benar & aman terhadap wrap selama
        // selisihnya kecil (dijamin oleh filter window 250ms). s.timestampMs
        // <= newestTs selalu (newestTs adalah sample TERBARU), jadi
        // (newestTs - s.timestampMs) aman dihitung unsigned lalu
        // dinegasikan ke double -- tidak pernah underflow.
        double t = -(double)(newestTs - s.timestampMs) / 1000.0;  // detik, <=0
        double w = (double)s.weight;
        sumT  += t;
        sumW  += w;
        sumTT += t * t;
        sumTW += t * w;
    }

    double n = (double)windowN;
    double denom = (n * sumTT) - (sumT * sumT);

    if (fabs(denom) < 1e-9) {
        // Semua titik punya timestamp identik (denom~0) -- regresi
        // tidak terdefinisi (divide-by-near-zero). Sengaja dianggap
        // TIDAK valid, sama seperti perilaku windowSpanMs terlalu
        // pendek di atas -- bukan kasus yang realistis kalau
        // windowSpanMs sudah lolos cek di atas, tapi dijaga untuk
        // keamanan numerik.
        result.windowSpanMs = static_cast<float>(windowSpanMs);
        return result;
    }

    double slope = ((n * sumTW) - (sumT * sumW)) / denom;

    result.valid = true;
    result.flowRateGps = (float)slope;
    result.windowSpanMs = static_cast<float>(windowSpanMs);
    return result;
}
