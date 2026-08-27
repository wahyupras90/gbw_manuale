#include "motor_controller.h"
#include <HTTPClient.h>

// Retry & timeout -- berdasarkan hasil stress test 40x percobaan
// ON/OFF dengan BLE aktif (notify Myscale terus mengalir): waktu HTTP
// RTT bervariasi 100ms-2000ms (radio sharing WiFi+BLE ESP32-C6), dan
// sesekali (dari 60 percobaan) gagal total (connection refused/
// timeout). Mode RAW (tanpa pause BLE notify) TERBUKTI lebih reliable
// & lebih cepat daripada unsubscribe/subscribe di sekitar command --
// itu justru menambah overhead radio, bukan menguranginya. Jangan
// tambahkan lagi unsubscribe/subscribe BLE di sini.
static const unsigned long MOTOR_HTTP_CONNECT_TIMEOUT_MS = 2000;
static const unsigned long MOTOR_HTTP_READ_TIMEOUT_MS = 3000;
static const int MOTOR_HTTP_MAX_ATTEMPTS = 2;  // 1 percobaan awal + 1 retry

TasmotaMotorController::TasmotaMotorController(const char* tasmotaIp, bool* wifiConnectedFlag)
    : tasmotaIp_(tasmotaIp), wifiConnectedFlag_(wifiConnectedFlag), lastCommandedOn_(false) {}

void TasmotaMotorController::setIp(const String& ip) {
    tasmotaIp_ = ip;
    Serial.printf("[MOTOR] IP Tasmota diperbarui: %s\n", tasmotaIp_.c_str());
}

String TasmotaMotorController::getIp() const {
    return tasmotaIp_;
}

MotorResult TasmotaMotorController::sendPowerCommand(bool on) {
    MotorResult result{};
    result.success = false;
    result.httpCode = -1;

    if (wifiConnectedFlag_ == nullptr || !(*wifiConnectedFlag_)) {
        Serial.println("  [MOTOR] WiFi belum connect, skip.");
        result.commandSentMs = millis();
        result.responseRecvMs = result.commandSentMs;
        result.rttMs = 0;
        return result;
    }

    if (tasmotaIp_.length() == 0 || tasmotaIp_ == "0.0.0.0") {
        Serial.println("  [MOTOR] IP Tasmota belum diketahui (discovery belum jalan/gagal), skip.");
        result.commandSentMs = millis();
        result.responseRecvMs = result.commandSentMs;
        result.rttMs = 0;
        return result;
    }

    HTTPClient http;
    http.setConnectTimeout(MOTOR_HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(MOTOR_HTTP_READ_TIMEOUT_MS);
    String url = String("http://") + tasmotaIp_ + "/cm?cmnd=Power%20" + (on ? "On" : "Off");

    // PENTING: rttMs di sini cuma HTTP round-trip (ESP32 -> WiFi -> Tasmota
    // -> respons balik). BUKAN motor response latency yang sesungguhnya --
    // lihat catatan lengkap di motor_controller.h. Timestamp commandSentMs/
    // responseRecvMs disimpan presisi supaya caller (nanti latency_calibrator)
    // bisa cross-reference dengan timestamp sample berat dari weight_filter.
    //
    // RETRY: dari data stress test, kegagalan (httpCode negatif) terjadi
    // sesekali (~5% dari percobaan) di tengah radio WiFi+BLE yang sibuk --
    // bukan kegagalan permanen (IP salah/Sonoff mati), karena percobaan
    // berikutnya biasa langsung berhasil lagi. Retry sekali dengan jeda
    // kecil jauh lebih murah & reliable daripada mencoba "menghindari"
    // BLE (unsubscribe/subscribe -- TERBUKTI dari stress test malah
    // menambah overhead & tidak menghilangkan kegagalan).
    for (int attempt = 1; attempt <= MOTOR_HTTP_MAX_ATTEMPTS; attempt++) {
        result.commandSentMs = millis();
        http.begin(url);
        result.httpCode = http.GET();
        result.responseRecvMs = millis();
        result.rttMs = result.responseRecvMs - result.commandSentMs;

        if (result.httpCode == 200) {
            String payload = http.getString();
            http.end();
            if (payload.indexOf("\"POWER\"") >= 0) {
                result.success = true;
                lastCommandedOn_ = on;
                Serial.printf("  [MOTOR] %s (percobaan %d/%d, HTTP RTT %lums -- BUKAN motor latency asli, lihat motor_controller.h)\n",
                              on ? "ON" : "OFF", attempt, MOTOR_HTTP_MAX_ATTEMPTS, result.rttMs);
                return result;
            }
            Serial.printf("  [MOTOR] httpCode 200 tapi payload tidak match (percobaan %d/%d): %s\n",
                          attempt, MOTOR_HTTP_MAX_ATTEMPTS, payload.c_str());
        } else {
            http.end();
            Serial.printf("  [MOTOR] request gagal (percobaan %d/%d), httpCode=%d\n",
                          attempt, MOTOR_HTTP_MAX_ATTEMPTS, result.httpCode);
        }

        if (attempt < MOTOR_HTTP_MAX_ATTEMPTS) {
            delay(100);  // jeda kecil sebelum retry -- beri waktu radio "reda"
        }
    }

    return result;
}

MotorResult TasmotaMotorController::start() {
    return sendPowerCommand(true);
}

MotorResult TasmotaMotorController::stop() {
    return sendPowerCommand(false);
}

bool TasmotaMotorController::isOn() const {
    return lastCommandedOn_;
}

// ============================================================
// GpioMotorController -- implementasi FINAL, GPIO langsung
// ============================================================
GpioMotorController::GpioMotorController(uint8_t motorPin, bool activeHigh)
    : motorPin_(motorPin), activeHigh_(activeHigh), isOn_(false) {
    // SENGAJA KOSONG (selain menyimpan parameter) -- TIDAK ADA akses
    // hardware (pinMode()/digitalWrite()/Serial) di sini. Lihat
    // komentar lengkap di motor_controller.h soal kenapa: instance ini
    // dideklarasikan `static` global di main.cpp, constructor objek
    // static global berjalan SEBELUM setup()/Serial.begin() -- inisialisasi
    // hardware sebenarnya WAJIB lewat begin(), dipanggil eksplisit
    // dari setup() SETELAH Serial.begin().
}

void GpioMotorController::begin() {
    // KOREKSI (per audit eksternal, dikonfirmasi 2 reviewer independen):
    // urutan SEBELUMNYA cuma pinMode(OUTPUT) lalu writePin(false) --
    // window singkat ANTARA pin masih floating/input (sebelum begin()
    // dipanggil sama sekali, termasuk periode bootloader) TETAP ada
    // secara fisik, TIDAK bisa ditutup software manapun (baik urutan
    // lama maupun baru) -- itulah kenapa pull-down resistor fisik
    // 10k-ohm dari GPIO6 ke GND WAJIB dipasang di hardware (lihat
    // WIRING.md), sebagai lapis pertama yang sebenarnya menutup window
    // itu. Urutan software di bawah ini LAPIS KEDUA: begitu begin()
    // sempat jalan, digitalWrite(LOW) dipanggil SEBELUM pinMode(OUTPUT)
    // supaya level output sudah di-latch LOW terlebih dahulu (lewat
    // konfigurasi pull-down internal saat pin masih INPUT) -- begitu
    // pinMode(OUTPUT) dipanggil setelahnya, transisi ke output terjadi
    // sudah dalam keadaan LOW, bukan HIGH sesaat lalu LOW.
    digitalWrite(motorPin_, LOW);
    pinMode(motorPin_, OUTPUT);

    // Panggilan writePin(false) tetap dipertahankan SETELAH pinMode()
    // di atas -- BUKAN duplikasi sia-sia, ini yang mengisi isOn_ dan
    // MotorResult (rttMs, dll) dengan benar sesuai kontrak
    // MotorController, konsisten dengan seluruh pemanggilan writePin()
    // lain di class ini. digitalWrite() manual di atas HANYA untuk
    // boot-safety ordering, TIDAK mengisi state internal apa pun.
    writePin(false);
}

MotorResult GpioMotorController::writePin(bool on) {
    MotorResult result{};

    result.commandSentMs = millis();

    // activeHigh_ true: HIGH=motor menyala. activeHigh_ false (kalau
    // relay module yang dipakai active-LOW pada sisi IN): LOW=motor
    // menyala. Lihat WIRING.md untuk cara memastikan yang mana berlaku
    // di hardware kamu SEBELUM mengandalkan asumsi default true di sini.
    bool pinLevel = activeHigh_ ? on : !on;
    digitalWrite(motorPin_, pinLevel ? HIGH : LOW);

    result.responseRecvMs = millis();
    result.rttMs = result.responseRecvMs - result.commandSentMs;  // akan hampir selalu 0 -- lihat catatan di motor_controller.h

    // digitalWrite() tidak pernah gagal di level software (beda dari
    // HTTP yang bisa timeout/connection refused) -- success SELALU
    // true. httpCode tidak relevan di sini (bukan HTTP), diisi 0
    // sebagai penanda "bukan protokol HTTP", BUKAN kode error.
    result.success = true;
    result.httpCode = 0;

    isOn_ = on;
    Serial.printf("  [MOTOR-GPIO] %s (pin %d -> %s)\n",
                  on ? "ON" : "OFF", motorPin_, pinLevel ? "HIGH" : "LOW");

    return result;
}

MotorResult GpioMotorController::start() {
    return writePin(true);
}

MotorResult GpioMotorController::stop() {
    return writePin(false);
}

bool GpioMotorController::isOn() const {
    return isOn_;
}
