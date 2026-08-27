#pragma once

#include <Arduino.h>
#include <HX711.h>

// ============================================================
// HX711Reader -- baca load cell via HX711, konversi ke gram
// ============================================================
// Menggantikan BLE Myscale sebagai sumber data berat (lihat
// WIRING.md untuk diagram sambungan fisik load cell -> HX711 ->
// ESP32-C6). Beda mendasar dari BLE:
//   - BLE Myscale: PUSH -- notify masuk sendiri lewat NimBLE task
//     terpisah, RawSampleQueue dipakai untuk jembatani ke loop().
//   - HX711: POLL -- dibaca aktif dari loop() tiap kali data siap
//     (dicek pakai isReady()), TIDAK perlu queue/task terpisah karena
//     tidak ada callback asinkron yang perlu dijembatani. Timestamp
//     sample HANYA presisi sebatas kapan loop() sempat memanggil
//     read() -- ini lebih kasar dibanding timestamp BLE asli (yang
//     dicatat di titik notify diterima), tapi sesuai laju sample
//     HX711 default (~10-80 Hz tergantung mode) ini cukup untuk
//     kebutuhan flow-rate estimation yang sama seperti sebelumnya.
//
// KALIBRASI WAJIB per unit load cell -- HX711Reader butuh dua angka
// yang HARUS diisi dari kalibrasi fisik (lihat README bagian "Cara
// kalibrasi HX711"), BUKAN ditebak:
//   1. offset (tare): pembacaan mentah HX711 saat load cell KOSONG
//      (tidak ada beban sama sekali)
//   2. scale: (pembacaan mentah saat ADA beban dikenal) - offset,
//      dibagi berat beban dikenal dalam gram -- jadi units per gram
//
// Rumus konversi: gram = (rawReading - offset) / scale
// ============================================================

class HX711Reader {
public:
    // doutPin/sckPin: sesuai wiring HX711 -> ESP32 (lihat WIRING.md).
    // gain: 128 (channel A, default paling umum & presisi tinggi) atau
    // 64 (channel A gain rendah) atau 32 (channel B) -- lihat
    // datasheet HX711, default 128 kalau tidak yakin.
    HX711Reader(uint8_t doutPin, uint8_t sckPin, uint8_t gain = 128);

    // Panggil di setup() -- init komunikasi HX711. Return false kalau
    // HX711 tidak terdeteksi merespons (cek wiring SCK/DOUT/VCC/GND).
    bool begin();

    // Set offset & scale HASIL KALIBRASI FISIK (lihat README) --
    // WAJIB dipanggil dengan angka yang benar sebelum
    // readWeightGrams() dipakai untuk keputusan apa pun, kalau tidak
    // hasilnya cuma angka mentah tidak berarti (bukan gram
    // sesungguhnya).
    void setCalibration(long offset, float scaleUnitsPerGram);

    // Cek apakah data baru siap dibaca TANPA blocking (poll manual --
    // panggil ini di loop() sebelum readWeightGrams(), supaya loop()
    // tidak pernah nge-block menunggu HX711).
    bool isReady();

    // Baca satu sample, HANYA panggil kalau isReady() sudah true.
    // Return NAN kalau setCalibration() belum pernah dipanggil (scale
    // masih 0 -- mencegah divide-by-zero DAN mencegah pemakaian tanpa
    // kalibrasi yang diam-diam menghasilkan angka salah).
    float readWeightGrams();

    // Baca rata-rata N sample MENTAH (blocking, dipakai HANYA saat
    // proses kalibrasi manual di README -- BUKAN dipakai di jalur
    // normal operasi karena blocking).
    long readRawAverage(uint8_t times = 10);

private:
    HX711 hx711_;
    uint8_t doutPin_;
    uint8_t sckPin_;
    uint8_t gain_;
    long offset_;
    float scaleUnitsPerGram_;
    bool calibrationSet_;
};
