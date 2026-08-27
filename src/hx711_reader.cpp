#include "hx711_reader.h"

HX711Reader::HX711Reader(uint8_t doutPin, uint8_t sckPin, uint8_t gain)
    : doutPin_(doutPin), sckPin_(sckPin), gain_(gain),
      offset_(0), scaleUnitsPerGram_(0.0f), calibrationSet_(false) {}

bool HX711Reader::begin() {
    hx711_.begin(doutPin_, sckPin_, gain_);

    // is_ready() HX711 butuh sedikit waktu setelah power-up sebelum
    // konversi pertama selesai (~sesuai clock internal chip, biasanya
    // <1 detik) -- tunggu sebentar dengan batas waktu supaya begin()
    // tidak hang selamanya kalau wiring salah/HX711 tidak terpasang.
    unsigned long start = millis();
    while (!hx711_.is_ready() && millis() - start < 2000) {
        delay(10);
    }

    if (!hx711_.is_ready()) {
        Serial.println("[HX711] TIDAK terdeteksi merespons -- cek wiring DOUT/SCK/VCC/GND.");
        return false;
    }

    Serial.println("[HX711] Terdeteksi & siap.");
    return true;
}

void HX711Reader::setCalibration(long offset, float scaleUnitsPerGram) {
    offset_ = offset;
    scaleUnitsPerGram_ = scaleUnitsPerGram;
    calibrationSet_ = (scaleUnitsPerGram_ != 0.0f);

    if (!calibrationSet_) {
        Serial.println("[HX711] PERINGATAN -- scale=0 diberikan, kalibrasi dianggap BELUM valid.");
    } else {
        Serial.printf("[HX711] Kalibrasi diset: offset=%ld scale=%.4f units/gram\n", offset_, scaleUnitsPerGram_);
    }
}

bool HX711Reader::isReady() {
    return hx711_.is_ready();
}

float HX711Reader::readWeightGrams() {
    if (!calibrationSet_) {
        // Tidak diam-diam mengembalikan angka mentah yang seolah-olah
        // gram -- itu bisa menyesatkan (kelihatan seperti data valid
        // padahal cuma raw ADC value). NAN memaksa caller sadar
        // kalibrasi belum diisi (sama filosofi dengan parseMyscaleWeight()
        // yang return NAN untuk data tidak lengkap).
        return NAN;
    }

    long raw = hx711_.read();
    return (raw - offset_) / scaleUnitsPerGram_;
}

long HX711Reader::readRawAverage(uint8_t times) {
    return hx711_.read_average(times);
}
