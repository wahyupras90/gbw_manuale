#pragma once

#include <Arduino.h>

// ============================================================
// OtaManager -- WiFi connection manager MURNI (ArduinoOTA DIHAPUS).
// ============================================================
// PERUBAHAN (keputusan eksplisit): SEBELUMNYA modul ini menjalankan
// ArduinoOTA (push firmware dari komputer lewat espota.py) DI ATAS
// koneksi WiFi ini. ArduinoOTA DIHAPUS TOTAL setelah diagnosa: pola
// protokolnya (chunk 1024 byte + acknowledgment round-trip tiap
// chunk) terbukti SANGAT lambat di jaringan hotspot HP (~2 detik per
// chunk), meski link WiFi-nya sendiri sehat (ping ke device normal).
// DIGANTIKAN oleh GithubOtaManager (lihat github_ota.h/.cpp) --
// device men-download firmware.bin dari GitHub Release lewat HTTPS,
// dipicu manual dari tombol Settings, jauh lebih cepat untuk kondisi
// jaringan yang sama.
//
// WiFi connection ITU SENDIRI TETAP DIPERTAHANKAN di modul ini --
// GithubOtaManager butuh WiFi+internet supaya bisa akses GitHub API/
// releases, jadi modul ini masih relevan sebagai "penyalur" koneksi
// WiFi, cuma tidak lagi menjalankan protokol ArduinoOTA di atasnya.
//
// ISOLASI TETAP SAMA (tidak berubah dari desain sebelumnya): modul
// ini SENGAJA TERISOLASI dari GrindController/MotorController/
// HX711Reader -- tidak ada dependency apa pun ke jalur kontrol
// grind, dan sebaliknya. WiFi HANYA untuk (sekarang) GitHub OTA,
// motor & timbangan TIDAK PERNAH bergantung status WiFi.
// ============================================================

class OtaManager {
public:
    OtaManager() : wifiConnectAttempted_(false) {}

    // Panggil sekali dari setup(). Mulai proses connect WiFi
    // (non-blocking -- WiFi.begin() lalu return segera, status
    // dicek via isWifiConnected() dari mana saja termasuk UI status
    // bar dan GithubOtaManager sebelum mulai proses OTA-nya).
    void begin();

    // Panggil tiap loop() utama -- SEKARANG cuma no-op murni (tidak
    // ada lagi ArduinoOTA.handle() untuk dipanggil). DIPERTAHANKAN
    // (bukan dihapus dari main.cpp) sebagai placeholder kalau nanti
    // perlu logic WiFi lain di loop() (mis. auto-reconnect custom),
    // supaya tidak perlu ubah signature call-site main.cpp lagi kalau
    // itu terjadi. allowOtaHandling TIDAK LAGI relevan secara
    // fungsional (tidak ada operasi yang di-skip), tapi parameter
    // dipertahankan untuk kompatibilitas call-site tanpa perlu ubah
    // main.cpp -- lihat catatan lengkap kalau ingin membersihkan ini
    // lebih lanjut di masa depan.
    void update(bool allowOtaHandling);

    bool isWifiConnected() const;

private:
    bool wifiConnectAttempted_;
};
