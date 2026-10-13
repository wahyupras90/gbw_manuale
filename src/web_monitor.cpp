#include "web_monitor.h"

#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>
#include "version.h"
#include "diag_trace.h"

// Kapasitas riwayat: 10 sesi terakhir. Persisten di NVS sebagai satu
// blob (~600 B), namespace sendiri supaya tidak menyentuh setting.
#define WEB_MONITOR_MAX_RECORDS   10
#define WEB_MONITOR_RETRY_MS      5000UL
#define WEB_MONITOR_NVS_NS        "gbwhist"
#define WEB_MONITOR_NVS_KEY       "h"
#define WEB_MONITOR_MAGIC         0x47425748UL   // "GBWH"
#define WEB_MONITOR_BLOB_VERSION  3   // v3: + tolUnderG (ukuran struct berubah -> blob lama diabaikan)

struct HistoryBlob {
    uint32_t magic;
    uint16_t version;
    uint16_t bootCount;      // boot terakhir yang menulis blob ini
    uint32_t total;          // total sesi tercatat (seq berlanjut lintas boot)
    GrindRecord recs[WEB_MONITOR_MAX_RECORDS];
};

static GrindRecord s_records[WEB_MONITOR_MAX_RECORDS];
static uint32_t    s_total = 0;       // total sesi tercatat (lintas boot)
static uint16_t    s_boot = 1;        // nomor boot sekarang
static bool        s_loadedFromNvs = false;
static WebServer*  s_server = nullptr; // dibuat saat WiFi pertama connect
static unsigned long s_lastTryMs = 0;
static bool        s_started = false;

static char   s_resetReason[32] = "";
static char   s_lastCp[32] = "";
static bool   s_wasIdle = false;
static unsigned long s_idleSinceMs = 0;
#define WEB_MONITOR_IDLE_GRACE_MS 1500UL   // beri UI waktu pindah layar setelah grind selesai

static float  s_weightG = NAN;
static float  s_flowGps = NAN;
static int    s_grindState = 0;

// ------------------------------------------------------------
// Format helper -- tulis angka ke buffer, NAN -> teks pengganti.
// ------------------------------------------------------------
static size_t fmtFloat(char* buf, size_t cap, float v, int dec, const char* nanText) {
    if (!isfinite(v)) return snprintf(buf, cap, "%s", nanText);
    return snprintf(buf, cap, "%.*f", dec, v);
}

// Iterasi riwayat dari terlama ke terbaru.
static uint32_t recordCount() {
    return s_total < WEB_MONITOR_MAX_RECORDS ? s_total : WEB_MONITOR_MAX_RECORDS;
}
static const GrindRecord& recordAt(uint32_t i) {  // i = 0 terlama
    uint32_t count = recordCount();
    uint32_t first = s_total - count;             // seq-1 dari entri terlama
    return s_records[(first + i) % WEB_MONITOR_MAX_RECORDS];
}

// ------------------------------------------------------------
// Handler
// ------------------------------------------------------------
static void handleStatus() {
    char w[16], f[16];
    fmtFloat(w, sizeof(w), s_weightG, 2, "null");
    fmtFloat(f, sizeof(f), s_flowGps, 2, "null");
    char out[640];
    snprintf(out, sizeof(out),
        "{\"fw\":\"%s\",\"uptime_s\":%lu,\"weight_g\":%s,\"flow_gps\":%s,"
        "\"grind_state\":%d,\"rssi\":%d,\"heap_free\":%u,\"heap_min\":%u,"
        "\"records\":%u,\"total\":%lu,\"boot\":%u,\"nvs_loaded\":%d,"
        "\"diag_on\":%d,\"diag_sessions\":%lu,\"diag_last_seq\":%lu,\"diag_last_n\":%u,\"diag_overflow\":%d,"
        "\"reset\":\"%s\",\"last_cp\":\"%s\"}",
        FIRMWARE_VERSION, (unsigned long)(millis() / 1000UL), w, f,
        s_grindState, (int)WiFi.RSSI(),
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
        (unsigned)recordCount(), (unsigned long)s_total,
        (unsigned)s_boot, s_loadedFromNvs ? 1 : 0,
        diag_trace_enabled() ? 1 : 0, (unsigned long)diag_trace_sessions(),
        (unsigned long)diag_trace_last_seq(), (unsigned)diag_trace_last_count(),
        diag_trace_last_overflow() ? 1 : 0,
        s_resetReason, s_lastCp);
    s_server->send(200, "application/json", out);
}

static void handleGrindsJson() {
    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200, "application/json", "");
    s_server->sendContent("[");
    uint32_t n = recordCount();
    for (uint32_t i = 0; i < n; i++) {
        const GrindRecord& r = recordAt(i);
        char b[8][16];
        fmtFloat(b[0], 16, r.targetDoseG,   2, "null");
        fmtFloat(b[1], 16, r.startWeightG,  2, "null");
        fmtFloat(b[2], 16, r.earlyStopG,    2, "null");
        fmtFloat(b[3], 16, r.weightAtStopG, 2, "null");
        fmtFloat(b[4], 16, r.weightAt100G,  2, "null");
        fmtFloat(b[5], 16, r.weightAt300G,  2, "null");
        fmtFloat(b[6], 16, r.finalWeightG,  2, "null");
        fmtFloat(b[7], 16, r.errorG,        2, "null");
        char tol[16], ap[16], tu[16];
        fmtFloat(tol, sizeof(tol), r.toleranceG, 2, "null");
        fmtFloat(ap, sizeof(ap), r.weightAfterPurgeG, 2, "null");
        fmtFloat(tu, sizeof(tu), r.tolUnderG, 2, "null");
        char row[520];
        snprintf(row, sizeof(row),
            "%s{\"seq\":%lu,\"boot\":%u,\"uptime_ms\":%lu,\"duration_ms\":%lu,\"result\":%u,\"abort\":%u,"
            "\"target_g\":%s,\"start_g\":%s,\"early_stop_g\":%s,\"stop_w_g\":%s,"
            "\"w100_g\":%s,\"w300_g\":%s,\"final_g\":%s,\"error_g\":%s,"
            "\"tol_g\":%s,\"settle_ms\":%u,\"pulses\":%u,\"max_pulses\":%u,"
            "\"purge_on\":%u,\"purge_n\":%u,\"after_purge_g\":%s,\"tol_under_g\":%s}",
            i == 0 ? "" : ",",
            (unsigned long)r.seq, (unsigned)r.boot, (unsigned long)r.uptimeMs, (unsigned long)r.durationMs,
            r.result, r.abortReason,
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
            tol, r.settleMs, r.pulses, r.maxPulses, r.purgeOn, r.purgeCount, ap, tu);
        s_server->sendContent(row);
    }
    s_server->sendContent("]");
    s_server->sendContent("");  // akhiri chunked
}

static void handleGrindsCsv() {
    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200, "text/csv", "");
    s_server->sendContent(
        "seq,boot,uptime_ms,duration_ms,result,abort,target_g,start_g,early_stop_g,"
        "stop_w_g,w100_g,w300_g,final_g,error_g,tol_g,settle_ms,pulses,max_pulses,"
        "purge_on,purge_n,after_purge_g,tol_under_g\n");
    uint32_t n = recordCount();
    for (uint32_t i = 0; i < n; i++) {
        const GrindRecord& r = recordAt(i);
        char b[9][16];
        fmtFloat(b[0], 16, r.targetDoseG,   2, "");
        fmtFloat(b[1], 16, r.startWeightG,  2, "");
        fmtFloat(b[2], 16, r.earlyStopG,    2, "");
        fmtFloat(b[3], 16, r.weightAtStopG, 2, "");
        fmtFloat(b[4], 16, r.weightAt100G,  2, "");
        fmtFloat(b[5], 16, r.weightAt300G,  2, "");
        fmtFloat(b[6], 16, r.finalWeightG,  2, "");
        fmtFloat(b[7], 16, r.errorG,        2, "");
        fmtFloat(b[8], 16, r.toleranceG,    2, "");
        char ap[16], tu[16];
        fmtFloat(ap, sizeof(ap), r.weightAfterPurgeG, 2, "");
        fmtFloat(tu, sizeof(tu), r.tolUnderG, 2, "");
        char row[260];
        snprintf(row, sizeof(row),
            "%lu,%u,%lu,%lu,%u,%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%u,%u,%u,%u,%s,%s\n",
            (unsigned long)r.seq, (unsigned)r.boot, (unsigned long)r.uptimeMs, (unsigned long)r.durationMs,
            r.result, r.abortReason,
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8],
            r.settleMs, r.pulses, r.maxPulses, r.purgeOn, r.purgeCount, ap, tu);
        s_server->sendContent(row);
    }
    s_server->sendContent("");
}

static void handleDiag() {
    // Tidak menyentuh kontrol grind: hanya menyalakan/mematikan perekaman trace.
    if (!s_server->hasArg("on")) {
        char o[96];
        snprintf(o, sizeof(o), "{\"diag_on\":%d,\"heap_free\":%u}",
                 diag_trace_enabled() ? 1 : 0, (unsigned)ESP.getFreeHeap());
        s_server->send(200, "application/json", o);
        return;
    }
    bool want = (s_server->arg("on") == "1");
    const char* msg = "";
    bool ok = diag_trace_set(want, &msg);
    char o[160];
    snprintf(o, sizeof(o), "{\"ok\":%d,\"diag_on\":%d,\"msg\":\"%s\",\"heap_free\":%u}",
             ok ? 1 : 0, diag_trace_enabled() ? 1 : 0, msg, (unsigned)ESP.getFreeHeap());
    s_server->send(ok ? 200 : 507, "application/json", o);
}

static void handleTraceCsv() {
    const TraceSample* t = diag_trace_last();
    uint16_t n = diag_trace_last_count();
    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200, "text/csv", "");
    s_server->sendContent("seq,t_ms,raw_g,filt_g,state,motor\n");
    if (t == nullptr || n == 0) {
        s_server->sendContent("");
        return;
    }
    uint32_t seq = diag_trace_last_seq();
    char chunk[1100];
    size_t used = 0;
    for (uint16_t i = 0; i < n; i++) {
        char row[64];
        int len = snprintf(row, sizeof(row), "%lu,%lu,%.2f,%.2f,%u,%u\n",
                           (unsigned long)seq, (unsigned long)traceMetaTime(t[i].meta),
                           t[i].rawG, t[i].filtG,
                           traceMetaState(t[i].meta), traceMetaMotor(t[i].meta));
        if (len < 0 || len >= (int)sizeof(row)) continue;
        if (used + (size_t)len >= sizeof(chunk)) {
            chunk[used] = 0;
            s_server->sendContent(chunk);
            used = 0;
        }
        memcpy(chunk + used, row, (size_t)len);
        used += (size_t)len;
    }
    if (used > 0) {
        chunk[used] = 0;
        s_server->sendContent(chunk);
    }
    s_server->sendContent("");
}

static void handleNotFound() {
    s_server->send(404, "text/plain", "Endpoint: /status /grinds /grinds.csv /diag /trace.csv");
}

// ------------------------------------------------------------
// Publik
// ------------------------------------------------------------
// Muat riwayat dari NVS. Blob tidak valid -> diabaikan (mulai kosong),
// TIDAK ada yang dihapus. Dipanggil sekali dari web_monitor_begin().
static void loadHistoryFromNvs() {
    s_total = 0;
    s_boot = 1;
    s_loadedFromNvs = false;

    Preferences p;
    if (!p.begin(WEB_MONITOR_NVS_NS, true)) return;  // read-only; namespace belum ada -> kosong
    size_t len = p.getBytesLength(WEB_MONITOR_NVS_KEY);
    if (len == sizeof(HistoryBlob)) {
        HistoryBlob* b = (HistoryBlob*)malloc(sizeof(HistoryBlob));
        if (b != nullptr) {
            size_t got = p.getBytes(WEB_MONITOR_NVS_KEY, b, sizeof(HistoryBlob));
            if (got == sizeof(HistoryBlob) &&
                b->magic == WEB_MONITOR_MAGIC &&
                b->version == WEB_MONITOR_BLOB_VERSION) {
                memcpy(s_records, b->recs, sizeof(s_records));
                s_total = b->total;
                s_boot = (uint16_t)(b->bootCount + 1);
                s_loadedFromNvs = true;
            }
            free(b);
        }
    }
    p.end();
}

static void saveHistoryToNvs() {
    HistoryBlob* b = (HistoryBlob*)malloc(sizeof(HistoryBlob));
    if (b == nullptr) return;
    b->magic = WEB_MONITOR_MAGIC;
    b->version = WEB_MONITOR_BLOB_VERSION;
    b->bootCount = s_boot;
    b->total = s_total;
    memcpy(b->recs, s_records, sizeof(s_records));

    Preferences p;
    if (p.begin(WEB_MONITOR_NVS_NS, false)) {
        // Gagal tulis (mis. NVS penuh) hanya dilaporkan; riwayat RAM tetap jalan.
        size_t w = p.putBytes(WEB_MONITOR_NVS_KEY, b, sizeof(HistoryBlob));
        if (w != sizeof(HistoryBlob)) {
            Serial.println("[WEB] Peringatan: gagal simpan riwayat ke NVS (RAM tetap jalan).");
        }
        p.end();
    }
    free(b);
}

void web_monitor_begin() {
    s_started = false;
    s_lastTryMs = 0;
    loadHistoryFromNvs();
    Serial.printf("[WEB] Riwayat dimuat dari NVS: %u sesi (total %lu, boot #%u)\n",
                  (unsigned)recordCount(), (unsigned long)s_total, (unsigned)s_boot);
}

uint32_t web_monitor_total() { return s_total; }

void web_monitor_set_boot_info(const char* resetReason, const char* lastCheckpoint) {
    snprintf(s_resetReason, sizeof(s_resetReason), "%s", resetReason ? resetReason : "");
    snprintf(s_lastCp, sizeof(s_lastCp), "%s", lastCheckpoint ? lastCheckpoint : "");
    // jaga JSON tetap valid: buang tanda kutip dan backslash
    for (char* p = s_resetReason; *p; ++p) if (*p == '"' || *p == '\\') *p = '_';
    for (char* p = s_lastCp; *p; ++p) if (*p == '"' || *p == '\\') *p = '_';
}

void web_monitor_push(const GrindRecord& rec) {
    GrindRecord r = rec;
    r.seq = s_total + 1;
    r.boot = s_boot;
    s_records[s_total % WEB_MONITOR_MAX_RECORDS] = r;
    s_total++;
    saveHistoryToNvs();
}

static void startServerOnce() {
    s_server = new WebServer(80);
    if (s_server == nullptr) return;
    s_server->on("/status", HTTP_GET, handleStatus);
    s_server->on("/grinds", HTTP_GET, handleGrindsJson);
    s_server->on("/grinds.csv", HTTP_GET, handleGrindsCsv);
    s_server->on("/diag", HTTP_GET, handleDiag);
    s_server->on("/trace.csv", HTTP_GET, handleTraceCsv);
    s_server->onNotFound(handleNotFound);
    s_server->begin();
    if (MDNS.begin("gbw")) {
        MDNS.addService("http", "tcp", 80);
    }
    s_started = true;
    Serial.printf("[WEB] Monitor siap: http://%s/  (juga http://gbw.local/)\n",
                  WiFi.localIP().toString().c_str());
}

void web_monitor_update(bool wifiConnected, bool grindIdle,
                        float weightG, float flowGps, int grindState) {
    s_weightG = weightG;
    s_flowGps = flowGps;
    s_grindState = grindState;

    if (!s_started) {
        // Nyalakan SEKALI setelah WiFi connect; coba lagi tiap 5 dtk
        // supaya tidak membebani loop() kalau WiFi lambat.
        if (!wifiConnected) return;
        unsigned long now = millis();
        if (s_lastTryMs != 0 && now - s_lastTryMs < WEB_MONITOR_RETRY_MS) return;
        s_lastTryMs = now ? now : 1;
        startServerOnce();
        return;
    }

    // Layani request HANYA saat tidak ada grind aktif, dan setelah masa tenang
    // singkat sejak grind selesai (supaya antrean koneksi tidak berebut waktu
    // dengan perpindahan layar/penulisan NVS di loop yang sama).
    if (grindIdle) {
        if (!s_wasIdle) {
            s_wasIdle = true;
            s_idleSinceMs = millis();
        }
    } else {
        s_wasIdle = false;
    }
    if (grindIdle && s_server != nullptr && (millis() - s_idleSinceMs) >= WEB_MONITOR_IDLE_GRACE_MS) {
        s_server->handleClient();
    }
}
