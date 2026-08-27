#include "github_ota.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "config.h"

GithubOtaManager githubOta;

const char* GithubOtaManager::statusText() const {
    switch (status_) {
        case GithubOtaStatus::IDLE:                  return "Idle";
        case GithubOtaStatus::CHECKING:               return "Checking GitHub...";
        case GithubOtaStatus::DOWNLOADING:            return "Downloading...";
        case GithubOtaStatus::SUCCESS:                return "Success, rebooting...";
        case GithubOtaStatus::ERROR_WIFI:             return "Error: WiFi not connected";
        case GithubOtaStatus::ERROR_API:              return "Error: GitHub API failed";
        case GithubOtaStatus::ERROR_ASSET_NOT_FOUND:  return "Error: asset not found";
        case GithubOtaStatus::ERROR_DOWNLOAD:         return "Error: download failed";
    }
    return "Unknown";
}

bool GithubOtaManager::fetchLatestAssetUrl(String& outDownloadUrl) {
    // GitHub API butuh HTTPS -- WiFiClientSecure::setInsecure() dipakai
    // SENGAJA (skip verifikasi root CA), lihat catatan keputusan
    // lengkap di github_ota.h. Ini alat rumahan (grind-by-weight
    // kopi), bukan sistem yang menghadapi ancaman keamanan jaringan
    // serius -- pinning certificate dianggap over-engineering untuk
    // kasus ini.
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = String("https://api.github.com/repos/") + GITHUB_OTA_OWNER +
                 "/" + GITHUB_OTA_REPO + "/releases/latest";

    Serial.printf("[GITHUB-OTA] Query: %s\n", url.c_str());

    if (!http.begin(client, url)) {
        Serial.println("[GITHUB-OTA] http.begin() gagal.");
        return false;
    }
    // GitHub API MEWAJIBKAN User-Agent -- request tanpa header ini
    // akan ditolak dengan 403, BUKAN bug di kode kita kalau lupa
    // pasang ini (ini persyaratan resmi GitHub REST API, terdokumentasi:
    // "You must supply a valid User-Agent header").
    http.addHeader("User-Agent", "GBW-Grinder-Firmware");
    http.addHeader("Accept", "application/vnd.github+json");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[GITHUB-OTA] GET gagal, HTTP code: %d\n", httpCode);
        http.end();
        return false;
    }

    // Response GitHub /releases/latest bisa lumayan besar (metadata
    // lengkap tiap asset) -- pakai filter ArduinoJson supaya cuma
    // field yang kita butuh yang di-deserialize (hemat RAM, ESP32-C6
    // tidak punya PSRAM di board ini). tag_name ditambahkan ke filter
    // untuk menampilkan versi terbaru di UI (lihat latestVersion()).
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[GITHUB-OTA] Parse JSON gagal: %s\n", err.c_str());
        return false;
    }

    // Diisi TERLEPAS dari asset ditemukan atau tidak -- operator tetap
    // ingin tahu versi terbaru yang tersedia di GitHub, meski asetnya
    // entah kenapa tidak ketemu (mis. release dibuat tanpa attach
    // firmware.bin).
    const char* tagName = doc["tag_name"];
    if (tagName != nullptr) {
        latestVersion_ = String(tagName);
    }

    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
        const char* name = asset["name"];
        if (name != nullptr && strcmp(name, GITHUB_OTA_ASSET_NAME) == 0) {
            outDownloadUrl = asset["browser_download_url"].as<String>();
            Serial.printf("[GITHUB-OTA] Asset ditemukan: %s (versi %s) -> %s\n", name, latestVersion_.c_str(), outDownloadUrl.c_str());
            return true;
        }
    }

    Serial.printf("[GITHUB-OTA] Tidak ada asset bernama '%s' di release terbaru.\n", GITHUB_OTA_ASSET_NAME);
    return false;
}

void GithubOtaManager::checkAndUpdate() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[GITHUB-OTA] TOLAK -- WiFi belum connect.");
        status_ = GithubOtaStatus::ERROR_WIFI;
        return;
    }

    status_ = GithubOtaStatus::CHECKING;
    progressPct_ = 0;

    String downloadUrl;
    if (!fetchLatestAssetUrl(downloadUrl)) {
        // fetchLatestAssetUrl() sendiri sudah print detail errornya ke
        // Serial (HTTP code / parse error / asset tidak ketemu) --
        // status_ di sini dibedakan cukup kasar (API vs asset-not-found)
        // supaya UI bisa kasih pesan yang sedikit lebih spesifik tanpa
        // perlu propagate detail error penuh ke lapisan UI.
        status_ = downloadUrl.length() == 0 && WiFi.status() == WL_CONNECTED
                      ? GithubOtaStatus::ERROR_ASSET_NOT_FOUND
                      : GithubOtaStatus::ERROR_API;
        return;
    }

    status_ = GithubOtaStatus::DOWNLOADING;

    WiFiClientSecure client;
    client.setInsecure();  // lihat catatan keputusan di fetchLatestAssetUrl()

    // WAJIB -- browser_download_url dari GitHub Release SELALU redirect
    // (github.com -> objects.githubusercontent.com, biasanya HTTP 302).
    // Tanpa ini, httpUpdate.update() gagal persis di titik redirect
    // tersebut (default HTTPClient TIDAK follow redirect otomatis).
    // DITEMUKAN & DITAMBAHKAN oleh Wahyu sendiri lewat testing aktual
    // di board -- dikonfirmasi PERSIS yang bikin GitHub OTA akhirnya
    // berhasil (compile aktual tidak menemukan bug ini karena ini bug
    // RUNTIME/network, bukan compile-time). HTTPC_FORCE_FOLLOW_REDIRECTS
    // dipilih (bukan HTTPC_STRICT_FOLLOW_REDIRECTS) supaya redirect
    // tetap diikuti walau ada deviasi kecil dari standar HTTP di sisi
    // server GitHub/CDN-nya.
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    // CATATAN: onProgress() callback SENGAJA TIDAK dipasang di sini --
    // signature pastinya (std::function vs raw function pointer) tidak
    // bisa diverifikasi dari sandbox development ini terhadap source
    // resmi HTTPUpdate library Arduino-ESP32 versi yang dipakai project
    // ini (platform pioarduino, rolling "stable" tag -- lihat brief
    // project). httpUpdate.update() SUDAH mencetak progress ke Serial
    // secara internal (perilaku default library), jadi operator tetap
    // bisa pantau progress lewat Serial Monitor tanpa fitur ini.
    // progressPct_ tetap ada di kelas untuk kemungkinan dipasang nanti
    // setelah signature dikonfirmasi lewat compile aktual di komputer
    // Wahyu -- JANGAN tebak signature-nya, verifikasi dulu.

    Serial.printf("[GITHUB-OTA] Mulai download+flash dari: %s\n", downloadUrl.c_str());
    t_httpUpdate_return result = httpUpdate.update(client, downloadUrl);

    switch (result) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[GITHUB-OTA] Gagal: (%d) %s\n",
                           httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            status_ = GithubOtaStatus::ERROR_DOWNLOAD;
            break;
        case HTTP_UPDATE_NO_UPDATES:
            // Praktis tidak akan terjadi di sini karena kita selalu
            // download URL asset yang eksplisit ditemukan di atas
            // (bukan pola check-version-header httpUpdate bawaan) --
            // tapi ditangani untuk kelengkapan enum switch.
            Serial.println("[GITHUB-OTA] Tidak ada update (tidak terduga di alur ini).");
            status_ = GithubOtaStatus::ERROR_DOWNLOAD;
            break;
        case HTTP_UPDATE_OK:
            Serial.println("\n[GITHUB-OTA] Sukses, reboot...");
            status_ = GithubOtaStatus::SUCCESS;
            // TIDAK PERLU ESP.restart() manual -- httpUpdate.update()
            // yang sukses SUDAH memanggil restart secara internal
            // (perilaku bawaan HTTPUpdate library), fungsi ini
            // praktis tidak akan pernah sampai baris setelah ini.
            break;
    }
}
