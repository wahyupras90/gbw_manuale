# Referensi source resmi -- ESP32-C6-Touch-AMOLED-1.64

File-file di folder ini disalin **verbatim** (tidak diubah) dari clone
`https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.64`, diambil
2026-08-22, untuk jadi sumber kebenaran nomor pin QSPI/I2C touch yang
dipakai `src/ui/lv_port.h` (lihat komentar di file itu untuk detail
lengkap & tabel pin).

Disimpan di sini supaya kalau perlu verifikasi ulang atau ada
pembaruan dari vendor, tidak perlu clone repo lagi dari nol -- cukup
bandingkan file di sini dengan versi terbaru di repo asli.

## Isi

- `lcd_config.h` -- nomor pin QSPI display + I2C touch (Arduino
  example, v3.2.0). **INI SUMBER UTAMA untuk pin di lv_port.h.**
- `lcd_bsp.c` / `lcd_bsp.h` -- init sequence LVGL + panel SH8601 QSPI
  (contoh Arduino resmi).
- `FT3168.cpp` / `FT3168.h` -- driver touch I2C manual (register map
  yang dipakai persis sebagai referensi implementasi
  `ft6146_read_touch()` di `src/ui/lv_port.cpp`).
- `ESP32-C6-Touch-AMOLED-1.64.h` -- header BSP resmi ESP-IDF
  (Espressif-style), sumber KEDUA yang independen mengonfirmasi angka
  pin yang sama persis dengan `lcd_config.h`. Juga referensi nomor pin
  SD card (`BSP_SD_*`) yang dipakai untuk analisis bentrok pin.
- `sd_card_bsp.cpp` -- konfirmasi independen nomor pin SD card dari
  contoh Arduino (cocok dengan `BSP_SD_*` di file BSP ESP-IDF).

## Temuan kunci dari file-file ini

Lihat tabel pin lengkap & analisis bentrok di komentar header
`src/ui/lv_port.h` -- ringkasannya:

- QSPI display: CS=GPIO10, PCLK=GPIO11, DATA0=GPIO4, DATA1=GPIO5,
  DATA2=GPIO7, DATA3=GPIO19, RST=GPIO20.
- I2C touch (dipakai bersama IMU): SDA=GPIO18, SCL=GPIO8,
  Touch INT=GPIO1.
- SD card: CS=GPIO15, SCK=GPIO11 (=LCD PCLK), MOSI=GPIO4 (=LCD DATA0),
  MISO=GPIO5 (=LCD DATA1) -- SD card & display sengaja berbagi bus
  SPI2 fisik yang sama menurut desain vendor.
- **GPIO4 dan GPIO5 dipakai QSPI display secara permanen** -- ini
  bentrok langsung dengan `MOTOR_GPIO_PIN=4` dan `HX711_DOUT_PIN=5` di
  `config.h` GBW. Perlu keputusan eksplisit user untuk memindahkan pin
  motor/HX711 sebelum UI LVGL dipakai bersamaan dengan kontrol
  motor/HX711 di hardware fisik.
