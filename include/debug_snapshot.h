#pragma once

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
};
