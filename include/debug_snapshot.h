#pragma once
#include <Arduino.h>  // String -- dibutuhkan field lastCheckpoint di bawah

// ============================================================
// DebugSnapshot -- struct kecil dipakai bersama main.cpp (produsen,
// lihat grind_get_debug_snapshot()) dan src/ui/screen_debug.cpp
// (konsumen, lihat ui_screen_debug_update()). Ditaruh di header
// terpisah (BUKAN di weight_filter.h/hx711_reader.h) supaya UI layer
// tidak perlu include HX711Reader/WeightFilter penuh cuma untuk satu
// struct data ini -- konsisten dengan pola project ini yang menjaga
// UI layer TIDAK menyentuh objek hardware langsung (selalu lewat
// fungsi pembungkus di main.cpp, lihat grind_start()/
// grind_last_abort_reason()).
// ============================================================
struct DebugSnapshot {
    long rawAdc;            // raw HX711 SAAT INI, -1 kalau HX711 belum isReady()
    long offsetActive;      // HX711_CALIBRATION_OFFSET aktif SAAT INI (runtime, bisa beda dari config.h kalau auto-tare sudah jalan)
    float scaleActive;      // HX711_CALIBRATION_SCALE aktif SAAT INI
    float weightGrams;      // hasil readWeightGrams() SAAT INI, NAN kalau kalibrasi belum diset
    bool hasSample;         // weightFilter.hasSample() -- syarat PERTAMA startGrind() (lihat grind_controller.cpp)
    bool flowValid;         // weightFilter.computeFlowRate().valid -- syarat KEDUA startGrind()
    float flowRateGps;      // NAN kalau !flowValid

    // --- DIAGNOSTIK SISTEM (BARU) -- ditambahkan untuk investigasi
    // laporan "layar tiba-tiba lompat ke Set Target SAAT GRINDING,
    // sesekali/random, kedipan lebih cepat dari reboot penuh". Karena
    // Serial tidak bisa diakses (case tertutup, power buck 5V), 3
    // field ini dibaca lewat Debug screen supaya kejadian berikutnya
    // bisa didiagnosis tanpa buka case:
    //   1. resetReasonStr -- kalau ternyata board REBOOT (brownout/
    //      watchdog/panic) walau kedipannya terasa cepat, ini akan
    //      tetap menunjukkan reason-nya (reset reason bertahan sampai
    //      reboot BERIKUTNYA, tidak hilang begitu boot selesai).
    //   2. homeGestureCount -- counter ui_go_home() (navigasi ke Set
    //      Target) terpanggil sejak boot. Kalau angka ini naik
    //      berbarengan dengan kejadian "lompat ke Set Target", itu
    //      mengarah ke phantom touch/gesture, BUKAN reboot.
    //   3. touchRecoveryCount -- counter touch_i2c_hard_recover()
    //      (mitigasi bug I2C dikenal, lihat lv_port.cpp) terpanggil
    //      sejak boot. Kalau naik bersamaan dengan gejala, mengarah ke
    //      noise I2C (kemungkinan dipicu motor menyala) sebagai akar
    //      masalah phantom touch.
    // KETIGANYA READ-ONLY, TIDAK mempengaruhi keputusan grind/UI apa
    // pun -- murni observability tambahan untuk sesi diagnosis
    // berikutnya.
    const char* resetReasonStr;
    unsigned long homeGestureCount;
    unsigned long touchRecoveryCount;

    // --- CHECKPOINT DIAGNOSTIK NVS (BARU) -- titik eksekusi terakhir
    // yang berhasil ditulis ke NVS oleh saveCheckpoint() (main.cpp)
    // sebelum reboot/freeze terjadi. Berbeda dari 3 field di atas
    // (RAM-only, ter-reset tiap boot), checkpoint ini BERTAHAN LINTAS
    // REBOOT karena disimpan ke flash NVS namespace "gbwdiag". Berguna
    // untuk mengetahui DI MANA PERSIS firmware berhenti saat terjadi
    // reboot POWERON di tengah sesi grind (Reset Reason=POWERON tapi
    // counter RAM sudah ter-reset, jadi tidak bisa tahu dari counter).
    // Dibaca lewat Debug screen -> field "Last checkpoint".
    String lastCheckpoint;       // label terakhir, "(belum ada)" kalau NVS kosong
    unsigned long lastCheckpointMs;  // millis() saat checkpoint itu ditulis (dari sesi SEBELUMNYA)
};
