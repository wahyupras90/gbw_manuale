#pragma once

// ============================================================
// KONFIGURASI HARDWARE FINAL -- GPIO motor + HX711 load cell + WiFi
// (OTA saja)
// ============================================================
// PENTING soal WiFi di firmware ini: WiFi HANYA dipakai untuk OTA
// update firmware, SAMA SEKALI TIDAK dipakai untuk jalur kontrol
// grind (motor tetap GPIO murni, timbangan tetap HX711 murni,
// keduanya TIDAK bergantung WiFi sama sekali -- kalau WiFi gagal
// connect, grinding tetap berfungsi normal). Ini beda dari firmware
// Tasmota+BLE lama yang WiFi-nya kritis untuk kontrol motor.
//
// Status bar UI (lihat src/ui/ui_common.h) menampilkan indikator
// "SCALE" (HX711 siap) dan "PLUG" (motor/relay siap) -- BUKAN
// indikator WiFi/BLE, karena WiFi/BLE bukan bagian jalur kontrol
// grind yang perlu dipantau operator saat proses grinding.
//
// Lihat WIRING.md untuk diagram lengkap + peringatan keselamatan
// SEBELUM menyambung apa pun secara fisik. Nilai pin di bawah cocok
// untuk ESP32-C6 DevKit -- SESUAIKAN kalau board/breakout kamu beda.
//
// PIN MOTOR & HX711 -- DIPINDAH dari GPIO4/5/6 (default lama) karena
// BENTROK TERKONFIRMASI dengan board display ESP32-C6-Touch-AMOLED-1.64
// (lihat src/ui/lv_port.h & vendor-reference/ untuk detail lengkap):
// GPIO4/5 dipakai QSPI ke panel AMOLED (LCD_DATA0/DATA1, berbagi bus
// SPI2 fisik dengan SD card juga) -- terverifikasi langsung dari
// source resmi vendor, BUKAN dugaan. GPIO4/5 SELALU aktif dipakai
// display begitu lv_port_init() dipanggil, jadi motor/HX711 TIDAK
// BOLEH pakai pin itu lagi begitu UI LVGL disambungkan (lihat
// main.cpp/lv_port.cpp).
//
// Pin baru dipilih dari daftar GPIO yang bebas dari display/touch/
// IMU/SD card/BOOT button DAN dari fungsi hardware khusus ESP32-C6
// lain (strapping pin, USB-JTAG -- lihat catatan penting di
// HX711_SCK_PIN di bawah, ini koreksi dari draft sebelumnya yang
// sempat salah pakai GPIO12/USB-JTAG). GPIO yang benar-benar aman
// dipakai bebas: GPIO2, GPIO3, GPIO6, GPIO16, GPIO17, GPIO21, GPIO22,
// GPIO23 (GPIO9 dihindari karena BOOT button/strapping, GPIO12/13
// dihindari karena USB-JTAG -- lihat detail di bawah). Prioritas
// pemilihan: KEBENARAN FUNGSI (tidak bentrok strapping/USB-JTAG/
// display) di atas simetri kabel kiri-kanan -- simetri hanya dipakai
// sebagai tie-breaker kalau beberapa kandidat pin sama-sama aman.
//
// PIN MOTOR: ke sisi kontrol/IN relay module yang dipasang PARALEL
// dengan microswitch fisik Eureka (WAJIB, jangan sambung langsung ke
// microswitch/motor tanpa relay module rated AC mains -- lihat
// WIRING.md). GPIO6 dipilih (kolom kiri, dekat GND) -- aman dipakai
// sebagai output digital biasa di ESP32-C6 (bukan strapping pin,
// bukan USB-JTAG, tidak bentrok dengan flash SPI internal --
// terverifikasi dari datasheet resmi Espressif ESP32-C6 v1.5).
#define MOTOR_GPIO_PIN          6
#define MOTOR_GPIO_ACTIVE_HIGH  true   // ganti ke false kalau modul relay yang dipakai active-LOW pada sisi IN (verifikasi fisik dulu, lihat WIRING.md)

// PIN HX711: bit-bang GPIO biasa (bukan pin fungsi khusus), jadi
// portable ke pin manapun -- yang penting DOUT bisa dibaca input,
// SCK bisa jadi output. DOUT di kolom kiri (GPIO2, menyeimbangkan
// dengan MOTOR_GPIO_PIN di atas -- total 2 kabel kiri), SCK di kolom
// kiri juga (GPIO3) -- lihat catatan penting di bawah soal kenapa
// BUKAN GPIO12.
//
// PENTING -- GPIO12 DIHINDARI (bukan lagi dipakai, versi sebelumnya
// salah): GPIO12 dan GPIO13 adalah pin default USB Serial/JTAG
// Controller di ESP32-C6 (terverifikasi dari ESP-IDF Programming
// Guide & datasheet resmi Espressif: "GPIO12 and GPIO13 are used by
// USB-JTAG by default. If they are reconfigured to operate as normal
// GPIOs, USB-JTAG functionality will be disabled."). Firmware ini
// memakai `-DARDUINO_USB_MODE=1` dan `-DARDUINO_USB_CDC_ON_BOOT=1`
// (lihat platformio.ini) -- artinya Serial monitor project ini
// BERGANTUNG pada USB Serial/JTAG Controller, yang justru memakai
// GPIO12/13. Memakai GPIO12 untuk HX711_SCK akan bentrok LANGSUNG
// dengan fungsi Serial/upload/debug yang krusial untuk development
// project ini -- SEKARANG DIPINDAH ke GPIO3.
//
// GPIO3 dipilih sebagai gantinya karena: (1) BUKAN strapping pin --
// strapping pin resmi ESP32-C6 hanya GPIO4, GPIO5, GPIO8, GPIO9,
// GPIO15 (datasheet Espressif ESP32-C6 v1.5, Section 2.3.4/2.3.5);
// (2) BUKAN dipakai USB-JTAG (GPIO12/13); (3) TIDAK bentrok dengan
// display/touch/IMU/SD card board ESP32-C6-Touch-AMOLED-1.64 (lihat
// src/ui/lv_port.h). GPIO3 di kolom kiri header board yang sama
// dengan GPIO2/GPIO6 -- total 3 kabel kiri untuk motor+HX711, TIDAK
// lagi seimbang kiri/kanan seperti versi sebelumnya (yang salah
// karena GPIO12 tidak aman dipakai), tapi ini prioritas yang benar:
// keamanan/kebenaran fungsi USB Serial/JTAG lebih penting daripada
// simetri kabel murni.
#define HX711_DOUT_PIN          2
#define HX711_SCK_PIN           3
#define HX711_GAIN               128   // channel A, gain 128 -- default paling umum, lihat datasheet HX711 kalau load cell butuh gain lain

// ============================================================
// KONFIGURASI WIFI -- SESUAIKAN DENGAN SETUP KAMU
// ============================================================
// SSID/password di-hardcode (sama pola dengan firmware Tasmota+BLE
// lama), BUKAN captive portal -- keputusan eksplisit, lihat diskusi.
// WiFi selalu aktif & connect terus sejak boot (bukan on-demand),
// supaya GitHub OTA bisa dipakai kapan saja tanpa perlu ke lokasi
// fisik alat (asal hotspot yang dipakai punya akses internet).
#define OTA_WIFI_SSID       "Testes"
#define OTA_WIFI_PASSWORD   "qwer1234"

// OTA_HOSTNAME/OTA_PASSWORD (dipakai ArduinoOTA/espota.py) SUDAH
// DIHAPUS -- ArduinoOTA dihapus total, lihat catatan lengkap di
// ota_manager.h. GitHub OTA (di bawah) tidak butuh hostname/password
// lokal seperti ini -- otentikasinya cukup lewat sifat repo publik
// GitHub itu sendiri (siapa saja bisa baca release publik).

// ============================================================
// GITHUB OTA -- satu-satunya mekanisme update firmware wireless di
// firmware ini (menggantikan ArduinoOTA yang dihapus total), dipicu
// manual lewat tombol di Settings. Device men-download firmware.bin
// dari GitHub Release lewat HTTPS (device yang menarik data, koneksi
// TCP tunggal mengalir penuh) -- jauh lebih cepat dibanding pola lama
// espota.py yang mendorong data chunk-per-chunk (1024 byte) dengan
// acknowledgment round-trip tiap chunk, yang terbukti SANGAT lambat
// di jaringan hotspot HP (~2 detik per chunk), walau link WiFi-nya
// sendiri sehat (dikonfirmasi lewat ping normal ~20-100ms sebelum
// keputusan pindah ke skema ini diambil).
//
// Repo GitHub publik project ini -- https://github.com/wahyupras90/gbw_manuale
// Release TERBARU di repo ini WAJIB punya asset bernama persis
// GITHUB_OTA_ASSET_NAME (lihat di bawah) supaya tombol Check for
// Update di Settings bisa menemukannya.
#define GITHUB_OTA_OWNER    "wahyupras90"
#define GITHUB_OTA_REPO     "gbw_manuale"

// Nama file asset .bin yang dicari di GitHub Release TERBARU (release
// workflow kamu harus attach file dengan nama PERSIS ini). Kalau nama
// asset di release tidak cocok persis, GithubOtaManager akan gagal
// menemukan URL download dan melaporkan error (lihat github_ota.cpp).
#define GITHUB_OTA_ASSET_NAME  "firmware.bin"

// KALIBRASI HX711 -- SCALE hasil kalibrasi fisik (load cell terpasang
// di case final), diverifikasi linear di 4 titik (0g, 122.1g, 232.6g,
// 499.4g), error <0.1% pada titik cross-check independen. Diyakini
// stabil jangka panjang (TIDAK seperti offset di bawah).
//
// OFFSET SENGAJA tetap 0L secara permanen -- JANGAN diisi angka tetap
// dari kalibrasi manapun. Raw baseline (nol) terbukti drift signifikan
// (>900 unit antar sesi, ~0.45g setara pada scale di atas -- 4.5x
// tolerance minimum 0.1g) bahkan setelah 1 jam menyala tanpa load cell
// disentuh. Sebagai gantinya, firmware melakukan AUTO-TARE tiap kali
// grind_start() dipanggil (lihat main.cpp) -- offset RUNTIME diambil
// segar dari readRawAverage() setiap sesi lewat hx711.setCalibration(),
// TIDAK PERNAH dari nilai tetap di sini. HX711_CALIBRATION_OFFSET di
// bawah ini HANYA dipakai sebagai nilai awal sebelum grind pertama kali
// dipanggil (mis. indikator UI Idle sebelum ada sesi grind sama sekali)
// -- boleh tetap 0L, TIDAK memengaruhi akurasi grind karena selalu
// ditimpa auto-tare sebelum startGrind() membaca berat apa pun.
#define HX711_CALIBRATION_OFFSET       0L
#define HX711_CALIBRATION_SCALE        2022.88f   // units per gram -- hasil kalibrasi fisik, lihat catatan di atas

// Ambang batas berat ABSOLUT (bukan dose) untuk anggap portafilter/
// wadah "terpasang" di layar Idle -- MURNI indikator visual (warna
// label berat berubah abu-abu <-> putih), TIDAK memengaruhi validasi
// startGrind() sama sekali (itu tetap pakai cek flow-rate stabil di
// GrindController::startGrind(), independen dari konstanta ini).
// Nilai default 50g dipilih supaya noise timbangan kosong (biasanya
// +-1-2g) tidak memicu status "terpasang" palsu, sementara cukup
// rendah untuk portafilter kosong paling ringan sekalipun (umumnya
// >150g). Sesuaikan kalau perlu.
#define PORTAFILTER_DETECT_THRESHOLD_G   50.0f

// ============================================================
// TARGET GRIND
// ============================================================
#define TARGET_WEIGHT_G   18.0f

// ============================================================
// KONSTANTA ALGORITMA -- disalin persis dari grind_control.h
// upstream (jaapp/smart-grind-by-weight), sama seperti gbw_sim.py
// ============================================================
#define GRIND_ACCURACY_TOLERANCE_G           0.03f
#define GRIND_MAX_PULSE_ATTEMPTS             10
#define GRIND_FLOW_DETECTION_THRESHOLD_GPS   0.5f
#define GRIND_FLOW_RATE_MIN_SANE_GPS         1.0f
#define GRIND_FLOW_RATE_MAX_SANE_GPS         3.0f
#define GRIND_PULSE_FLOW_RATE_FALLBACK_GPS   1.5f
#define GRIND_SCALE_PRECISION_SETTLING_TIME_MS  500
// (BUG DITEMUKAN & DIHAPUS lewat audit config: "GRIND_MOTOR_SETTLING_TIME_MS
// = 200" sempat ada di sini, TIDAK PERNAH dipakai di kode manapun --
// bahkan tidak disebut di komentar. Konstanta mati, dihapus.)

// ============================================================
// GRIND_CONTROLLER -- konstanta predictive-stop & safety
// (spesifikasi dikunci setelah diskusi & verifikasi source code
// upstream jaapp/smart-grind-by-weight -- lihat grind_controller.h
// untuk penjelasan lengkap)
//
// MODEL PREDICTIVE STOP: diganti dari regresi statistik
// (coastWeightG = a*flowAtStop + b, dari dataset kalibrasi terpisah)
// menjadi model REAL-TIME per-sesi, mengikuti pendekatan upstream:
//   grind_latency_ms = waktu motor ON -> flow pertama terdeteksi
//                       (>= GRIND_FLOW_DETECTION_THRESHOLD_GPS,
//                       window konfirmasi GRIND_LATENCY_CONFIRMATION_MS)
//   coast_time_ms     = grind_latency_ms * GRIND_LATENCY_TO_COAST_RATIO
//   motor_stop_target_weight_g = flow_now_gps * coast_time_ms / 1000
//   STOP saat currentWeight >= targetWeight - motor_stop_target_weight_g
//
// Alasan: perubahan grind size/kopi otomatis tercermin di flow_now
// dan grind_latency_ms SESI ITU SENDIRI -- tidak perlu tahu grind
// size, tidak perlu dataset kalibrasi dari kondisi sebelumnya, tidak
// ada risiko regresi basi saat kondisi berubah.
//
// GRIND_LATENCY_TO_COAST_RATIO mengonversi T_onset (mudah diukur
// real-time) ke estimasi coast/T_stop (yang secara fisik BERBEDA --
// waktu motor ON sampai flow pertama BUKAN sama dengan waktu OFF
// sampai flow berhenti). Rasio ini FIXED untuk versi pertama (bukan
// adaptif), dikalibrasi via LatencyCalibrator (lihat perannya yang
// baru di bawah) -- BUKAN diwarisi begitu saja dari default upstream
// (1.0), karena karakteristik hardware kita berbeda (GPIO instan vs
// motor/relay upstream).
// ============================================================
#define GRIND_LATENCY_CONFIRMATION_MS   500UL    // window konfirmasi flow pertama -- persis meniru upstream
#define GRIND_LATENCY_TO_COAST_RATIO    1.0f     // TITIK AWAL eksperimen, WAJIB dikalibrasi ulang dari data GPIO+HX711 kita -- lihat README

// KONFIRMASI dari 2 audit independen: grindLatencyMs_ (candidateFlowStartMs_
// - motorStartedMs_) SEBELUMNYA tidak punya batas atas sama sekali --
// kalau flow butuh waktu lama untuk terdeteksi (hopper hampir kosong,
// biji tersangkut sesaat, dst; batas realistis terpanjang adalah
// GRIND_STALL_TIMEOUT_MS di atas sebelum STALL abort terpicu), nilai
// grindLatencyMs_ yang anomali besar itu langsung dipakai mentah ke
// coastTimeMs = grindLatencyMs_ * GRIND_LATENCY_TO_COAST_RATIO --
// menghasilkan motorStopTargetWeightG_ yang jauh lebih besar dari
// wajar, membuat predictive stop mematikan motor JAUH lebih awal dari
// seharusnya (severe undershoot).
//
// GRIND_MAX_PREDICTIVE_LATENCY_MS meng-clamp grindLatencyMs_ SEBELUM
// dipakai ke perhitungan coastTimeMs (lihat evaluateGrindProgress()),
// TIDAK mengubah grindLatencyMs_ itu sendiri (nilai asli tetap
// tersimpan & dilaporkan apa adanya lewat getter/Serial log -- cuma
// nilai YANG DIPAKAI model yang dibatasi).
//
// Nilai 1500ms dipilih SEBAGAI TITIK AWAL KONSERVATIF, BUKAN angka
// final -- kedua audit sepakat menolak klaim "800ms" sebelumnya karena
// tidak berdasar data grinder aktual. 1500ms dipilih sebagai kompromi:
// jauh di atas GRIND_LATENCY_CONFIRMATION_MS (500ms, waktu MINIMUM
// yang realistis untuk flow onset normal), tapi jauh di bawah
// GRIND_STALL_TIMEOUT_MS (5000ms, batas dimana STALL abort sudah
// terpicu duluan) -- memberi ruang untuk variasi onset yang wajar
// (grind size berbeda, dst) tanpa membiarkan anomali ekstrem ikut
// menggerakkan model. WAJIB dikalibrasi ulang dari data grind_latency_ms
// aktual di README setelah beberapa puluh trial fisik -- lihat
// prosedur kalibrasi GRIND_LATENCY_TO_COAST_RATIO, angka ini termasuk
// yang perlu direvisi di siklus kalibrasi yang sama.
#define GRIND_MAX_PREDICTIVE_LATENCY_MS 1500UL

// Stall detection -- motor ON tapi flow tidak muncul (beans habis,
// jalur macet, dst). Grace period dulu supaya spin-up motor tidak
// salah kena deteksi stall.
#define GRIND_MOTOR_STARTUP_GRACE_MS   1000
#define GRIND_STALL_TIMEOUT_MS         5000

// Hard overshoot -- BEDA dari GRIND_ACCURACY_TOLERANCE_G (yang soal
// presisi/kualitas hasil). Ini sanity/safety boundary untuk kondisi
// abnormal (sensor error, pulse runaway, dst) -- bukan target akurasi.
#define GRIND_HARD_OVERSHOOT_G         2.0f

// Pulse correction -- durasi dihitung proporsional (error/flow),
// diclamp ke rentang ini (MIN/MAX di bawah). Flow untuk pulsa pakai
// P95 SESI INI (dihitung sekali setelah predictive stop, bukan
// flow_now real-time per pulsa -- keputusan final setelah bandingkan
// dengan upstream: sisa berat kecil + durasi pulsa pendek membuat
// flow_now terlalu rentan noise/sample sedikit untuk jadi estimator
// pulsa).
// (BUG DITEMUKAN & DIHAPUS lewat audit config: sempat ada konstanta
// duplikat mati "GRIND_MOTOR_MAX_PULSE_DURATION_MS = 250.0f" di sini,
// nilainya sama persis dengan GRIND_MAX_PULSE_DURATION_MS di bawah
// tapi TIDAK PERNAH dipakai di kode manapun -- cuma disebut di
// komentar. Risiko nyata: kalau salah satu diubah tanpa yang lain,
// terjadi inkonsistensi diam-diam. Konstanta mati itu sudah dihapus,
// GRIND_MAX_PULSE_DURATION_MS di bawah adalah SATU-SATUNYA sumber
// kebenaran untuk batas durasi pulsa.)
#define GRIND_MIN_PULSE_DURATION_MS    30.0f
#define GRIND_MAX_PULSE_DURATION_MS    250.0f
#define GRIND_PULSE_P95_WINDOW_MS      2500UL   // window pengumpulan sample untuk hitung P95 flow sesi, persis meniru upstream

// Safety timeout keseluruhan -- dihitung sejak command grind
// DITERIMA (bukan sejak motor ON), mencakup seluruh alur termasuk
// pulse correction. Nilai awal generous, dipersempit setelah lihat
// data durasi grind aktual.
#define GRIND_MAX_DURATION_MS          60000UL

