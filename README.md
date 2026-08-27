# GBW Firmware -- Hardware Final (GPIO + HX711)

Firmware ini menggantikan Tasmota HTTP (WiFi kontrol motor) + Myscale (BLE) sepenuhnya dengan **GPIO langsung ke motor** dan **HX711 + load cell** untuk berat. **Tidak ada BLE sama sekali** di firmware ini. **WiFi TETAP ADA, tapi HANYA untuk OTA update firmware** (lihat `ota_manager.h`/section "OTA" di bawah) -- SAMA SEKALI TIDAK dipakai untuk kontrol motor atau pembacaan berat, beda dari firmware Tasmota lama yang WiFi-nya kritis untuk kontrol.

Project ini **terpisah** dari firmware Tasmota+BLE (yang dipertahankan utuh sebagai referensi/cadangan) -- lihat catatan migrasi di bawah.

## ⚠️ Sebelum mulai

1. **Baca `WIRING.md` sepenuhnya** sebelum menyambung apa pun secara fisik -- motor dikontrol lewat **relay module yang dipasang paralel dengan microswitch fisik Eureka** (bukan sinyal low-voltage langsung ke PCB seperti referensi upstream Specialita), dan sisi kontak relay harus dianggap AC mains sampai diverifikasi sebaliknya. Ada langkah verifikasi keselamatan wajib di `WIRING.md` sebelum menyambung apa pun.
2. Ini firmware Eureka Mignon Manuale (motor **momentary/held** -- HIGH = motor menyala selama ditahan, LOW = berhenti seketika). Kalau grinder kamu beda perilaku (toggle/pulse), `motor_controller.cpp` (`GpioMotorController`) perlu disesuaikan.

## Migrasi dari firmware Tasmota+BLE

Komponen berikut **TIDAK BERUBAH kodenya sama sekali** (sudah agnostic terhadap sumber data sejak desain awal):
- `WeightFilter` -- terima `pushRawSample(weight, timestamp, ...)`, tidak peduli asalnya BLE atau HX711
- `LatencyCalibrator` -- terima `onWeightSample(rawWeight, timestamp)`, sama, agnostic
- `GrindController` -- konsumen `WeightFilter`+`LatencyCalibrator`+`MotorController` (abstrak), tidak peduli implementasi di baliknya

Komponen yang **diganti implementasinya** (interface/abstraksi tetap sama):
- `MotorController`: `TasmotaMotorController` (HTTP) -> `GpioMotorController` (digitalWrite langsung)
- Sumber berat: BLE Myscale (`weight_parser.h` + `RawSampleQueue`) -> `HX711Reader` (polling langsung di `loop()`, tidak perlu queue karena tidak ada callback asinkron)

## ⚠️ Model predictive-stop VERSI 2 -- real-time, bukan regresi

Firmware ini memakai model **real-time per-sesi** (terinspirasi & diverifikasi langsung dari source code `jaapp/smart-grind-by-weight`), BUKAN regresi statistik dari dataset kalibrasi historis (versi sebelumnya):

```
Motor ON
  |
  v
WAIT_FLOW_START -- TIDAK ADA predictive stop di state ini.
  |                Menunggu flow >= 0.5 g/s TERKONFIRMASI selama
  |                500ms berturut-turut (satu sample saja TIDAK
  |                cukup -- kalau flow turun sebelum window selesai,
  |                window direset, menunggu candidate baru).
  |                Kalau flow tidak pernah confirmed dalam
  |                GRIND_STALL_TIMEOUT_MS (5 detik) -> ABORT(STALL).
  v
grind_latency_ms = candidateFlowStartMs - motorStartedMs (T_onset)
  |
  v
GRINDING -- predictive stop AKTIF mulai di sini:
  |
  v
coast_time_ms = grind_latency_ms * GRIND_LATENCY_TO_COAST_RATIO
  |
  v
motor_stop_target_weight_g = flow_now_gps (REAL-TIME) * coast_time_ms / 1000
  |
  v
STOP saat currentWeight >= targetWeight - motor_stop_target_weight_g
  |
  v
SETTLE -> PULSE (pakai P95 flow SESI INI dari window 2.5 detik
                  terakhir sebelum stop, bukan flow_now per pulsa)
```

**Penting**: predictive stop TIDAK PERNAH aktif sebelum flow benar-benar terkonfirmasi (bukan fallback "target penuh" seperti versi awal yang salah) -- kalau flow gagal terdeteksi sama sekali, grind akan ABORT dengan alasan STALL, bukan diam-diam menyelesaikan grind tanpa model prediksi.

**Kenapa diganti dari regresi**: regresi lama (`coastWeightG = a*flowAtStop + b`) dilatih dari trial kalibrasi di SATU kondisi grind (grind size/kopi tertentu). Ganti grind size/kopi mengubah profil flow secara fundamental -- regresi yang dilatih di satu kondisi tidak otomatis representatif untuk kondisi lain. Model real-time ini **otomatis mengikuti kondisi grind SAAT INI** (lewat `flow_now` dan `grind_latency_ms` sesi itu sendiri) tanpa perlu tahu grind size/kopi secara eksplisit.

**`grind_latency_ms` (T_onset)** secara fisik BEDA dari waktu coast/T_stop sesungguhnya (waktu dari keputusan OFF sampai motor benar-benar berhenti mengalirkan kopi) -- `GRIND_LATENCY_TO_COAST_RATIO` adalah faktor konversi antara keduanya, BUKAN safety factor generik.

## ⚠️ Kalibrasi GRIND_LATENCY_TO_COAST_RATIO (WAJIB sebelum grind presisi)

`GRIND_LATENCY_TO_COAST_RATIO` di `config.h` (default `1.0f`) adalah **titik awal eksperimen**, bukan angka final.

**⚠️ KETERBATASAN METODOLOGI YANG DIKETAHUI (ditemukan lewat review, BELUM diperbaiki di kode -- baca sebelum mengikuti prosedur di bawah):** `grindLatencyMs` (T_onset, motor ON -> flow pertama terdeteksi) HANYA dicatat oleh **`GrindController`** (dipicu command `grind <target>`, model real-time V2). Sementara `coastWeightG`/`flowAtStop` HANYA dicatat oleh **`LatencyCalibrator`** (dipicu command `g <target>`, trial terpisah -- lihat `latency_calibrator.h`, struct `CalibrationTrial` TIDAK punya field `grindLatencyMs` sama sekali). **Kedua angka ini berasal dari DUA SESI/COMMAND BERBEDA, bukan satu trial yang sama** -- membandingkan `grindLatencyMs` dari satu sesi `grind` dengan `coastWeightG` dari sesi `g` yang terpisah (walau berturut-turut) BUKAN perbandingan apple-to-apple yang valid secara statistik, karena kondisi grind bisa sedikit berbeda antar sesi (variasi motor spin-up, getaran, dll).

**Cara paling valid untuk kalibrasi ratio SAAT INI** (sampai `LatencyCalibrator` direvisi untuk mencatat `grindLatencyMs` di trial yang sama -- ini perubahan arsitektur terpisah, belum dikerjakan): pakai **`grind <target>` berulang kali** (bukan `g <target>`), baca `grindLatencyMs` DAN `finalErrorG`/overshoot dari **command `gs`** (yang membaca langsung dari `GrindController`, satu sumber data yang sama untuk kedua angka dalam satu sesi grind yang sama):

1. Jalankan `grind <target>` beberapa kali dengan hardware GPIO+HX711 terpasang.
2. Untuk tiap sesi, SEGERA setelah selesai (sebelum sesi berikutnya), ketik `gs` -- catat `grindLatencyMs` DAN error akhir (`finalErrorG`, dari `GrindController::finalErrorG()` -- lihat command Serial `gs`) untuk sesi grind YANG SAMA.
3. Hitung rasio yang **seharusnya** menghasilkan prediksi akurat: kalau error akhir positif (overshoot), `ratio` saat ini terlalu besar -- turunkan sedikit; kalau negatif (undershoot), naikkan sedikit. Iterasi manual (bukan rumus tertutup presisi seperti `ratio_ideal = ...` yang pernah ditulis di sini -- rumus itu mengasumsikan data `LatencyCalibrator` dan `GrindController` bisa dicampur, yang TIDAK valid per keterbatasan di atas).
4. Update `GRIND_LATENCY_TO_COAST_RATIO` di `config.h`, re-flash, ulangi sampai error konsisten dalam toleransi (`GRIND_ACCURACY_TOLERANCE_G`) selama beberapa sesi berturut.

**`LatencyCalibrator` (`g <target>`) tetap berguna untuk keperluan LAIN** -- karakterisasi trajectory coast murni (hubungan flow-saat-off vs overshoot, di luar soal `grindLatencyMs`), TAPI **jangan campur datanya dengan `grindLatencyMs` dari sesi `grind <target>` yang terpisah** seperti prosedur versi sebelumnya di sini secara implisit menyarankan.

**LatencyCalibrator sekarang berperan sebagai alat validasi rasio ini** -- BUKAN lagi sumber model keputusan stop (`GrindController` tidak lagi membaca dataset trial-nya untuk keputusan real-time).

## ⚠️ KALIBRASI ULANG WAJIB (HX711, bukan lagi soal regresi coast)

Data trial `LatencyCalibrator` yang dikumpulkan dari firmware Tasmota+BLE (kalau ada) **TIDAK VALID** di firmware ini -- HX711 punya karakteristik noise/kalibrasi yang beda total dari BLE Myscale. Kumpulkan trial baru dari nol dengan hardware GPIO+HX711 ini terpasang.

## Cara kalibrasi HX711

HX711 mengembalikan angka mentah (ADC value), bukan gram -- perlu dikonversi lewat dua parameter: `offset` (pembacaan saat kosong) dan `scale` (units per gram). Berikut caranya:

### 1. Ambil offset (tare)

1. Pastikan load cell **benar-benar kosong** (tidak ada beban apa pun).
2. Flash & jalankan firmware ini, buka Serial Monitor.
3. Ketik `raw` beberapa kali, catat nilai "Raw average" -- ini `offset` kamu.

Contoh: kalau `raw` menunjukkan nilai rata-rata sekitar `-123456`, itu offset-nya.

### 2. Ambil scale

1. Taruh beban **dengan berat yang kamu tahu pasti** di load cell (misal, koin/beban kalibrasi 100g -- makin presisi beban referensinya, makin akurat kalibrasinya).
2. Ketik `raw` lagi, catat nilai raw average yang baru.
3. Hitung: `scale = (raw_dengan_beban - offset) / berat_referensi_gram`

Contoh: kalau beban referensi 100g menghasilkan raw average `48765`, dan offset dari langkah 1 adalah `-123456`:
```
scale = (48765 - (-123456)) / 100 = 1722.21
```

### 3. Isi ke `config.h`

```cpp
#define HX711_CALIBRATION_OFFSET       -123456L
#define HX711_CALIBRATION_SCALE        1722.21f
```

Re-flash firmware. Ketik `raw` lagi -- sekarang harus muncul juga baris "Dengan kalibrasi config.h saat ini: X.XX g", verifikasi angkanya masuk akal (kosongkan load cell, cek dekat 0; taruh beban referensi lagi, cek dekat berat aslinya).

### Catatan presisi

- Load cell 1kg yang kamu pesan biasanya linear di rentang kerjanya, tapi presisi terbaik biasanya di 10-90% dari kapasitas maksimum. Untuk dose kopi (~15-20g), ini jauh di bawah kapasitas 1kg -- pastikan beban referensi kalibrasi **representatif** (idealnya di rentang gram yang mendekati dose asli, misal 20-50g, bukan cuma 1 beban besar) kalau ingin presisi maksimal di rentang kerja sebenarnya. Kalibrasi 2 titik (0g dan 1 beban) sudah cukup untuk baseline, tapi kalau ada anomali presisi nanti, pertimbangkan kalibrasi multi-titik.

## Command Serial

Sama seperti firmware Tasmota+BLE (alur kerja operator tidak berubah):

- `g <target_gram>` -- mulai kalibrasi coast/decay (reaktif)
- `x` -- batalkan trial berjalan
- `r` -- lihat semua trial tersimpan
- `c` -- hapus riwayat trial
- `grind <target_gram>` -- mulai predictive grind (model real-time -- tidak ada lagi gate kesiapan kalibrasi, bisa langsung dicoba, tapi hasil belum tentu presisi sebelum `GRIND_LATENCY_TO_COAST_RATIO` dikalibrasi)
- `gs` -- lihat angka model real-time (grind_latency_ms, motor_stop_target_weight_g, P95 flow, **final_weight_g/final_error_g BARU ditambahkan**) sesi saat ini/terakhir
- `stop` / `abort` -- paksa hentikan grind yang sedang berjalan (setara tombol Stop di UI, lewat `GrindController::forceAbort()` -- motor dihentikan dengan jalur safety penuh, bukan cuma navigasi UI)
- `raw` -- baca sample mentah HX711 (debug wiring/kalibrasi)

Tidak ada lagi `d`/`t`/`tr` (command diagnostik jaringan) -- tidak relevan, tidak ada jaringan di firmware ini.

## Alur kerja yang disarankan

1. Selesaikan **WIRING.md** (verifikasi keselamatan dulu, baru sambung fisik)
2. Flash firmware, ketik `raw` -- pastikan HX711 merespons
3. Kalibrasi HX711 (lihat di atas), isi `config.h`, re-flash
4. Test motor GPIO **hati-hati** (jauhkan tangan dari burr) -- amati langsung saat kalibrasi `g <target>` yang otomatis start/stop motor
5. Jalankan `g <target>` beberapa kali -- catat `grindLatencyMs` vs overshoot aktual (`r` untuk lihat trial), pakai untuk kalibrasi `GRIND_LATENCY_TO_COAST_RATIO` (lihat bagian di atas)
6. Coba `grind <target>` -- cek `gs` untuk lihat angka model yang dipakai
7. Ulangi kalibrasi rasio kalau hasil masih jauh dari toleransi

## Pin default (lihat WIRING.md untuk diagram lengkap)

| Fungsi | GPIO ESP32-C6 |
|---|---|
| Motor control (via relay module, paralel microswitch) | GPIO6 |
| HX711 DOUT | GPIO2 |
| HX711 SCK | GPIO3 |

Pin ini SENGAJA dipilih untuk tidak bentrok dengan QSPI display board ESP32-C6-Touch-AMOLED-1.64 (yang memakai GPIO4/GPIO5/GPIO7/GPIO10/GPIO11/GPIO19/GPIO20, plus I2C touch/IMU di GPIO8/GPIO18) MAUPUN dengan USB Serial/JTAG Controller ESP32-C6 (GPIO12/GPIO13 default -- firmware ini pakai USB CDC untuk Serial monitor, lihat `platformio.ini`) atau strapping pin (GPIO4/5/8/9/15) -- lihat `src/ui/lv_port.h` untuk daftar lengkap pin terpakai/bebas. Sesuaikan di `include/config.h` kalau wiring fisik kamu beda -- cek dulu daftar GPIO bebas di `src/ui/lv_port.h` sebelum memilih pin baru.

## OTA (update firmware via GitHub Release)

WiFi ditambahkan **khusus** untuk OTA -- **TIDAK dipakai untuk kontrol grind sama sekali**. Motor (GPIO) dan timbangan (HX711) tetap berfungsi normal walau WiFi gagal connect atau sedang proses OTA -- lihat catatan isolasi lengkap di `ota_manager.h`.

**Mekanisme DIGANTI TOTAL (v19)**: sebelumnya pakai `ArduinoOTA` (`espota.py` push firmware dari komputer ke device lewat WiFi lokal). **Diganti ke GitHub OTA** setelah diagnosa menemukan `ArduinoOTA` sangat lambat di jaringan hotspot HP (~2 detik per chunk 1024 byte, walau ping ke device normal) -- pola protokolnya (acknowledgment round-trip tiap chunk kecil) tidak cocok untuk jaringan dengan latency tidak nol. GitHub OTA sebaliknya: device men-download `firmware.bin` dari GitHub Release lewat HTTPS (koneksi TCP tunggal mengalir penuh), jauh lebih cepat untuk kondisi jaringan yang sama. `ArduinoOTA`/`espota.py`/environment `esp32-c6-devkitc-1-ota` di `platformio.ini` **sudah dihapus total**, bukan lagi tersedia sebagai opsi. **Status: berhasil diverifikasi bekerja end-to-end di board fisik** (compile, publish release, download, flash, reboot -- lihat catatan redirect di bawah).

### Setup awal

1. Isi `OTA_WIFI_SSID`/`OTA_WIFI_PASSWORD` di `include/config.h` sesuai WiFi/hotspot kamu -- **hotspot ini WAJIB punya akses internet** (bukan cuma hotspot lokal tanpa data), karena GitHub OTA butuh mengakses `api.github.com`/`github.com` lewat internet, bukan cuma jaringan lokal.
2. Isi `GITHUB_OTA_OWNER`/`GITHUB_OTA_REPO` di `include/config.h` sesuai repo GitHub publik kamu (contoh project ini: `wahyupras90`/`gbw_manuale`).
3. Flash pertama kali tetap **lewat USB** (`pio run -e esp32-c6-devkitc-1 -t upload`) -- WiFi/GitHub OTA baru bisa dipakai setelah firmware dengan `GithubOtaManager` ini sudah terpasang.

### Publish update baru

1. Compile firmware seperti biasa: `pio run -e esp32-c6-devkitc-1` (TANPA `-t upload` kalau cuma perlu file `.bin`-nya, tidak perlu USB tersambung).
2. Ambil file `.pio/build/esp32-c6-devkitc-1/firmware.bin`.
3. Buat GitHub Release baru di repo (lewat web GitHub atau `gh release create <tag> firmware.bin`), **attach file dengan nama PERSIS `firmware.bin`** (sesuai `GITHUB_OTA_ASSET_NAME` di `config.h`) sebagai asset release.
4. Di board: buka Settings, pastikan WiFi/hotspot dengan internet aktif, tekan tombol **"CHECK"** di baris "Firmware Update". Device akan query GitHub API, temukan asset di release terbaru, download, flash, lalu reboot otomatis kalau sukses.

**Tidak perlu bongkar case atau colok USB lagi** untuk update logic/bugfix setelahnya -- selama board masih bisa dijangkau WiFi dan layar/tombol masih bisa disentuh, seluruh alur update cukup lewat tombol CHECK ini.

### Catatan penting

- **Trigger MANUAL, bukan otomatis.** Tidak ada polling berkala ke GitHub di background -- device hanya mengecek update saat tombol CHECK ditekan. Ini keputusan eksplisit (tidak perlu device diam-diam mengecek internet sendiri).
- **`checkAndUpdate()` bersifat BLOCKING** -- LVGL sengaja tidak responsif selama proses check+download+flash berlangsung (bisa beberapa detik sampai beberapa menit tergantung ukuran firmware & kecepatan internet), sama filosofinya dengan bagaimana `ArduinoOTA` dulu memblokir saat upload berlangsung.
- **TLS pakai `setInsecure()`** (skip verifikasi root CA GitHub) -- keputusan eksplisit, dianggap cukup untuk alat rumahan ini, bukan sistem yang menghadapi ancaman keamanan jaringan serius. Lihat catatan lengkap di `github_ota.h`.
- Repo GitHub **publik** -- tidak perlu token/autentikasi apa pun untuk fetch release/asset-nya.
- **`setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS)` WAJIB dipasang** sebelum `httpUpdate.update()` -- `browser_download_url` dari GitHub Release selalu redirect (`github.com` -> `objects.githubusercontent.com`), dan `HTTPClient`/`HTTPUpdate` tidak follow redirect otomatis secara default. Sudah dipasang & **dikonfirmasi bekerja lewat testing aktual di board** (tanpa ini, download gagal persis di titik redirect).
- `httpUpdate.onProgress()` (progress bar granular) SENGAJA belum dipasang -- signature API-nya belum diverifikasi lewat compile aktual di komputer Wahyu saat perubahan ini dibuat. Progress tetap terlihat lewat log default `httpUpdate` di Serial Monitor. Bisa ditambahkan setelah signature dikonfirmasi lewat compile aktual (lihat catatan di `github_ota.cpp`).
- WiFi selalu aktif & auto-reconnect (bawaan ESP32 STA mode) -- kalau jaringan WiFi kamu berubah (ganti SSID/password), update `config.h` dan re-flash lewat USB.
- Status WiFi ditampilkan di status bar UI (icon WiFi, ijo = connected) -- lihat `src/ui/ui_common.h`. Icon BLE di sebelahnya masih placeholder (selalu abu-abu), belum ada fitur BLE di firmware ini.

## Status UI LVGL (`src/ui/`)

6 screen sudah ditulis sesuai mockup HTML yang disetujui (`gbw_ui_flow.html`): Set Target, Idle, Predictive Grind, Pulse Correction, Done, Settings. **SEKARANG SUDAH di-include ke build** (`build_src_filter` di `platformio.ini` sudah tidak exclude `ui/` lagi) dan disambung ke `GrindController`:

1. **LVGL library** sudah ditambahkan ke `lib_deps` (`lvgl/lvgl @ ^8.4.0`, sesuai versi yang dipakai contoh resmi Waveshare untuk board ini), plus `moononournation/GFX Library for Arduino @ 1.3.7` (**versi DI-PIN**, bukan lagi tanpa versi -- 1.3.7 dikonfirmasi dipakai vendor lain, `Xinyuan-LilyGO/T-Display-S3-AMOLED-1.64`, untuk board dengan chip CO5300 yang sama, jadi sudah terbukti kompatibel dengan kelas `Arduino_CO5300`).
2. **Driver display + touch** ada di `src/ui/lv_port.h`/`.cpp` -- pakai `Arduino_CO5300` (Arduino_GFX, QSPI) untuk panel dan polling I2C manual ke FT3168/FT6146 untuk touch. **Nomor pin TERVERIFIKASI** langsung dari clone `github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.64`: `LCD_CS=10, LCD_PCLK=11, LCD_DATA0=4, LCD_DATA1=5, LCD_DATA2=7, LCD_DATA3=19, LCD_RST=21` (**dikoreksi dari GPIO20** -- BSP ESP-IDF resmi vendor menyebut GPIO20, TAPI contoh Arduino resmi -- acuan yang benar karena firmware ini Arduino_GFX-style -- menyebut GPIO21; dua sumber resmi vendor sendiri tidak konsisten, GPIO21 yang dipakai); touch I2C `SDA=18, SCL=8` (bus dipakai bersama IMU), `TOUCH_INT=1`. **Offset X +20** (`col_offset1=20` di constructor `Arduino_CO5300`) **ditambahkan** -- source resmi vendor mengonfirmasi RAM controller CO5300 butuh offset ini (`area->x1 + 0x14` di flush callback ESP-IDF asli); Arduino_GFX menerapkan offset setara lewat parameter constructor (`_xStart`, diterapkan otomatis di semua operasi gambar termasuk `draw16bitRGBBitmap`), bukan manual di flush callback. **Bug constructor lain juga ditemukan & diperbaiki**: draft sebelumnya menyisipkan parameter `false /* IPS */` yang TIDAK ADA di signature resmi `Arduino_CO5300` -- sudah dihapus. Touch **init sequence** (kembalikan controller ke normal mode, register `0x00`) dan **clamp koordinat** ke batas resolusi panel sekarang disalin persis dari source resmi (sebelumnya tidak ada keduanya).
3. **✅ Bentrok pin motor/HX711 vs QSPI display + USB-JTAG -- SUDAH DIPERBAIKI (2 iterasi).** Source resmi vendor mengonfirmasi `LCD_DATA0=GPIO4` dan `LCD_DATA1=GPIO5` dipakai QSPI ke panel AMOLED (berbagi bus fisik dengan SD card: `SD_MOSI=GPIO4, SD_MISO=GPIO5, SD_CLK=GPIO11`), yang sebelumnya bentrok langsung dengan `MOTOR_GPIO_PIN`/`HX711_DOUT_PIN` default lama. Iterasi pertama memindah ke `MOTOR_GPIO_PIN=GPIO6`, `HX711_DOUT_PIN=GPIO2`, `HX711_SCK_PIN=GPIO12` -- tapi **GPIO12 ternyata pin default USB Serial/JTAG Controller ESP32-C6** (terverifikasi dari datasheet & ESP-IDF Programming Guide resmi Espressif), bentrok dengan Serial monitor project ini yang memakai `-DARDUINO_USB_CDC_ON_BOOT=1`. **Diperbaiki lagi**: `HX711_SCK_PIN` dipindah ke `GPIO3` (bukan strapping pin -- strapping pin resmi ESP32-C6 hanya GPIO4/5/8/9/15 -- dan bukan USB-JTAG). Pin final: `MOTOR_GPIO_PIN=GPIO6`, `HX711_DOUT_PIN=GPIO2`, `HX711_SCK_PIN=GPIO3` -- ketiganya terverifikasi tidak bentrok display/touch/IMU/SD card/BOOT button/strapping/USB-JTAG. Lihat daftar lengkap pin terpakai/bebas di `src/ui/lv_port.h`.
4. **Wiring `GrindController` <-> `g_ui_state`** dua arah sekarang lengkap di `main.cpp`: `syncGrindControllerToUi()` menyalin getter `GrindController` -> `g_ui_state` tiap `loop()` (arah controller-ke-UI), DAN **`syncUiSettingsToGrindController()` (BARU)** menyalin `g_ui_state.accuracy_tolerance_g`/`max_pulse_attempts` -> `GrindController` lewat setter (arah UI-ke-controller). **✅ Bug ditemukan & diperbaiki**: sebelumnya Settings screen (stepper Tolerance/Max Pulses) HANYA mengubah `g_ui_state`, TIDAK PERNAH memengaruhi algoritma -- `GrindController` tetap memakai konstanta `GRIND_ACCURACY_TOLERANCE_G`/`GRIND_MAX_PULSE_ATTEMPTS` langsung dari `config.h` di 5 lokasi berbeda. Sekarang `GrindController` punya member `accuracyToleranceG_`/`maxPulseAttempts_` yang bisa di-override lewat `setAccuracyToleranceG()`/`setMaxPulseAttempts()`, **di-snapshot ke sesi aktif SAAT `startGrind()` dipanggil** (bukan real-time selama grinding) -- supaya operator mengubah Settings di tengah grind yang sedang berjalan tidak mengubah parameter sesi itu, baru berlaku efektif di sesi berikutnya. `handleGrindStateTransitionForUi()` tetap seperti sebelumnya, memanggil `ui_transition_to_pulse_correction()`/`ui_transition_to_done()` secara edge-triggered saat `GrindController::state()` berubah.
5. **Tombol Stop** di screen Predictive Grind & Pulse Correction memanggil `grind_force_abort()` (extern function di `main.cpp`, membungkus `GrindController::forceAbort(AbortReason::NONE)`) -- motor benar-benar dihentikan dengan jalur safety penuh (`doAbort()`/`stopMotorOrAbort()`) sebelum navigasi ke screen Done, bukan langsung `ui_new_grind()` seperti placeholder lama. Command Serial `'stop'`/`'abort'` juga ditambahkan untuk paritas debugging tanpa UI.
6. **Persistence Settings** (Tolerance & Max Pulses) ke NVS/flash **masih belum ada** -- nilai efektif sekarang benar-benar dipakai algoritma (lihat poin 4), tapi tetap hilang saat reboot (kembali ke default `config.h`). Ini masih TODO terbuka.

**Yang SENGAJA belum ada** (per keputusan, bukan item TODO): toggle Auto/Manual, "learned" value, dan persistence untuk Coast Ratio (`GRIND_LATENCY_TO_COAST_RATIO`) dan Flow Threshold (`GRIND_FLOW_DETECTION_THRESHOLD_GPS`) di Settings -- ini baru dikerjakan setelah keputusan adaptive learning dikunci terpisah (lihat brief Adaptive Learning v1.0 yang terpisah).

**Klarifikasi retry count motor OFF** (setelah sempat salah dihitung dalam satu review external sebagai "6 attempt"): worst case SEBENARNYA adalah **3x `motor_->stop()`** (1x panggilan awal + 1x retry di `GrindController` + 1x lagi di `doAbort()`), BUKAN 4 (klaim brief lama) atau 6. `GpioMotorController::stop()` (implementasi final, digitalWrite langsung) TIDAK punya retry internal sendiri -- itu ciri `TasmotaMotorController` (implementasi HTTP lama) yang sudah tidak dipakai. Lihat komentar lengkap di `stopMotorOrAbort()` (`grind_controller.cpp`).

**✅ COMPILE AKTUAL BERHASIL** (`platformio run` di toolchain ESP32-C6 sungguhan, bukan lagi cuma review statis) -- lihat section "Riwayat perbaikan v16" di bawah untuk daftar lengkap bug yang ditemukan & diperbaiki lewat compile pertama ini (versi library, `lv_conf.h`, missing include, escape sequence UTF-8 salah). Hasil build: Flash 70.1% (1,378,634/1,966,080 bytes), RAM 43.6% (142,988/327,680 bytes), nol warning dari kode project sendiri.

Lihat `gbw_ui_flow.html` untuk referensi visual lengkap tiap screen.

## Riwayat perbaikan v11 (setelah review mendalam v10)

Review menyeluruh terhadap v10 (source firmware + UI + wiring + hardware) menemukan beberapa bug nyata. Semua sudah diperbaiki:

1. **✅ CO5300 rounder_cb -- BUG DITEMUKAN & DIPERBAIKI.** Panel CO5300 board ini butuh area refresh dengan koordinat/ukuran GENAP (byte-boundary requirement) -- source resmi vendor (`lcd_bsp.c`) memasang `disp_drv.rounder_cb` untuk ini, tapi `lv_port.cpp` versi sebelumnya TIDAK memasangnya sama sekali. Tanpa ini, UI berisiko artefak visual (garis/teks hilang, area tidak ter-refresh) tergantung area redraw yang sedang berubah -- berisiko operator salah baca angka berat. **Sekarang ditambahkan**: `disp_rounder_cb()` di `lv_port.cpp`, disalin persis dari source resmi, di-register ke `s_disp_drv.rounder_cb`.
2. **✅ Urutan `ui_transition_to_done()` -- BUG DITEMUKAN & DIPERBAIKI.** Versi sebelumnya memanggil `ui_screen_done_update()` SEBELUM `navigate_to(UI_SCREEN_DONE)` -- pada grind PERTAMA sejak boot, widget Done screen (`s_weight_label` dkk) belum pernah dibuat (`nullptr`), jadi guard di `ui_screen_done_update()` langsung `return` tanpa mengisi apa pun. Operator akan melihat teks default/mockup pada hasil grind pertama, bukan angka aktual (grind kedua dst kebetulan benar, karena widget sudah ada dari grind pertama). **Sekarang diperbaiki**: `navigate_to()` dipanggil dulu (memastikan widget ada), baru `ui_screen_done_update()`.
3. **✅ Semantik dose vs absolute -- BUG DITEMUKAN & DIPERBAIKI (paling substansial).** `g_ui_state.target_weight_g` adalah DOSE tambahan (mis. 18g), sementara `GrindController` bekerja dengan target ABSOLUT (`startWeight + dose`, mis. 250g+18g=268g). Versi sebelumnya memakai `target_weight_g` langsung untuk hitung error di screen Done (`current - target_weight_g` = `268 - 18` = `+250g`, salah total) dan progress ring Predictive Grind (`current/target_weight_g`, langsung dianggap >100% kalau berat awal > dose). **Sekarang diperbaiki**: dua field baru `target_absolute_g`/`start_weight_g` ditambahkan ke `g_ui_state` (lihat `ui_common.h`), disinkronkan dari `GrindController::targetAbsoluteG()`/`startWeightG()` lewat `syncGrindControllerToUi()` di `main.cpp`. `screen_done.cpp` sekarang hitung error dari `target_absolute_g`; `screen_predictive_grind.cpp` sekarang hitung progress dari dose (`(current-start)/(target_absolute-start)`), bukan rasio mentah. `target_weight_g` TETAP dipakai murni untuk label ("TARGET 18.0g"), itu sudah benar dari awal.
4. **✅ Relay module -- DIKONFIRMASI PASTI dari foto produk toko** (bukan lagi dugaan): "Modul Relay 1 Channel DC 3V Optocoupler **Active High**" -- jadi `MOTOR_GPIO_ACTIVE_HIGH=true` di `config.h` SUDAH BENAR, dan modul memang punya isolasi optocoupler sesuai deskripsi produk. `WIRING.md` diperbarui dengan info produk pasti ini (part relay `JQC3F-03VDC-C`, kontak 10A 250VAC).
5. **✅ Metodologi kalibrasi ratio -- keterbatasan didokumentasikan, prosedur diperbaiki.** `grindLatencyMs` (dari `GrindController`, dipicu `grind <target>`) dan `coastWeightG`/`flowAtStop` (dari `LatencyCalibrator`, dipicu `g <target>`) berasal dari DUA SESI/COMMAND BERBEDA -- membandingkan keduanya seolah satu trial (seperti prosedur versi sebelumnya) tidak valid secara statistik. **Prosedur baru** (lihat README bagian kalibrasi ratio): pakai `grind <target>` berulang + command `gs` (yang sekarang juga menampilkan `final_weight_g`/`final_error_g`, sebelumnya tidak ada) untuk baca `grindLatencyMs` DAN error akhir dari SESI YANG SAMA.
6. **✅ Komentar usang "tekan & tahan microswitch manual" di `LatencyCalibrator` -- diperbaiki.** Sisa dokumentasi dari era sebelum topologi relay-paralel-microswitch (arsitektur final sekarang: relay otomatis "menekan" microswitch, operator tidak perlu aksi manual). Pesan log & komentar arsitektur di `latency_calibrator.h`/`.cpp` sudah diperbarui.
7. **✅ Force-off di `LatencyCalibrator::startGrind()` -- ditambahkan.** Sebelumnya kalau `motor_->start()` gagal, kalibrator cuma batal sesi tanpa force-off (beda dari `GrindController` yang punya `doAbort()`). Sekarang konsisten -- `motor_->stop()` dipanggil sebagai jaga-jaga (untuk `GpioMotorController` saat ini praktis tidak pernah terpicu karena `r.success` selalu `true`, tapi invariant safety ini penting kalau `MotorController` lain dipakai lagi di masa depan).
8. **✅ OTA ditolak otomatis saat grinding -- ditambahkan** (lihat bagian OTA di atas).

**Belum diperbaiki (didokumentasikan sebagai keterbatasan, butuh keputusan/perubahan arsitektur terpisah):**
- `CalibrationTrial` (struct di `latency_calibrator.h`) tidak menyimpan `grindLatencyMs` per trial -- untuk kalibrasi ratio benar-benar valid dari satu sumber data, struct ini idealnya direvisi untuk mencatat `grindLatencyMs` di trial yang sama dengan `coastWeightG`. Ini perubahan arsitektur yang lebih besar, belum dikerjakan.

## Riwayat perbaikan v12 (setelah review mendalam v11)

Review lanjutan terhadap v11 (termasuk cross-check ke datasheet resmi komponen fisik yang dipakai) menemukan beberapa perbaikan lagi:

1. **✅ Suplai `VCC` relay -- DIPINDAH dari 3.3V board display ke sumber terpisah (perbaikan paling penting sesi ini).** Datasheet relay sejenis (`SRD-03VDC-SL-C`, coil data chart) menyebut **arus coil ~120mA @ 3V** -- lihat catatan penting di v13 di bawah soal ketidakpastian identitas part fisik. Board sepupu yang didokumentasikan resmi Waveshare (ESP32-C6-Touch-AMOLED-**2.06**) menyebut sisa headroom regulator 3.3V on-board cuma <50mA setelah motherboard+layar -- 120mA jelas melebihi itu. **Diperbaiki**: `VCC` relay sekarang disuplai dari modul step-down 5V->3V terpisah (diberi daya dari buck converter 5V yang sudah ada di project), BUKAN dari pin 3.3V board ESP32-C6-Touch-AMOLED-1.64. `GND` tetap WAJIB disambung bersama di semua titik (buck converter, step-down, relay, ESP32) -- lihat diagram lengkap di `WIRING.md`.
2. **✅ `GpioMotorController` -- inisialisasi hardware dipindah dari constructor ke `begin()` (bug arsitektur, DIPERBAIKI).** `motorController` dideklarasikan `static` global di `main.cpp` -- constructor-nya (yang sebelumnya langsung memanggil `pinMode()` + `writePin(false)`, dan `writePin()` di dalamnya memanggil `Serial.printf()`) berjalan SEBELUM `setup()`/`Serial.begin()` dipanggil sama sekali. Ini bergantung urutan static-initialization yang implisit (biasanya "kebetulan aman" di Arduino/ESP32, tapi bukan jaminan portable/robust untuk firmware yang mengontrol motor mekanis). **Diperbaiki**: constructor sekarang HANYA menyimpan parameter (tidak ada akses hardware/Serial sama sekali); `void begin()` baru ditambahkan untuk inisialisasi hardware sebenarnya, dipanggil eksplisit dari `setup()` SETELAH `Serial.begin()` (lihat `main.cpp`).
3. **✅ Dokumentasi kontradiktif soal status verifikasi pin -- diperbaiki.** Komentar header `main.cpp` masih menyebut pin QSPI/touch sebagai "PLACEHOLDER, belum diverifikasi" -- padahal `lv_port.h` dan bagian lain README sudah menyatakan pin itu terverifikasi dari source resmi vendor (sisa dari draft awal sebelum verifikasi selesai, tidak pernah diperbarui). **Diperbaiki**: komentar sekarang menyatakan dengan akurat bahwa pin sudah diverifikasi dari SOURCE/DOKUMEN resmi vendor, tapi belum diverifikasi secara FISIK pada unit board yang benar-benar terpasang (dua hal yang berbeda) -- lebih tepat daripada klaim "placeholder" yang sudah usang.
4. **✅ `computeSessionP95()` -- `std::vector` diganti array fixed (heap-free, DIPERBAIKI).** Fungsi ini sebelumnya memakai `std::vector<float> inWindow` (alokasi heap), kontradiksi dengan komentar buffer penyimpanan (`g_flowHistoryBuf`) yang menegaskan "bukan heap growable". Dampaknya kecil dalam praktik (fungsi ini cuma dipanggil sekali per sesi grind, bukan di jalur real-time), tapi untuk konsistensi penuh dengan prinsip heap-free di jalur grinding, **diganti** array `static float` fixed-size + `std::sort` langsung di array C-style -- tidak ada alokasi heap sama sekali lagi di fungsi ini.
5. **Duplikasi pemanggilan `ui_screen_done_update()`** -- diperiksa ulang, TIDAK ditemukan di source v11 (hanya satu pemanggilan, sudah benar sejak perbaikan urutan di v11). Kemungkinan review sebelumnya melihat versi/draft yang berbeda -- tidak ada perubahan yang diperlukan di sini.

## Riwayat perbaikan v13 (koreksi identitas relay fisik)

Review lanjutan menemukan bahwa v12 secara keliru mengunci identitas part relay:

1. **✅ Ketidakpastian nama part relay -- diperbaiki, bukan lagi diklaim pasti.** `WIRING.md` v12 menyatakan modul relay yang dipakai "TERKONFIRMASI PASTI" sebagai `SRD-DC03V-SL-C`/`SRD-03VDC-SL-C`, dan mengunci angka arus coil 120mA dari datasheet part itu. **Ini salah** -- part number yang tercetak di badan relay fisik (dari foto produk) adalah **`JQC3F-03VDC-C`** (manufaktur Bestep/T73), BUKAN `SRD-03VDC-SL-C` (manufaktur Songle) yang cuma disebut di teks deskripsi listing toko. Kedua part number ini **kemungkinan besar setara secara elektrik** (pola umum industri relay China: form-factor `T73` 5-pin dengan spesifikasi coil yang konsisten lintas "merek" berbeda), TAPI ini kesimpulan berdasarkan pola industri, bukan verifikasi pasti untuk unit fisik yang dipegang. **Diperbaiki**: `WIRING.md` sekarang menyatakan ketidakpastian ini secara eksplisit, dan menambahkan **langkah verifikasi arus coil dengan multimeter** (ukur langsung, in-line di jalur `VCC`, sebelum menyambung ke GPIO/microswitch) sebagai cara paling pasti untuk memastikan angka arus yang sebenarnya -- menggantikan ketergantungan pada datasheet part number yang mungkin tidak 100% cocok dengan unit fisik. Keputusan suplai 3V terpisah (poin 1 di atas) TETAP BERLAKU terlepas dari ketidakpastian ini -- memisahkan suplai relay dari regulator board display tetap praktik yang lebih aman.

## Riwayat perbaikan v14 (koreksi dokumentasi setelah review v13)

Review lanjutan menemukan 2 hal dokumentasi yang kontradiktif/tidak akurat (tidak ada perubahan kode, murni dokumentasi):

1. **✅ Bentuk konektor relay -- diperbaiki dari "header 4-pin" ke "terminal 3-pin".** `WIRING.md` sebelumnya menulis "Header 4-pin di sisi kanan: `GND`, `IN`, `VCC` + jumper" -- ini kontradiksi internal (menyebut "4-pin" tapi cuma nama 3 pin) yang bisa membuat orang mencari pin ke-4 yang sebenarnya tidak ada di terminal itu. **Diperbaiki**: terminal kontrol dinyatakan dengan benar sebagai 3-pin (`GND`/`IN`/`VCC`), dan jumper "double power selection" dijelaskan sebagai komponen 2-pin TERPISAH di PCB (dekat chip optocoupler), bukan pin tambahan di terminal blok yang sama. Section "Posisi jumper source select" & langkah TEST relay juga diperbarui supaya konsisten (tidak lagi menyebut "soket terminal kiri" sebagai sumber daya coil -- yang benar, sisi kiri adalah terminal KONTAK `NC`/`COM`/`NO`, bukan sumber daya).
2. **✅ Pernyataan "Tidak ada WiFi/BLE sama sekali" di awal README -- diperbaiki, kontradiksi dengan isi project.** Baris pembuka README menyatakan firmware ini "Tidak ada WiFi/BLE sama sekali" -- padahal `ota_manager.h`/`.cpp` jelas memakai WiFi untuk OTA (fitur yang sudah ada sejak beberapa versi sebelumnya, dan sudah didokumentasikan dengan benar di bagian lain README/`main.cpp`). Baris pembuka ini adalah sisa dari draft sangat awal sebelum fitur OTA ditambahkan, tidak pernah diperbarui. **Diperbaiki**: pernyataan sekarang akurat -- "Tidak ada BLE sama sekali", "WiFi TETAP ADA, tapi HANYA untuk OTA update firmware... SAMA SEKALI TIDAK dipakai untuk kontrol motor atau pembacaan berat".

## Riwayat perbaikan v15 (koreksi interpretasi jumper relay + platformio.ini stale)

Review lanjutan menemukan interpretasi jumper relay di v14 salah, dan satu lagi dokumentasi WiFi yang stale:

1. **✅ Interpretasi jumper "double power selection" -- DIPERBAIKI TOTAL, versi v14 SALAH.** `WIRING.md` v14 menjelaskan jumper ini sebagai "coil mengambil daya dari kontak NC/COM/NO (internal) vs dari VCC/GND eksternal" -- itu tebakan tanpa dasar sumber, dan terbukti keliru: kontak relay (`NC`/`COM`/`NO`) TIDAK PERNAH jadi sumber daya untuk apa pun (itu murni jalur switching mekanis). **Diperbaiki, berdasar referensi teknis topologi modul relay 1-channel dengan optocoupler EL817 sejenis**: modul seperti ini biasanya punya DUA rail suplai DC internal -- `VCC` (suplai LED optocoupler/kontrol, arus kecil ~5mA) dan `JD-VCC` (suplai coil relay, arus besar ~120mA) -- yang secara default DISATUKAN lewat jumper/link kecil di PCB ("shared power"). Jumper DILEPAS memisahkan keduanya ("separate power") -- **INI yang dipakai project ini**, supaya `JD-VCC` (coil) disuplai dari step-down 3V terpisah, TIDAK ikut ditarik dari sumber `VCC`/kontrol yang sama. Klarifikasi soal common ground juga diperbaiki: `GND` tetap wajib disambung bersama (soal referensi sinyal `IN`, bukan soal isolasi daya), dan isolasi optocoupler yang sebenarnya berlaku adalah antara SISI KONTROL (termasuk `VCC`+`JD-VCC`/coil) dan SISI KONTAK (`COM`/`NO`/`NC`, AC mains) -- bukan antara `VCC` dan `JD-VCC` itu sendiri. Langkah verifikasi arus dengan multimeter juga diperbaiki (ukur di jalur `JD-VCC`, bukan `VCC`, karena `VCC` cuma arus LED optocoupler yang kecil). **Catatan kejujuran yang tetap dipertahankan**: penjelasan ini berdasar referensi topologi modul SEJENIS (bukan datasheet modul spesifik yang dipakai), layout PCB relay murah bisa bervariasi antar batch -- verifikasi visual jalur PCB tetap disarankan sebelum menganggap 100% berlaku untuk unit fisik yang dipegang.
2. **✅ `platformio.ini` -- komentar "TIDAK PAKAI WiFi/BLE sama sekali" diperbaiki.** Sama seperti README v14, `platformio.ini` juga punya sisa komentar stale dari draft sebelum fitur OTA ada. **Diperbaiki**: sekarang menyatakan "TIDAK PAKAI BLE sama sekali. WiFi TETAP DIPAKAI, tapi HANYA untuk OTA" -- konsisten dengan README dan `main.cpp`.

## Riwayat perbaikan v16 (COMPILE AKTUAL PERTAMA KALI -- bug baru ditemukan)

Setelah 15 iterasi review statis, v16 adalah versi pertama yang benar-benar dicompile lewat toolchain ESP32-C6 sungguhan (`platformio run`). Hasilnya: **compile berhasil (`[SUCCESS]`)**, tapi menemukan beberapa bug nyata yang TIDAK KELIHATAN dari review statis manapun -- ini bukti konkret kenapa compile aktual penting, bukan formalitas:

1. **✅ `platformio.ini`: versi `Arduino_GFX` yang di-pin (`@1.3.7`) TERNYATA BELUM PUNYA kelas `Arduino_CO5300` sama sekali** -- kelas itu baru ditambahkan di v1.4.8 (dan versi itu pun beda signature constructor dari yang dipakai `lv_port.cpp`; signature yang cocok baru stabil mulai v1.6.1). **Diperbaiki**: pin dinaikkan ke `@1.6.7` (diverifikasi langsung: clone source, cek isi file, match persis dengan `lv_port.cpp`).
2. **✅ `include/lv_conf.h` tidak lengkap -- HAMPIR SEMUA widget LVGL v8 default AKTIF kalau tidak disebutkan eksplisit.** Draft sebelumnya cuma menyebutkan widget yang benar-benar dipakai (`lv_arc`, `lv_btn`, `lv_label`, dst) dan mengasumsikan sisanya otomatis mati -- SALAH. Widget seperti `lv_calendar`, `lv_keyboard`, `lv_msgbox`, `lv_spinbox`, dst semuanya default aktif dan saling bergantung ke `LV_USE_BTNMATRIX`/`LV_USE_TEXTAREA` yang sengaja dimatikan, memicu cascading `#error` compile-time. **Diperbaiki**: SEMUA widget yang tidak dipakai (bukan cuma yang "kelihatan currently error") dimatikan eksplisit, disalin dari daftar lengkap `lv_conf_template.h` resmi LVGL v8.4.0.
3. **✅ `platformio.ini`: `-I include` ditambahkan ke `build_flags`.** `-DLV_CONF_INCLUDE_SIMPLE` saja tidak cukup -- PlatformIO tidak otomatis mengexpose folder `include/` project ke library eksternal (`lvgl` di `lib_deps`), menyebabkan `fatal error: lv_conf.h: No such file or directory` walau file itu memang ada.
4. **✅ `src/ui/lv_port.cpp`: `BLACK` bukan konstanta yang dikenal Arduino_GFX** (itu nama konstanta khas Adafruit_GFX, library berbeda) -- diganti `RGB565_BLACK` (nama resmi Arduino_GFX).
5. **✅ Escape sequence UTF-8 salah di 4 file UI (`screen_done.cpp`, `screen_idle.cpp`, `screen_predictive_grind.cpp`, `screen_pulse_correction.cpp`, `screen_set_target.cpp`) -- total 9 lokasi.** Pola `"\xC2\xB1"` (dimaksudkan 2 byte UTF-8 terpisah untuk `±`) dibaca compiler C++ sebagai SATU hex escape sequence panjang (aturan C++: hex escape terus "melahap" digit hex berikutnya tanpa batas otomatis) -- menghasilkan warning "hex escape sequence out of range" dan isi string yang salah/terpotong. **Diperbaiki**: semua diganti literal Unicode langsung (`"\u00B1"`, `"\u2014"`, `"\u2713"`, dst), lebih sederhana dan benar karena `LV_TXT_ENC_UTF8` sudah aktif di `lv_conf.h`.
6. **✅ 6 file `src/ui/*.cpp` kehilangan `#include <cstdio>`/`<math.h>` yang dipakai (`snprintf`/`fabsf`/`isnan`).** Kode ini kemungkinan "kebetulan" berhasil compile di lingkungan lain lewat include transitif tidak eksplisit dari `<lvgl.h>` -- rapuh dan tidak portable. **Diperbaiki**: semua file yang butuh ditambahkan include eksplisit. Catatan teknis: dipakai `<math.h>` (C-style, fungsi di global namespace), BUKAN `<cmath>` (C++-style, fungsi di `std::` -- sempat dicoba dan menghasilkan error kedua "isnan tidak dideklarasikan, maksudnya std::isnan?"), supaya konsisten dengan `grind_controller.cpp` yang sudah lebih dulu pakai `<math.h>`.

**Hasil akhir compile**: `[SUCCESS]`, Flash 70.1% (1,378,634/1,966,080 bytes), RAM 43.6% (142,988/327,680 bytes), **nol warning dari kode project sendiri** (hanya warning tooling `freertos-gdb` opsional yang tidak relevan).

**Catatan metodologi**: compile ini dijalankan di lingkungan sandbox yang tidak bisa mengakses `api.registry.platformio.org` (PlatformIO Library Registry) karena kebijakan jaringan sandbox itu sendiri -- untuk mengatasinya, `bogde/HX711`, `lvgl/lvgl`, dan `moononournation/GFX Library for Arduino` di-clone manual dari GitHub ke folder `lib/` sementara (dihapus lagi setelah verifikasi selesai, TIDAK disertakan di ZIP ini). **Di komputer Anda, `lib_deps` di `platformio.ini` akan resolve normal lewat PlatformIO Library Manager** seperti biasa -- tidak perlu langkah manual apa pun, cukup jalankan `pio run` seperti biasa dan PlatformIO akan mengunduh ketiga library itu otomatis.

## Audit menyeluruh v17 (setelah compile berhasil)

Setelah compile v16 sukses, dilakukan audit sistematis terhadap `GrindController`, timestamp, HX711, UI, motor/relay, safety, pulse correction, dan konfigurasi -- membandingkan source aktual terhadap spesifikasi yang sudah disepakati di iterasi-iterasi sebelumnya. Hasilnya:

**✅ PASS tanpa perubahan (diverifikasi baris-per-baris terhadap source, bukan asumsi):**
- State transition SELALU lewat `transitionTo()` (satu titik kontrol) -- tidak ada state diubah langsung di tempat lain.
- `startGrind()` gagal (`motor_->start()` tidak sukses) -> `doAbort(MOTOR_COMMAND_FAILED)`, bukan `transitionTo(ABORT)` langsung.
- Tare/stability check di `startGrind()`: `flow.valid == true` DAN `abs(flow) <= threshold` -- KEDUANYA wajib (baris 173 & 180 `grind_controller.cpp`).
- `motorStartedMs_ = r.commandSentMs` (basis grace period & stall -- sengaja konservatif), `motorStoppedMs_ = r.responseRecvMs`/`retry.responseRecvMs` (basis settle timer). Settle timer SELALU pakai `millis() - motorStoppedMs_`, tidak pernah `sampleTimestampMs - motorStoppedMs_`.
- `stopMotorOrAbort()`: stop -> gagal? retry -> masih gagal? `doAbort(MOTOR_OFF_FAILED)`. Tidak ada jalur ke `WAIT_SETTLE` tanpa OFF terkonfirmasi.
- `doAbort()`: force-off dulu, simpan `abortReason_`+`result_`+`finalWeightG_`, baru `transitionTo(ABORT)` -- TIDAK memanggil `stopMotorOrAbort()` dari dalamnya (tidak ada rekursi).
- Stall detection: timer mulai dari `motorStartedMs_`, evaluasi pakai `lastFlowAboveThresholdMs_` (flow valid), HANYA dicek saat `WAIT_FLOW_START`/`GRINDING` -- terpisah jelas dari `WAIT_SETTLE`.
- Hard overshoot (`GRIND_HARD_OVERSHOOT_G=2.0f`) terpisah dari accuracy tolerance (`GRIND_ACCURACY_TOLERANCE_G=0.03f`) -- keduanya konstanta berbeda, dipakai di jalur keputusan berbeda (ABORT vs SUCCESS/INACCURATE).
- Pulse correction: durasi di-clamp `GRIND_MIN_PULSE_DURATION_MS`(30)/`GRIND_MAX_PULSE_DURATION_MS`(250), max `GRIND_MAX_PULSE_ATTEMPTS`(10) attempt, ON gagal -> `doAbort`, OFF lewat `stopMotorOrAbort()`, blocking pulse dengan komentar eksplisit "diterima untuk trial awal" (sesuai keputusan), 10 pulse habis + masih di luar toleransi -> `COMPLETE`+`INACCURATE` (bukan ABORT).
- HX711: `calibrationSet_ = (scale != 0.0f)` sebagai safety gate murni, `begin()` dipanggil sebelum `setCalibration()`, tidak ada sisa kode BLE/MyScale AKTIF (referensi BLE yang tersisa semuanya komentar historis/perbandingan arsitektur, legitimate untuk konteks `WeightFilter` yang didesain general-purpose).
- UI architecture: `GrindController` TIDAK PERNAH menyentuh `g_ui_state` (nol referensi, dikonfirmasi grep) -- `main.cpp` satu-satunya jembatan lewat getter, sesuai arsitektur yang disepakati.
- Static consistency: semua field `g_ui_state` yang dibaca UI screens juga ditulis dari suatu tempat (tidak ada "getter UI yang tidak pernah di-update").

**✅ Bug ditemukan & diperbaiki (dead code dari refactor sebelumnya):**
1. **`GRIND_MOTOR_MAX_PULSE_DURATION_MS`** (config.h) -- konstanta duplikat mati, nilainya sama persis (`250.0f`) dengan `GRIND_MAX_PULSE_DURATION_MS` yang benar-benar dipakai kode, tapi TIDAK PERNAH dipakai di mana pun (cuma disebut di komentar). Risiko nyata: kalau salah satu diubah tanpa yang lain, terjadi inkonsistensi diam-diam. **Dihapus.**
2. **`GRIND_MOTOR_SETTLING_TIME_MS`** (config.h) -- konstanta mati, tidak pernah dipakai atau disebut di mana pun. **Dihapus.**
3. **`AbortReason::BLE_DISCONNECTED`** (grind_controller.h) -- enum value sisa dari firmware Tasmota+BLE lama, TIDAK PERNAH dipakai (tidak ada `doAbort(AbortReason::BLE_DISCONNECTED)` di manapun, tidak ada switch statement yang bergantung padanya). Firmware ini sama sekali tidak pakai BLE. **Dihapus** -- diverifikasi ulang lewat compile aktual bahwa penghapusan ini aman (tidak ada switch statement yang butuh exhaustiveness enum ini).

Compile ulang setelah ketiga perubahan ini: **`[SUCCESS]` tetap terjaga** (Flash 70.1%, RAM 43.6%, sama seperti sebelum perubahan -- sesuai dugaan karena semuanya memang dead code).




