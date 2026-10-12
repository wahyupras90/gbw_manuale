#pragma once

#include <Arduino.h>

// ============================================================
// DiagTrace -- perekam kurva berat per sesi grind (DIAGNOSTIK).
// ============================================================
// TUJUAN: mengukur kurva settle (kapan berat benar-benar stabil
// setelah motor OFF), sumbangan purge/pulse, dll, untuk menentukan
// settle_ms yang optimal. HANYA instrumentasi: TIDAK dipakai oleh
// logika kontrol grind, TIDAK menyentuh motor/NVS/UI.
//
// Sifat:
//   - DEFAULT OFF setiap boot. Dinyalakan lewat GET /diag?on=1 dan
//     dimatikan lewat GET /diag?on=0 (diatur dari PC oleh gbw_logger).
//   - Buffer (2 x 3000 sampel x 12 B = ~72 KB) di-malloc saat ON dan
//     dilepas saat OFF. Menolak ON kalau heap bebas < 100 KB.
//   - Batas otomatis: berhenti merekam setelah 20 sesi; seluruh buffer
//     dilepas otomatis setelah 60 menit sejak ON.
//   - Perekaman hanya dari state STARTING s/d PULSE_CORRECTION.
//   - Hook per sampel hanya menyalin 3 nilai ke array (tanpa printf,
//     tanpa NVS, tanpa alokasi).
// ============================================================

#define DIAG_TRACE_MAX_SAMPLES    3000
#define DIAG_TRACE_MAX_SESSIONS   20
#define DIAG_TRACE_TIMEOUT_MS     (60UL * 60UL * 1000UL)
#define DIAG_TRACE_MIN_FREE_HEAP  100000UL

// Satu sampel = 12 B. meta: bit 0..23 = ms sejak sesi mulai,
// bit 24..27 = state grind, bit 28 = motor ON.
struct TraceSample {
    uint32_t meta;
    float rawG;
    float filtG;
};

inline uint32_t traceMetaTime(uint32_t m)  { return m & 0x00FFFFFFUL; }
inline uint8_t  traceMetaState(uint32_t m) { return (m >> 24) & 0x0F; }
inline uint8_t  traceMetaMotor(uint32_t m) { return (m >> 28) & 0x01; }

// ON/OFF. Return true kalau status akhir sesuai permintaan; msg diisi
// alasan singkat (selalu valid sampai pemanggilan berikutnya).
bool diag_trace_set(bool on, const char** msg);
bool diag_trace_enabled();

// Dipanggil tiap sampel valid dari loop(). state = int(GrindState).
void diag_trace_sample(unsigned long nowMs, float rawG, float filtG,
                       int grindState, bool motorOn);

// Dipanggil sekali saat sesi selesai (COMPLETE/ABORT). seq = nomor
// sesi di riwayat WebMonitor, supaya trace bisa dipasangkan dengan baris grind.
void diag_trace_finish(uint32_t seq);

// Dipanggil tiap loop(); melepas buffer setelah timeout.
void diag_trace_tick(unsigned long nowMs);

// Akses untuk /status dan /trace.csv.
uint32_t diag_trace_sessions();           // sesi selesai sejak ON
uint32_t diag_trace_last_seq();           // 0 kalau belum ada
uint16_t diag_trace_last_count();         // jumlah sampel sesi terakhir
bool diag_trace_last_overflow();          // sampel terbuang (buffer penuh)
const TraceSample* diag_trace_last();     // nullptr kalau belum ada
