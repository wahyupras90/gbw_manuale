#pragma once

#include <Arduino.h>

// ============================================================
// WebMonitor -- server HTTP READ-ONLY untuk memantau data grind.
// ============================================================
// TUJUAN: mengeluarkan riwayat sesi grind (berat saat motor stop,
// +100/+300 ms, berat akhir, error, jumlah pulse, parameter yang
// dipakai) supaya bisa dianalisis di PC untuk mencari setting terbaik.
//
// BATASAN DESAIN (disengaja, sama semangat dengan ota_manager.h):
//   - READ-ONLY. Tidak ada endpoint yang menyalakan motor / mengubah
//     setting. Jalur kontrol grind tetap GPIO + HX711 murni.
//   - Tidak ada filesystem. Riwayat 10 sesi terakhir disimpan sebagai
//     SATU blob kecil (~600 B) di NVS namespace "gbwhist" -- terpisah
//     dari namespace setting, ditulis sekali tiap grind selesai, dan
//     dimuat ulang saat boot (tahan reboot & OTA). Blob yang tidak
//     valid (magic/versi/ukuran beda) diabaikan, TIDAK menghapus apa
//     pun di NVS lain.
//   - handleClient() HANYA dipanggil saat grind tidak aktif
//     (IDLE/COMPLETE/ABORT), sama pola dengan OtaManager.
//   - Server baru dinyalakan SETELAH WiFi connect; kalau WiFi tidak
//     pernah connect, modul ini tidak melakukan apa-apa.
//
// Endpoint:
//   GET /status      -> JSON: versi, berat, flow, state, RSSI, heap, jumlah sesi
//   GET /grinds      -> JSON array riwayat (terlama -> terbaru)
//   GET /grinds.csv  -> CSV riwayat, siap dibaca pandas/Excel
// mDNS: http://gbw.local/
// ============================================================

// Satu sesi grind yang sudah selesai (COMPLETE atau ABORT).
// Float yang tidak relevan diisi NAN (mis. berat stop pada ABORT).
struct GrindRecord {
    uint32_t seq;            // nomor urut sejak boot (mulai 1)
    uint32_t uptimeMs;       // millis() saat sesi selesai
    uint32_t durationMs;     // lama sesi sejak startGrind()
    float targetDoseG;       // dose tambahan yang diminta
    float startWeightG;      // berat tare saat mulai
    float earlyStopG;        // parameter Early Stop G yang dipakai
    float weightAtStopG;     // berat saat motor stop (NAN kalau ABORT)
    float weightAt100G;      // berat +100 ms setelah relay OFF
    float weightAt300G;      // berat +300 ms setelah relay OFF
    float finalWeightG;      // berat akhir
    float errorG;            // final - target absolut
    float toleranceG;        // toleransi yang dipakai
    uint16_t settleMs;       // settle time yang dipakai
    uint8_t pulses;          // pulse koreksi yang terjadi
    uint8_t maxPulses;       // batas pulse yang dipakai
    uint8_t purgeOn;         // post-purge aktif (0/1)
    uint8_t purgeCount;      // jumlah pulse purge
    uint8_t result;          // GrindResult (0 NONE,1 SUCCESS,2 INACCURATE,3 ABORTED)
    uint8_t abortReason;     // AbortReason (0 = tidak ada)
    uint16_t boot;           // nomor boot saat sesi dicatat (uptimeMs hanya bermakna per boot)
    float weightAfterPurgeG; // berat setelah purge, sebelum pulse pertama (NAN kalau ABORT)
};

// CATATAN penamaan kolom (jangan salah baca):
//   weightAtStopG = berat SETELAH SETTLE penuh (settle_ms setelah relay OFF),
//   BUKAN berat saat relay OFF -- lihat grind_controller.cpp (WAIT_SETTLE).
//   Urutan waktu: w100 -> w300 -> weightAtStopG (settled) -> after_purge -> final.

// Panggil sekali dari setup() (tidak menyalakan server, hanya reset state).
void web_monitor_begin();

// Dicatat main.cpp tiap sesi selesai. Aman dipanggil kapan saja.
void web_monitor_push(const GrindRecord& rec);

// Panggil tiap loop(). wifiConnected/grindIdle dari main.cpp.
// weightG/flowGps = nilai terkini, grindState = int(GrindState).
void web_monitor_update(bool wifiConnected, bool grindIdle,
                        float weightG, float flowGps, int grindState);
