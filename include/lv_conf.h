/**
 * lv_conf.h -- konfigurasi LVGL v8 untuk firmware GBW.
 *
 * SENGAJA DIBUAT MINIMAL (bukan menyalin lv_conf_template.h penuh) --
 * hanya fitur yang benar-benar dipakai src/ui/*.cpp yang diaktifkan:
 * lv_arc, lv_label, lv_btn, lv_obj (flex layout), font Montserrat
 * 12/14/32, dan simbol LV_SYMBOL_* (WIFI/BLUETOOTH/SETTINGS/PLUS/
 * MINUS -- lihat ui_common.h & screen_*.cpp).
 *
 * KALAU compile gagal dengan pesan symbol/font "undefined reference"
 * atau widget tidak dikenali, cek dulu apakah ada widget/fitur baru
 * yang dipakai di src/ui/ tapi belum diaktifkan di sini -- JANGAN
 * langsung set semua LV_USE_* ke 1 (itu menghapus tujuan file minimal
 * ini), aktifkan satu-satu sesuai kebutuhan nyata.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0   /* CO5300 QSPI panel -- ubah ke 1 kalau warna terbalik/RGB salah saat uji fisik pertama */

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)   /* 48KB heap internal LVGL -- cukup untuk 6 screen sederhana, longgarkan kalau habis */

/*====================
   HAL SETTINGS
 *====================*/
#define LV_TICK_CUSTOM 0   /* lv_tick_inc() dipanggil manual dari lv_port.cpp, bukan lewat tick custom LVGL */
#define LV_DPI_DEF 130

/*====================
   FEATURE CONFIGURATION
 *====================*/
#define LV_USE_ARC        1   /* dipakai ring progress di 4 screen (Idle/Predictive/Pulse/Done) */
#define LV_USE_BTN        1   /* semua tombol (Start/Stop/Confirm/Save/preset/stepper) */
#define LV_USE_LABEL      1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_USE_FLEX       1   /* dipakai preset row & settings param row (LV_FLEX_FLOW_ROW) */

/* Widget lain yang TIDAK dipakai UI ini -- dimatikan sengaja supaya
   footprint kecil (screen_*.cpp hanya pakai lv_obj/lv_arc/lv_label/
   lv_btn, tidak ada slider/chart/table/dll).
   BUG YANG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL PERTAMA KALI:
   HAMPIR SEMUA widget di LVGL v8 (termasuk yang di folder extra/,
   seperti lv_calendar, lv_keyboard, lv_msgbox, lv_spinbox, dst)
   DEFAULT AKTIF (=1) kalau TIDAK disebutkan sama sekali di file ini
   (lihat lv_conf_internal.h -- pola "#ifndef LV_USE_X ... #define
   LV_USE_X 1"). Draft sebelumnya HANYA menyebutkan sebagian kecil
   widget yang benar-benar dipakai screen_*.cpp, dan MENGASUMSIKAN
   sisanya otomatis mati -- itu SALAH, sisanya justru diam-diam AKTIF,
   lalu banyak yang saling bergantung (mis. lv_calendar butuh
   LV_USE_BTNMATRIX, lv_keyboard butuh LV_USE_BTNMATRIX+LV_USE_TEXTAREA)
   sehingga memicu cascading #error/compile error begitu salah satu
   dependency dimatikan. INI TIDAK KELIHATAN DARI REVIEW STATIS
   MANAPUN -- baru ketahuan lewat compile aktual pertama kali.
   Perbaikan: SEMUA widget yang tidak dipakai (bukan cuma yang
   "kelihatan" jadi masalah) SEKARANG dimatikan eksplisit, disalin
   dari daftar lengkap resmi lv_conf_template.h (LVGL v8.4.0) supaya
   tidak ada satu pun widget default yang diam-diam aktif tanpa
   sepengetahuan project ini. */
#define LV_USE_ANIMIMG      0
#define LV_USE_BAR          0
#define LV_USE_BTNMATRIX    0
#define LV_USE_CALENDAR     0   /* BARU -- default aktif, butuh BTNMATRIX */
#define LV_USE_CANVAS       0
#define LV_USE_CHART        0   /* BARU -- default aktif */
#define LV_USE_CHECKBOX     0
#define LV_USE_COLORWHEEL   0   /* BARU -- default aktif */
#define LV_USE_DROPDOWN     0
#define LV_USE_IMG          0
#define LV_USE_IMGBTN       0   /* BARU -- default aktif */
#define LV_USE_KEYBOARD     0   /* default aktif, butuh BTNMATRIX+TEXTAREA */
#define LV_USE_LED          0   /* BARU -- default aktif */
#define LV_USE_LINE         0
#define LV_USE_LIST         0   /* BARU -- default aktif */
#define LV_USE_MENU         0   /* BARU -- default aktif */
#define LV_USE_METER        0   /* BARU -- default aktif */
#define LV_USE_MSGBOX       0   /* default aktif, butuh BTNMATRIX */
#define LV_USE_ROLLER       0
#define LV_USE_SLIDER       0
#define LV_USE_SPAN         0   /* BARU -- default aktif */
#define LV_USE_SPINBOX      0   /* default aktif, butuh TEXTAREA */
#define LV_USE_SPINNER      0   /* BARU -- default aktif */
#define LV_USE_SWITCH       0
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0   /* BARU -- default aktif */
#define LV_USE_TEXTAREA     0
#define LV_USE_TILEVIEW     0   /* BARU -- default aktif */
#define LV_USE_WIN          0   /* BARU -- default aktif */

/* Fitur non-widget yang juga default aktif di template resmi tapi
   tidak dipakai project ini -- dimatikan eksplisit dengan alasan
   sama (hindari dependency tak terduga & kurangi footprint flash). */
#define LV_USE_THEME_BASIC  0
#define LV_USE_THEME_MONO   0
#define LV_USE_GRID         0   /* project ini hanya pakai LV_FLEX_FLOW_ROW, bukan grid layout */
#define LV_USE_PNG          0
#define LV_USE_BMP          0
#define LV_USE_SJPG         0
#define LV_USE_GIF          0
#define LV_USE_QRCODE       0
#define LV_USE_FREETYPE     0
#define LV_USE_TINY_TTF     0
#define LV_USE_RLOTTIE      0
#define LV_USE_FFMPEG       0
#define LV_USE_SNAPSHOT     0
#define LV_USE_MONKEY       0
#define LV_USE_GRIDNAV      0
#define LV_USE_FRAGMENT     0
#define LV_USE_IMGFONT      0
#define LV_USE_MSG          0
#define LV_USE_IME_PINYIN   0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS  0
#define LV_USE_DEMO_MUSIC   0

/*====================
   FONT USAGE
 *====================*/
/* Dipakai screen_*.cpp: montserrat_12 (label kecil/pill/stat), _14
   (nama param settings/preset button), _16 (judul kecil DIPERBESAR --
   TARGET WEIGHT/QUICK PRESETS/dst, permintaan eksplisit supaya
   proporsional dengan angka besar 40px), _32 (dipertahankan untuk
   kompatibilitas/tempat lain yang belum diaudit), _40 (angka berat
   besar DIPERBESAR dari 32, permintaan eksplisit -- lihat
   ui_create_weight_label() di ui_common.h). */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   TEXT SETTINGS
 *====================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8   /* label pakai UTF-8 literal (em dash, panah, checkmark -- lihat screen_idle.cpp dkk) */

/*====================
   WIDGETS EXTRA
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1      /* palet warna UI ini gelap (COLOR_BG dkk di ui_common.h) -- theme dasar ikut gelap supaya konsisten */
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/*====================
   LOG SETTINGS
 *====================*/
#define LV_USE_LOG 0

/*====================
   COMPILER SETTINGS
 *====================*/
#define LV_ATTRIBUTE_TICK_INC
#define LV_ATTRIBUTE_TIMER_HANDLER
#define LV_ATTRIBUTE_FLUSH_READY
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 1
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FAST_MEM
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning

#endif /* LV_CONF_H */
