#include "diag_trace.h"

#include <stdlib.h>
#include <math.h>

static TraceSample* s_bufA = nullptr;
static TraceSample* s_bufB = nullptr;
static TraceSample* s_cur  = nullptr;   // buffer yang sedang ditulis
static TraceSample* s_last = nullptr;   // sesi terakhir yang selesai

static bool     s_enabled = false;      // buffer teralokasi (mode diagnostik hidup)
static bool     s_recording = false;    // masih boleh membuka sesi baru
static bool     s_sessionActive = false;
static unsigned long s_t0 = 0;          // millis() awal sesi
static unsigned long s_enabledAtMs = 0;
static uint32_t s_curCount = 0;
static bool     s_curOverflow = false;
static uint32_t s_sessions = 0;
static uint32_t s_lastSeq = 0;
static uint16_t s_lastCount = 0;
static bool     s_lastOverflow = false;

static void freeBuffers() {
    free(s_bufA);
    free(s_bufB);
    s_bufA = s_bufB = s_cur = s_last = nullptr;
    s_enabled = false;
    s_recording = false;
    s_sessionActive = false;
    s_curCount = 0;
    s_lastCount = 0;
    s_lastSeq = 0;
    s_lastOverflow = false;
    s_curOverflow = false;
}

bool diag_trace_set(bool on, const char** msg) {
    static const char* okOn   = "on";
    static const char* okOff  = "off";
    static const char* already = "sudah on";
    static const char* noHeap = "heap tidak cukup";
    static const char* noMem  = "alokasi buffer gagal";

    if (!on) {
        freeBuffers();
        s_sessions = 0;
        if (msg) *msg = okOff;
        return true;
    }
    if (s_enabled) {
        if (msg) *msg = already;
        return true;
    }
    if (ESP.getFreeHeap() < DIAG_TRACE_MIN_FREE_HEAP + 2UL * DIAG_TRACE_MAX_SAMPLES * sizeof(TraceSample)) {
        if (msg) *msg = noHeap;
        return false;
    }
    s_bufA = (TraceSample*)malloc(DIAG_TRACE_MAX_SAMPLES * sizeof(TraceSample));
    s_bufB = (TraceSample*)malloc(DIAG_TRACE_MAX_SAMPLES * sizeof(TraceSample));
    if (s_bufA == nullptr || s_bufB == nullptr) {
        freeBuffers();
        if (msg) *msg = noMem;
        return false;
    }
    s_cur = s_bufA;
    s_last = nullptr;
    s_enabled = true;
    s_recording = true;
    s_sessionActive = false;
    s_sessions = 0;
    s_enabledAtMs = millis();
    if (msg) *msg = okOn;
    return true;
}

bool diag_trace_enabled() { return s_enabled; }

void diag_trace_sample(unsigned long nowMs, float rawG, float filtG,
                       int grindState, bool motorOn) {
    if (!s_enabled || !s_recording) return;

    // Rekam hanya saat sesi aktif: STARTING(3) .. PULSE_CORRECTION(7).
    bool active = (grindState >= 3 && grindState <= 7);
    if (!active) return;

    if (!s_sessionActive) {
        s_sessionActive = true;
        s_t0 = nowMs;
        s_curCount = 0;
        s_curOverflow = false;
    }
    if (s_curCount >= DIAG_TRACE_MAX_SAMPLES) {
        s_curOverflow = true;  // buffer penuh: buang sampel, jangan menimpa
        return;
    }
    uint32_t rel = (uint32_t)(nowMs - s_t0) & 0x00FFFFFFUL;
    TraceSample& s = s_cur[s_curCount++];
    s.meta = rel | ((uint32_t)(grindState & 0x0F) << 24) | ((motorOn ? 1UL : 0UL) << 28);
    s.rawG = rawG;
    s.filtG = filtG;
}

void diag_trace_finish(uint32_t seq) {
    if (!s_enabled || !s_sessionActive) return;
    s_sessionActive = false;
    if (s_curCount == 0) return;

    // Sesi selesai: jadikan "last", lalu pakai buffer satunya untuk sesi berikutnya.
    s_last = s_cur;
    s_lastCount = (uint16_t)s_curCount;
    s_lastOverflow = s_curOverflow;
    s_lastSeq = seq;
    s_cur = (s_cur == s_bufA) ? s_bufB : s_bufA;
    s_curCount = 0;
    s_curOverflow = false;
    s_sessions++;

    if (s_sessions >= DIAG_TRACE_MAX_SESSIONS) {
        s_recording = false;  // berhenti merekam; buffer terakhir tetap bisa ditarik
        Serial.println("[DIAG] Batas 20 sesi tercapai -- perekaman berhenti.");
    }
}

void diag_trace_tick(unsigned long nowMs) {
    if (s_enabled && (nowMs - s_enabledAtMs) >= DIAG_TRACE_TIMEOUT_MS) {
        Serial.println("[DIAG] Timeout 60 menit -- buffer dilepas, mode diagnostik OFF.");
        freeBuffers();
    }
}

uint32_t diag_trace_sessions()   { return s_sessions; }
uint32_t diag_trace_last_seq()   { return s_lastSeq; }
uint16_t diag_trace_last_count() { return s_lastCount; }
bool diag_trace_last_overflow()  { return s_lastOverflow; }
const TraceSample* diag_trace_last() { return s_last; }
