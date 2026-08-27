#pragma once

#include <Arduino.h>

// ============================================================
// GithubOtaManager -- update firmware via GitHub Release (HTTPS
// download), dipicu MANUAL lewat tombol di Settings.
// ============================================================
// KENAPA INI ADA: espota.py/ArduinoOTA (DIHAPUS TOTAL dari firmware
// ini, lihat catatan lengkap di ota_manager.h) mendorong firmware
// dalam chunk kecil (1024 byte) dengan acknowledgment round-trip tiap
// chunk -- pola ini membuatnya lambat di jaringan dengan latency
// tidak nol (mis. hotspot HP), walau link WiFi-nya sendiri sehat.
// GitHub Release OTA sebaliknya: device men-download file lewat HTTPS
// biasa (satu koneksi TCP mengalir penuh, TANPA round-trip kecil per
// chunk), jauh lebih cepat untuk kondisi jaringan yang sama. GitHub
// OTA sekarang SATU-SATUNYA mekanisme update firmware wireless di
// firmware ini.
//
// ISOLASI: SAMA SEPERTI OtaManager (lihat ota_manager.h) -- modul ini
// TIDAK punya dependency apa pun ke GrindController/MotorController/
// HX711Reader. Grind logic tidak tahu-menahu soal ini.
//
// TLS: SENGAJA pakai WiFiClientSecure::setInsecure() (skip verifikasi
// root CA GitHub) -- keputusan eksplisit karena ini alat rumahan
// (grind-by-weight kopi), bukan sistem yang menghadapi ancaman
// keamanan jaringan yang serius. JANGAN tambahkan pinning
// certificate/root CA kecuali ada keputusan eksplisit baru untuk itu.
//
// PEMICU: HANYA manual, lewat tombol "Check for Update" di
// screen_settings.cpp -- TIDAK ADA polling otomatis berkala (device
// tidak akan diam-diam mengecek GitHub sendiri di background).
// ============================================================

enum class GithubOtaStatus {
    IDLE,
    CHECKING,       // sedang query GitHub API utk cari asset URL
    DOWNLOADING,    // sedang download+flash firmware.bin
    SUCCESS,        // selesai, device akan reboot sendiri (tidak akan sempat lihat status ini lama)
    ERROR_WIFI,     // WiFi tidak connect
    ERROR_API,      // gagal query/parse GitHub API (rate limit, repo/release tidak ada, dst)
    ERROR_ASSET_NOT_FOUND,  // release ditemukan tapi tidak ada asset dengan nama GITHUB_OTA_ASSET_NAME
    ERROR_DOWNLOAD, // gagal download/flash (lihat Serial utk detail httpUpdate error)
};

class GithubOtaManager {
public:
    GithubOtaManager() : status_(GithubOtaStatus::IDLE), progressPct_(0), latestVersion_("") {}

    // BLOCKING -- dipanggil dari event handler tombol UI (LVGL sengaja
    // dibiarkan tidak responsif selama proses ini). Tidak dipanggil
    // dari loop() utama seperti OtaManager::update(), karena ini
    // one-shot yang dipicu manual, bukan polling berkelanjutan.
    //
    // Alur: (1) GET ke api.github.com/repos/OWNER/REPO/releases/latest,
    // (2) parse JSON cari asset bernama GITHUB_OTA_ASSET_NAME, ambil
    // browser_download_url, (3) httpUpdate.update() ke URL itu (stream
    // langsung ke partisi OTA lewat Update.h). Kalau sukses, device
    // REBOOT OTOMATIS ke firmware baru (fungsi ini tidak akan pernah
    // return dalam kasus itu).
    void checkAndUpdate();

    GithubOtaStatus status() const { return status_; }
    int progressPct() const { return progressPct_; }
    const char* statusText() const;

    // Versi (tag_name) dari release TERBARU di GitHub -- diisi
    // sesudah fetchLatestAssetUrl() berhasil (baik lewat
    // checkAndUpdate() penuh, atau kalau nanti dipanggil terpisah
    // sebagai cek versi saja tanpa download). Kosong ("") kalau belum
    // pernah berhasil query GitHub API sejak boot.
    const String& latestVersion() const { return latestVersion_; }

private:
    GithubOtaStatus status_;
    int progressPct_;
    String latestVersion_;

    // Query GitHub API, isi outDownloadUrl kalau ketemu asset yang
    // cocok. Return false kalau gagal di titik mana pun (lihat
    // status_ untuk detail kegagalan spesifik).
    bool fetchLatestAssetUrl(String& outDownloadUrl);
};

extern GithubOtaManager githubOta;
