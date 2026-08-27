#include "ota_manager.h"
#include <WiFi.h>
#include "config.h"

void OtaManager::begin() {
    Serial.printf("[WIFI] Menghubungkan ke %s (untuk GitHub OTA saja, TIDAK dipakai kontrol grind)...\n", OTA_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // matikan WiFi power-save -- prioritaskan koneksi tetap stabil, konsumsi daya bukan concern utama alat ini
    WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
    wifiConnectAttempted_ = true;

    // TIDAK menunggu (blocking) sampai connect di sini -- sengaja,
    // supaya setup() tidak tertunda kalau WiFi lambat/gagal. Status
    // connect dipantau lewat isWifiConnected() kapan saja dibutuhkan
    // (status bar UI, GithubOtaManager sebelum mulai proses OTA).
}

void OtaManager::update(bool allowOtaHandling) {
    (void)allowOtaHandling;
    // NO-OP -- ArduinoOTA.handle() DIHAPUS (lihat catatan lengkap di
    // ota_manager.h kenapa ArduinoOTA dihapus total). WiFi STA mode
    // ESP32 auto-reconnect sendiri secara default kalau sempat putus,
    // jadi tidak perlu logic retry manual di sini. Fungsi ini
    // dipertahankan (bukan dihapus dari main.cpp) sebagai placeholder
    // untuk kemungkinan logic WiFi lain di masa depan.
}

bool OtaManager::isWifiConnected() const {
    return WiFi.status() == WL_CONNECTED;
}
