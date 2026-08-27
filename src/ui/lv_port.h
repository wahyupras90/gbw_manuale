#pragma once

// ============================================================
// lv_port -- inisialisasi LVGL + driver display/touch untuk board
// Waveshare ESP32-C6-Touch-AMOLED-1.64 (280x456, QSPI).
// ============================================================
// STATUS PIN: TERVERIFIKASI LANGSUNG dari clone repo resmi
// github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.64 (BUKAN LAGI
// dugaan/placeholder dari foto pinout seperti draft sebelumnya).
// Dua sumber independen dalam repo yang sama SEPAKAT pada angka pin
// yang identik:
//   1) Examples/Arduino-V3.2.0/examples/06_LVGL_Test/lcd_config.h
//   2) Examples/ESP-IDF-V5.5.2/06_LVGL_Demo/components/
//      ESP32-C6-Touch-AMOLED-1.64/include/bsp/ESP32-C6-Touch-AMOLED-1.64.h
//      (BSP resmi Espressif-style, dependen ke espressif/esp_lcd_co5300
//      + espressif/esp_lcd_touch_ft3168)
//
// CATATAN CHIP DRIVER: contoh Arduino (lebih lama, v3.2.0) memakai
// esp_lcd_sh8601 + custom FT3168 I2C register access. BSP ESP-IDF
// terbaru memakai esp_lcd_co5300 + esp_lcd_touch_ft3168 (komponen
// resmi Espressif). Kemungkinan ada varian chip fisik antar batch
// produksi board (SH8601 vs CO5300) -- TAPI NOMOR PIN GPIO IDENTIK
// di kedua sumber, itu yang menentukan wiring lv_port ini. Kelas
// driver Arduino_GFX yang dipakai di lv_port.cpp (Arduino_CO5300)
// SEBAIKNYA dicocokkan ke chip yang tertera di PCB fisik board Anda
// kalau memungkinkan (biasanya tercetak kecil di dekat konektor
// display) -- kalau ternyata SH8601, ganti ke kelas Arduino_SH8601
// (API konstruktor serupa, parameter sama).
//
// RIWAYAT BENTROK PIN -- DIKONFIRMASI DARI SOURCE RESMI, SUDAH
// DIPERBAIKI: LCD_DATA0=GPIO4 dan LCD_DATA1=GPIO5 SEBELUMNYA sama
// persis dengan MOTOR_GPIO_PIN/HX711_DOUT_PIN default lama di
// config.h. Source resmi JUGA mengonfirmasi SD card (SD_MOSI=GPIO4,
// SD_MISO=GPIO5, SD_CLK=GPIO11) BERBAGI bus fisik yang SAMA dengan
// QSPI display (LCD_PCLK=GPIO11 juga) -- keputusan desain vendor yang
// eksplisit, bukan kebetulan.
//
// GPIO4/GPIO5 SELALU dipakai QSPI display begitu lv_port_init()
// dipanggil -- TERLEPAS SD card dipakai atau tidak, KARENA QSPI ke
// panel AMOLED adalah jalur fisik TETAP yang tidak bisa dipindah ke
// GPIO lain.
//
// SUDAH DIPERBAIKI (lihat config.h): MOTOR_GPIO_PIN dipindah ke
// GPIO6, HX711_DOUT_PIN ke GPIO2, HX711_SCK_PIN ke GPIO3. Versi
// SEBELUMNYA sempat memakai GPIO12 untuk HX711_SCK (dipilih murni
// demi simetri kabel kiri/kanan) -- KOREKSI PENTING: GPIO12 (bersama
// GPIO13) adalah pin default USB Serial/JTAG Controller di ESP32-C6
// (terverifikasi dari ESP-IDF Programming Guide & datasheet resmi
// Espressif). Firmware ini memakai `-DARDUINO_USB_MODE=1` +
// `-DARDUINO_USB_CDC_ON_BOOT=1` (platformio.ini) -- Serial monitor
// project ini BERGANTUNG pada USB Serial/JTAG Controller yang sama.
// GPIO12 SEKARANG DIGANTI ke GPIO3 (bukan strapping pin -- strapping
// pin resmi ESP32-C6 hanya GPIO4/5/8/9/15 -- dan bukan USB-JTAG).
// Konsekuensinya susunan kabel jadi 3 kabel kolom kiri (GPIO2+GPIO3+
// GPIO6), tidak lagi seimbang kiri/kanan seperti rencana awal -- ini
// trade-off yang diterima karena kebenaran fungsi USB Serial/JTAG
// lebih penting daripada simetri kabel murni.
//
// GPIO YANG SUDAH TERPAKAI board (JANGAN dipakai ulang untuk periferal
// eksternal lain):
//   GPIO0  = BOOT button (strapping)
//   GPIO1  = LCD Touch INT
//   GPIO2  = HX711 DOUT (config.h)
//   GPIO3  = HX711 SCK (config.h)
//   GPIO4  = LCD DATA0 / SD MOSI (shared bus) -- JUGA strapping pin
//   GPIO5  = LCD DATA1 / SD MISO (shared bus) -- JUGA strapping pin
//   GPIO6  = Motor GPIO (config.h)
//   GPIO7  = LCD DATA2
//   GPIO8  = I2C SCL (touch + IMU shared bus) -- JUGA strapping pin
//   GPIO9  = BOOT/strapping (boot mode select)
//   GPIO10 = LCD CS
//   GPIO11 = LCD PCLK / SD SCK (shared bus)
//   GPIO12 = USB-JTAG D- (default) -- JANGAN dipakai GPIO biasa kalau
//            Serial/USB CDC dipakai (lihat platformio.ini)
//   GPIO13 = USB-JTAG D+ (default) -- sama seperti GPIO12
//   GPIO15 = SD CS -- JUGA strapping pin
//   GPIO18 = I2C SDA (touch + IMU shared bus)
//   GPIO19 = LCD DATA3
//   GPIO21 = LCD RST (KOREKSI dari GPIO20, lihat catatan di LCD_PIN_RST di bawah)
//
// GPIO YANG MASIH BEBAS (untuk periferal tambahan di masa depan, mis.
// BLE companion app/sensor lain -- TIDAK strapping, TIDAK USB-JTAG,
// TIDAK dipakai display/touch/IMU/SD/BOOT): GPIO16, GPIO17, GPIO20,
// GPIO22, GPIO23.
//
// Library yang dipakai (lihat platformio.ini lib_deps):
//   - lvgl (v8.4.0, SESUAI versi yang dipakai contoh resmi Waveshare
//     -- JANGAN pakai v9, ada breaking API changes)
//   - Arduino_GFX_Library (moononournation) -- dipakai untuk kelas
//     driver Arduino_CO5300/Arduino_SH8601 tergantung chip fisik
//     board Anda (lihat catatan di atas)
// ============================================================

#include <lvgl.h>

// --- Resolusi panel (SUDAH DIKONFIRMASI dari spek resmi Waveshare
// DAN dari source code resmi EXAMPLE_LCD_H_RES/V_RES, lihat
// ui_common.h SCREEN_WIDTH/SCREEN_HEIGHT) ---
#define LV_PORT_HOR_RES   280
#define LV_PORT_VER_RES   456

// ============================================================
// Definisi pin QSPI display + I2C touch (lihat status verifikasi &
// referensi source resmi di komentar header file ini).
// ============================================================
#define LCD_PIN_CS    10
#define LCD_PIN_SCK   11
#define LCD_PIN_D0    4
#define LCD_PIN_D1    5
#define LCD_PIN_D2    7
#define LCD_PIN_D3    19
#define LCD_PIN_RST   21   // KOREKSI: contoh Arduino resmi (lcd_config.h, EXAMPLE_PIN_NUM_LCD_RST) pakai GPIO21 -- BSP ESP-IDF (ESP32-C6-Touch-AMOLED-1.64.h, BSP_LCD_RST) pakai GPIO20, sumber resmi vendor sendiri TIDAK KONSISTEN antar dua contoh. Firmware ini pakai Arduino_GFX (Arduino-style), jadi acuan yang benar adalah contoh Arduino -> GPIO21. Draft sebelumnya salah kutip GPIO20 dari BSP ESP-IDF.
#define LCD_PIN_TE    -1   // tidak dipakai di source resmi (tidak ada define BSP_LCD_TE)

// --- PIN I2C TOUCH -- TERVERIFIKASI, bus dipakai BERSAMA dengan IMU
// QMI8658 (dikonfirmasi eksplisit di source resmi: BSP_I2C_SCL/SDA
// dipakai satu bus untuk "LCD Touch controller" + IMU + IO Expander +
// Codec, lihat komentar asli di ESP32-C6-Touch-AMOLED-1.64.h). ---
#define TOUCH_PIN_SDA   18
#define TOUCH_PIN_SCL   8
#define TOUCH_PIN_INT   1    // TERVERIFIKASI (BSP_LCD_TOUCH_INT) -- beda dari dugaan sebelumnya (-1), lv_port.cpp saat ini masih polling murni (tidak pakai interrupt), boleh dioptimasi nanti pakai GPIO ini sebagai interrupt kalau perlu
#define TOUCH_PIN_RST   -1   // TERVERIFIKASI (BSP_LCD_TOUCH_RST = GPIO_NUM_NC, artinya memang tidak ada reset pin terpisah untuk touch)

// ============================================================
// API publik lv_port -- dipanggil dari main.cpp / ui_screen_manager.cpp
// ============================================================

// Inisialisasi display driver (QSPI CO5300) + touch driver (I2C
// FT6146) + LVGL core (lv_init, display buffer, disp_drv, indev_drv).
// Panggil SEKALI dari setup(), SEBELUM ui_init() (ui_init() langsung
// bikin screen LVGL pertama, jadi LVGL core harus sudah siap).
void lv_port_init(void);

// Panggil TIAP loop() -- membungkus lv_timer_handler() (proses render
// LVGL + animasi + input) DAN lv_tick_inc() (LVGL butuh tahu berapa ms
// berlalu sejak panggilan terakhir untuk animasi/timer internal --
// diimplementasikan pakai millis() delta, lihat lv_port.cpp).
void lv_port_tick(void);
