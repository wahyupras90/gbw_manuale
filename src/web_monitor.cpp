#include "web_monitor.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>
#include "version.h"

// Kapasitas riwayat. 100 x sizeof(GrindRecord) (~64 B) = ~6 KB RAM.
#define WEB_MONITOR_MAX_RECORDS   100
#define WEB_MONITOR_RETRY_MS      5000UL

static GrindRecord s_records[WEB_MONITOR_MAX_RECORDS];
static uint32_t    s_total = 0;       // total sesi tercatat sejak boot
static WebServer*  s_server = nullptr; // dibuat saat WiFi pertama connect
static unsigned long s_lastTryMs = 0;
static bool        s_started = false;

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
    char out[320];
    snprintf(out, sizeof(out),
        "{\"fw\":\"%s\",\"uptime_s\":%lu,\"weight_g\":%s,\"flow_gps\":%s,"
        "\"grind_state\":%d,\"rssi\":%d,\"heap_free\":%u,\"heap_min\":%u,"
        "\"records\":%u,\"total\":%lu}",
        FIRMWARE_VERSION, (unsigned long)(millis() / 1000UL), w, f,
        s_grindState, (int)WiFi.RSSI(),
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
        (unsigned)recordCount(), (unsigned long)s_total);
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
        char tol[16];
        fmtFloat(tol, sizeof(tol), r.toleranceG, 2, "null");
        char row[420];
        snprintf(row, sizeof(row),
            "%s{\"seq\":%lu,\"uptime_ms\":%lu,\"duration_ms\":%lu,\"result\":%u,\"abort\":%u,"
            "\"target_g\":%s,\"start_g\":%s,\"early_stop_g\":%s,\"stop_w_g\":%s,"
            "\"w100_g\":%s,\"w300_g\":%s,\"final_g\":%s,\"error_g\":%s,"
            "\"tol_g\":%s,\"settle_ms\":%u,\"pulses\":%u,\"max_pulses\":%u,"
            "\"purge_on\":%u,\"purge_n\":%u}",
            i == 0 ? "" : ",",
            (unsigned long)r.seq, (unsigned long)r.uptimeMs, (unsigned long)r.durationMs,
            r.result, r.abortReason,
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
            tol, r.settleMs, r.pulses, r.maxPulses, r.purgeOn, r.purgeCount);
        s_server->sendContent(row);
    }
    s_server->sendContent("]");
    s_server->sendContent("");  // akhiri chunked
}

static void handleGrindsCsv() {
    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200, "text/csv", "");
    s_server->sendContent(
        "seq,uptime_ms,duration_ms,result,abort,target_g,start_g,early_stop_g,"
        "stop_w_g,w100_g,w300_g,final_g,error_g,tol_g,settle_ms,pulses,max_pulses,"
        "purge_on,purge_n\n");
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
        char row[200];
        snprintf(row, sizeof(row),
            "%lu,%lu,%lu,%u,%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%u,%u,%u,%u\n",
            (unsigned long)r.seq, (unsigned long)r.uptimeMs, (unsigned long)r.durationMs,
            r.result, r.abortReason,
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8],
            r.settleMs, r.pulses, r.maxPulses, r.purgeOn, r.purgeCount);
        s_server->sendContent(row);
    }
    s_server->sendContent("");
}

static void handleNotFound() {
    s_server->send(404, "text/plain", "Endpoint: /status /grinds /grinds.csv");
}

// ------------------------------------------------------------
// Publik
// ------------------------------------------------------------
void web_monitor_begin() {
    s_total = 0;
    s_started = false;
    s_lastTryMs = 0;
}

void web_monitor_push(const GrindRecord& rec) {
    GrindRecord r = rec;
    r.seq = s_total + 1;
    s_records[s_total % WEB_MONITOR_MAX_RECORDS] = r;
    s_total++;
}

static void startServerOnce() {
    s_server = new WebServer(80);
    if (s_server == nullptr) return;
    s_server->on("/status", HTTP_GET, handleStatus);
    s_server->on("/grinds", HTTP_GET, handleGrindsJson);
    s_server->on("/grinds.csv", HTTP_GET, handleGrindsCsv);
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

    // Layani request HANYA saat tidak ada grind aktif.
    if (grindIdle && s_server != nullptr) {
        s_server->handleClient();
    }
}
