#pragma once

#include <Arduino.h>

// ============================================================
// MotorController -- abstraksi kontrol motor grinder
// ============================================================
// Trial ini pakai Tasmota+HTTP (motong power steker utama grinder).
// Firmware final nanti pakai GPIO+relay langsung di motor microswitch
// (GPIO23, sudah settled di project brief). GrindController (STEP 4)
// hanya bicara ke interface ini -- tidak peduli implementasi di
// baliknya -- supaya migrasi trial->final tidak perlu ubah logic GBW.
//
// INTERFACE: BLOCKING secara sengaja.
//   - start()/stop() menunggu HTTP request selesai sebelum return.
//   - Alasan: STEP 3 ini tujuannya justru MENGUKUR latency presisi
//     (command -> response), bukan menghindarinya. Non-blocking
//     mengaburkan titik waktu yang justru ingin dikalibrasi.
//   - GrindController jalan sebagai state machine di loop(); blocking
//     call singkat (~puluhan-ratusan ms via WiFi lokal) tidak masalah
//     karena BLE notify tetap diproses di NimBLE task terpisah, tidak
//     ikut ke-block oleh loop().
//   - Saat migrasi ke GPIO relay final, start()/stop() jadi hampir
//     instan (digitalWrite) -- asumsi blocking tetap valid, interface
//     tidak perlu berubah.
//
// PENTING (diwariskan dari catatan tasmotaPower() asli): rtt/latency
// yang dikembalikan MotorResult DI SINI cuma round-trip HTTP command,
// BUKAN motor response latency yang sesungguhnya. Rantai lengkap
// sampai motor benar-benar berhenti jauh lebih panjang (relay
// switching, AC power, torque decay, burr benar-benar berhenti).
// Kalibrasi latency asli (STEP 3 lanjutan) harus ukur waktu dari
// command OFF sampai ALIRAN MATERIAL berhenti via flow rate dari
// timbangan (weight_filter), bukan cuma rtt HTTP ini.
//
// COEXISTENCE WIFI+BLE (temuan dari debugging Tasmota discovery,
// dikonfirmasi via program tes standalone terpisah): ESP32-C6 share
// radio/antena untuk WiFi dan BLE, jadi RTT HTTP command ke Tasmota
// bervariasi jauh lebih lebar saat BLE notify aktif mengalir (100ms-
// 2000ms, dibanding <100ms konsisten kalau BLE idle), dan sesekali
// (~5% dari 60 percobaan stress test) gagal total. SEMPAT dicoba
// unsubscribe BLE notify sesaat sebelum tiap command HTTP lalu
// subscribe lagi -- TERBUKTI LEBIH BURUK (rata-rata RTT 3x lebih
// lambat, gagal tetap ada) karena unsubscribe/subscribe sendiri
// operasi radio BLE yang menambah kontensi, bukan menguranginya.
// Solusi yang dipakai: JANGAN sentuh BLE sama sekali di sekitar
// command HTTP (biarkan notify tetap mengalir), cukup retry sekali
// dengan connect/read timeout eksplisit kalau request pertama gagal
// -- lihat sendPowerCommand() di motor_controller.cpp.
// ============================================================

struct MotorResult {
    bool success;
    unsigned long commandSentMs;   // timestamp millis() persis sebelum command dikirim
    unsigned long responseRecvMs;  // timestamp millis() persis setelah response diterima
    unsigned long rttMs;           // responseRecvMs - commandSentMs (HTTP RTT saja, BUKAN motor latency asli)
    int httpCode;                  // -1 kalau tidak sempat dapat kode (mis. WiFi belum connect)
};

class MotorController {
public:
    virtual ~MotorController() = default;

    // Nyalakan motor (blocking, tunggu command terkonfirmasi).
    virtual MotorResult start() = 0;

    // Matikan motor (blocking, tunggu command terkonfirmasi).
    virtual MotorResult stop() = 0;

    // Status motor yang KITA KIRIM terakhir kali (bukan status aktual
    // motor -- Tasmota trial tidak polling status balik tiap saat,
    // cuma asumsi command terakhir berhasil kalau MotorResult.success).
    virtual bool isOn() const = 0;
};

// ------------------------------------------------------------
// Implementasi trial: kontrol via Tasmota HTTP (Sonoff S26).
// ------------------------------------------------------------
class TasmotaMotorController : public MotorController {
public:
    TasmotaMotorController(const char* tasmotaIp, bool* wifiConnectedFlag);

    // Update IP Tasmota setelah construction -- dipakai setelah
    // TasmotaDiscovery menemukan IP sesungguhnya (lihat
    // tasmota_discovery.h). IP di constructor tetap dipakai sebagai
    // fallback awal/default sebelum discovery selesai.
    void setIp(const String& ip);
    String getIp() const;

    MotorResult start() override;
    MotorResult stop() override;
    bool isOn() const override;

private:
    String tasmotaIp_;
    bool* wifiConnectedFlag_;  // pointer ke flag global wifiConnected -- lihat main.cpp
    bool lastCommandedOn_;

    MotorResult sendPowerCommand(bool on);
};

// ------------------------------------------------------------
// Implementasi FINAL: kontrol via GPIO ke sisi kontrol relay module
// (yang dipasang PARALEL dengan microswitch fisik Eureka Mignon --
// BUKAN sinyal langsung ke PCB kontrol seperti referensi upstream
// Specialita. Lihat WIRING.md untuk diagram lengkap + peringatan
// keselamatan sebelum menyambung apa pun -- sisi kontak relay
// membawa AC MAINS, harus diverifikasi fisik dulu).
//
// BEDA PALING PENTING dari TasmotaMotorController: start()/stop() di
// sini HAMPIR INSTAN (digitalWrite(), microdetik) -- BUKAN lagi ratusan
// milidetik seperti HTTP. Ini berarti:
//   1. rttMs akan SELALU mendekati 0 -- MotorResult.success TIDAK LAGI
//      representasi "request berhasil dikirim & dikonfirmasi", karena
//      tidak ada request/response sama sekali. success SELALU true
//      (GPIO write tidak pernah gagal di level software).
//   2. SEMUA data kalibrasi lama (dari LatencyCalibrator/GrindController
//      yang dikumpulkan pakai TasmotaMotorController) TIDAK VALID lagi
//      untuk model REGRESI LAMA (sudah dihapus, lihat grind_controller.h
//      versi 2) -- stopOvershootWeightG akan jauh lebih kecil (murni
//      physical coast burr, HTTP latency component-nya hilang total).
//   3. Model predictive-stop SEKARANG (versi 2, real-time per sesi --
//      lihat grind_controller.h) TIDAK LAGI bergantung dataset
//      kalibrasi historis sama sekali, jadi poin 2 di atas relevan
//      untuk LatencyCalibrator (yang sekarang berperan validasi
//      GRIND_LATENCY_TO_COAST_RATIO), bukan untuk gate kesiapan
//      GrindController (gate itu sudah dihapus bersama regresi lama).
//
// PIN SINYAL MOTOR (ke sisi kontrol/IN relay module -- WAJIB dicek
// dulu topologi & isolasi fisiknya, lihat WIRING.md): HIGH = relay
// energized = motor menyala TERUS selama sinyal ditahan HIGH
// (momentary/held, BUKAN pulse toggle -- sudah dikonfirmasi langsung
// ke unit Eureka Mignon Manuale fisik). LOW = relay membuka, motor
// berhenti seketika. Ini cocok 1:1 dengan semantik start()/stop()
// yang sudah ada -- tidak perlu logic tambahan simulasi pulsa.
// ------------------------------------------------------------
class GpioMotorController : public MotorController {
public:
    // motorPin: GPIO yang tersambung ke sisi kontrol/IN relay module
    // (lihat WIRING.md). activeHigh: true kalau HIGH=relay energized/
    // motor menyala (kasus umum untuk modul relay non-inverting);
    // set false kalau pengujian fisik menunjukkan modul relay yang
    // dipakai active-LOW pada sisi IN (LOW=relay energized).
    //
    // PENTING (per review, KOREKSI arsitektur): constructor SEKARANG
    // HANYA menyimpan parameter (motorPin_/activeHigh_/isOn_), TIDAK
    // LAGI memanggil pinMode()/writePin() langsung. Alasan: instance
    // GpioMotorController ini dideklarasikan `static` GLOBAL di
    // main.cpp (mis. `static GpioMotorController motorController(...)`)
    // -- constructor objek static global dijalankan SEBELUM setup()
    // dipanggil, yaitu SEBELUM Serial.begin(). writePin() lama
    // memanggil Serial.printf() di dalamnya -- itu berarti versi
    // sebelumnya sempat mencoba menulis ke Serial SEBELUM Serial.begin()
    // dipanggil sama sekali, yang bergantung pada urutan static
    // initialization antar translation unit (undefined/implementation-
    // specific ordering di C++, walau pada Arduino/ESP32 core biasanya
    // "kebetulan aman" karena UART hardware sudah siap sejak boot ROM
    // -- tapi ini bukan jaminan portable, dan TIDAK LAYAK untuk
    // firmware hardware final yang mengontrol motor mekanis).
    //
    // SEKARANG: constructor murni menyimpan state (tidak ada akses
    // hardware sama sekali). Inisialisasi hardware SEBENARNYA
    // (pinMode() + pastikan motor OFF sejak awal) dipindah ke begin()
    // di bawah, yang HARUS dipanggil eksplisit dari setup() SETELAH
    // Serial.begin() (lihat main.cpp) -- supaya urutan inisialisasi
    // hardware jelas & deterministic, tidak bergantung urutan static
    // init yang implisit.
    explicit GpioMotorController(uint8_t motorPin, bool activeHigh = true);

    // Inisialisasi hardware SEBENARNYA -- pinMode(OUTPUT) + pastikan
    // motor OFF sejak awal (fail-safe boot, sama seperti yang dulu ada
    // di constructor). WAJIB dipanggil SEKALI dari setup(), SETELAH
    // Serial.begin() -- lihat main.cpp. Aman dipanggil lebih dari
    // sekali (idempotent, sama seperti panggilan stop() biasa), tapi
    // tidak perlu.
    void begin();

    MotorResult start() override;
    MotorResult stop() override;
    bool isOn() const override;

private:
    uint8_t motorPin_;
    bool activeHigh_;
    bool isOn_;

    MotorResult writePin(bool on);
};
