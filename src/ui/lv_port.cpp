#include "lv_port.h"
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

// ============================================================
// CATATAN PENTING -- baca lv_port.h dulu untuk detail verifikasi pin.
// ============================================================
//
// Driver dipilih: Arduino_CO5300 (kelas Arduino_GFX untuk display
// controller CO5300 QSPI). CATATAN: source resmi vendor (lihat
// lv_port.h) menunjukkan varian contoh yang memakai esp_lcd_sh8601
// alih-alih esp_lcd_co5300 -- kemungkinan ada variasi chip fisik
// antar revisi/batch board. Kalau compile/tampilan tidak sesuai,
// cek chip yang tertera di PCB board Anda dan ganti ke Arduino_SH8601
// kalau perlu (parameter konstruktor serupa).
//
// Touch: implementasi DI BAWAH pakai polling I2C manual ke register
// map yang SAMA PERSIS dengan getTouch() di source resmi vendor
// (Examples/Arduino-V3.2.0/examples/06_LVGL_Test/FT3168.cpp) --
// alamat I2C 0x38, register 0x02 untuk jumlah titik sentuh, lalu 4
// byte mulai 0x03 untuk X/Y titik pertama (X: 4 bit tinggi byte
// pertama + byte kedua, Y: sama pola untuk byte ketiga+keempat). Ini
// TERVERIFIKASI dari source resmi, BUKAN lagi dugaan dari datasheet
// umum FT5x06 family.
// ============================================================

#define FT6146_I2C_ADDR   0x38   // TERVERIFIKASI dari source resmi (I2C_ADDR_FT3168 di FT3168.cpp) -- sama nilainya walau nama chip di source beda (FT3168 vs FT6146), kemungkinan variasi chip fisik antar batch board, lihat catatan lv_port.h
#define FT6146_REG_TD_STATUS  0x02
#define FT6146_REG_P1_XH      0x03

// ------------------------------------------------------------
// Display bus + panel (Arduino_GFX).
//
// KOREKSI (terverifikasi dari source Arduino_GFX resmi,
// github.com/moononournation/Arduino_GFX, file Arduino_CO5300.cpp):
// signature konstruktor sebenarnya adalah
//   Arduino_CO5300(bus, rst, r, w, h, col_offset1, row_offset1,
//                  col_offset2, row_offset2)
// TIDAK ADA parameter IPS (draft sebelumnya salah menyisipkan
// `false /* IPS */` sebagai parameter ke-4 -- itu bug, dihapus).
//
// OFFSET X +20 (WAJIB, bukan opsional): source resmi vendor
// (lcd_bsp.c, fungsi example_lvgl_flush_cb) menambahkan
// `area->x1 + 0x14` (0x14 = 20) sebelum memanggil
// esp_lcd_panel_draw_bitmap() -- ini kompensasi untuk RAM controller
// CO5300 yang punya lebar fisik lebih besar dari 280px yang terlihat
// (offset umum untuk panel AMOLED byte-boundary). Arduino_GFX
// menerapkan offset yang setara lewat parameter col_offset1 di
// constructor (diverifikasi dari Arduino_TFT::writeAddrWindow() yang
// menambahkan _xStart -- diisi dari col_offset1 -- ke x sebelum kirim
// CASET, dipanggil otomatis oleh SEMUA operasi gambar termasuk
// draw16bitRGBBitmap, bukan sesuatu yang perlu ditambahkan manual di
// disp_flush_cb). Nilai 20 dikonfirmasi juga oleh contoh publik lain
// pemakaian Arduino_CO5300 untuk panel serupa (Arduino_GFX issue #798,
// board CO5300 lain memakai col_offset1=22 -- nilai persis bisa sedikit
// beda antar model panel, TAPI mekanismenya -- lewat constructor,
// bukan flush callback manual -- terverifikasi benar). row_offset1
// tetap 0 (source resmi vendor tidak menambahkan offset Y).
// ------------------------------------------------------------
static Arduino_DataBus* s_bus = new Arduino_ESP32QSPI(
    LCD_PIN_CS, LCD_PIN_SCK, LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3);

static Arduino_GFX* s_gfx = new Arduino_CO5300(
    s_bus, LCD_PIN_RST, 0 /* rotation */,
    LV_PORT_HOR_RES, LV_PORT_VER_RES,
    20 /* col_offset1 -- WAJIB, lihat catatan di atas */,
    0 /* row_offset1 */, 0 /* col_offset2 */, 0 /* row_offset2 */);

// ------------------------------------------------------------
// LVGL draw buffer -- 1/10 layar (cukup untuk performa wajar di
// ESP32-C6, tidak butuh PSRAM). Dialokasikan statis (bukan di stack)
// karena ukurannya besar.
// ------------------------------------------------------------
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[LV_PORT_HOR_RES * (LV_PORT_VER_RES / 10)];
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static uint32_t s_lastTickMs = 0;

// ------------------------------------------------------------
// LVGL flush callback -- kirim area yang di-render LVGL ke panel
// fisik lewat Arduino_GFX. draw_bitmap() Arduino_GFX untuk QSPI
// panel menangani windowing (set_addr_window) secara internal.
// ------------------------------------------------------------
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // color_p MENUNJUK KE AWAL ARRAY w*h piksel untuk area ini (bukan
    // satu piksel) -- LVGL selalu memberi buffer kontigu utuh untuk
    // seluruh area yang di-flush di draw_buf mode LV_DISP_RENDER_MODE
    // default (partial). draw16bitRGBBitmap menerima pointer array
    // uint16_t RGB565, PERSIS representasi lv_color_t saat
    // LV_COLOR_DEPTH=16 (lihat lv_conf.h) -- reinterpret_cast aman di
    // sini karena layout memori lv_color_t 16-bit == uint16_t polos.
    s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)color_p, w, h);

    lv_disp_flush_ready(drv);
}

// ------------------------------------------------------------
// LVGL rounder callback -- WAJIB untuk panel CO5300 board ini.
// DISALIN PERSIS dari source resmi vendor (lcd_bsp.c,
// example_lvgl_rounder_cb()) -- BUG YANG DIPERBAIKI: draft sebelumnya
// TIDAK memasang rounder_cb sama sekali. Controller CO5300 di board
// ini membutuhkan area refresh dengan koordinat/ukuran GENAP (byte-
// boundary requirement khas panel AMOLED QSPI) -- tanpa rounder_cb,
// area redraw yang LVGL kirim (hasil dirty-region tracking internal,
// bisa berupa koordinat ganjil kapan saja tergantung widget/layout apa
// yang berubah) bisa menghasilkan artefak visual: garis/teks hilang,
// sebagian objek tidak ter-refresh, atau tampilan tidak konsisten
// tergantung area yang sedang di-redraw. Ini BUKAN masalah kosmetik
// kecil -- untuk UI dengan angka berat yang harus akurat dibaca
// operator (Predictive Grind/Pulse Correction/Done screen), artefak
// redraw yang salah bisa membuat operator salah baca angka.
//
// Logika: bulatkan x1/y1 KE BAWAH ke kelipatan genap terdekat,
// bulatkan x2/y2 KE ATAS ke ganjil terdekat (x2/y2 inklusif, jadi
// "kelipatan genap + 1" berarti lebar/tinggi area jadi genap juga).
// ------------------------------------------------------------
static void disp_rounder_cb(lv_disp_drv_t* drv, lv_area_t* area) {
    (void)drv;
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

// ------------------------------------------------------------
// PEMULIHAN BUS AGRESIF -- ditambahkan (permintaan eksplisit Wahyu,
// setelah observasi lapangan: sentuhan layar sesekali berhenti
// merespons sepenuhnya sampai perlu restart/cabut power manual,
// terjadi "beberapa kali" dalam pemakaian normal, BUKAN hal baru
// pasca firmware terbaru). Mitigasi endTransmission(true) yang sudah
// ada di ft6146_read_touch() (lihat di bawah) mereset state PER-
// TRANSAKSI, tapi issue #11374 (arduino-esp32, status "Needs
// investigation" per Agustus 2026, BELUM ada fix resmi Espressif)
// menunjukkan sesekali bus I2C bisa masuk state yang TIDAK pulih
// hanya dari endTransmission() saja -- perlu Wire.begin() ulang
// PENUH (dikonfirmasi sebagai mitigasi umum dari komunitas ESP32
// untuk bug I2C serupa, walau disebut sendiri "bandaid fix" bukan
// solusi akar).
//
// TIDAK menjamin 100% menghilangkan masalah (ini bug level driver,
// bukan sesuatu yang bisa firmware kita perbaiki tuntas) -- tujuannya
// membuat sistem PULIH SENDIRI dari kegagalan beruntun tanpa perlu
// restart manual, bukan mencegah kegagalan itu sendiri terjadi.
static uint8_t s_touchConsecutiveFailures = 0;
#define TOUCH_I2C_RECOVERY_THRESHOLD 20  // ~20 polling gagal berturut-turut (di laju polling LVGL biasa beberapa puluh Hz, ini setara kurang dari 1 detik) sebelum Wire.begin() ulang dicoba -- cukup tinggi untuk tidak overreact ke 1-2 kegagalan sesaat yang wajar/self-recovering, cukup rendah untuk tidak biarkan operator menunggu lama saat benar-benar macet.

// Counter total touch_i2c_hard_recover() terpanggil sejak boot --
// BARU, ditambahkan untuk diagnosis laporan "layar tiba-tiba lompat
// ke Set Target saat GRINDING, sesekali/random, kedipan lebih cepat
// dari reboot". Kalau counter ini naik BERBARENGAN dengan kejadian
// tsb (dicek lewat Debug screen sesudahnya), itu mengarah ke phantom
// touch/noise I2C (kemungkinan dipicu motor menyala) sebagai akar
// masalah, BUKAN reboot -- lihat catatan lengkap di debug_snapshot.h.
// RAM-only (bukan NVS) -- cukup untuk diagnosis satu sesi pemakaian,
// tidak perlu bertahan lintas reboot.
static unsigned long s_touchRecoveryCount = 0;

static void touch_i2c_hard_recover(void) {
    Wire.begin(TOUCH_PIN_SDA, TOUCH_PIN_SCL);  // parameter SAMA PERSIS dengan inisialisasi awal di lv_port_init()
    s_touchConsecutiveFailures = 0;
    s_touchRecoveryCount++;
}

unsigned long lv_port_touch_recovery_count(void) {
    return s_touchRecoveryCount;
}

// ------------------------------------------------------------
// Baca register FT6146 lewat I2C -- helper kecil, blocking (durasi
// I2C read singkat, tidak masalah dipanggil tiap poll LVGL indev).
// Register map + clamp koordinat DISALIN PERSIS dari source resmi
// vendor (FT3168.cpp: getTouch()) -- lihat catatan clamp di bawah.
// ------------------------------------------------------------
static bool ft6146_read_touch(uint16_t* outX, uint16_t* outY) {
    Wire.beginTransmission(FT6146_I2C_ADDR);
    Wire.write(FT6146_REG_TD_STATUS);
    if (Wire.endTransmission(false) != 0) {
        // PEMULIHAN BUS -- ditambahkan untuk mitigasi bug dikenal di
        // driver I2C "ng" (new generation) Arduino-ESP32 core untuk
        // ESP32-C6 (lihat espressif/arduino-esp32#11374: transaksi
        // repeated-start/nonstop sesekali gagal dengan
        // ESP_ERR_INVALID_STATE tanpa sebab wiring/hardware). Tanpa
        // reset eksplisit di sini, satu transaksi gagal bisa membuat
        // driver Wire menyisakan state internal yang korup, sehingga
        // transaksi BERIKUTNYA ikut gagal terus-menerus (observasi:
        // error muncul berulang sejak boot, bukan sesekali acak) --
        // ini yang diduga membuat sentuhan/klik LVGL tidak pernah
        // ter-commit bersih walau state PRESSED sempat terdeteksi.
        // endTransmission() tanpa argumen (default true/sendStop)
        // memaksa bus kembali ke kondisi idle sebelum poll berikutnya.
        Wire.endTransmission(true);
        // HITUNG kegagalan KOMUNIKASI I2C (BUKAN "tidak ada sentuhan"
        // -- lihat catatan di touchCount==0 di bawah, itu keberhasilan
        // komunikasi, bukan kegagalan) -- kalau beruntun melebihi
        // threshold, coba pemulihan lebih agresif (lihat
        // touch_i2c_hard_recover() di atas).
        s_touchConsecutiveFailures++;
        if (s_touchConsecutiveFailures >= TOUCH_I2C_RECOVERY_THRESHOLD) {
            touch_i2c_hard_recover();
        }
        return false;
    }

    if (Wire.requestFrom((int)FT6146_I2C_ADDR, 5) != 5) {
        Wire.endTransmission(true);  // sama alasan seperti di atas -- pulihkan bus sebelum poll berikutnya
        s_touchConsecutiveFailures++;
        if (s_touchConsecutiveFailures >= TOUCH_I2C_RECOVERY_THRESHOLD) {
            touch_i2c_hard_recover();
        }
        return false;
    }

    // Komunikasi I2C BERHASIL sampai titik ini -- reset counter
    // kegagalan (bus dalam kondisi sehat), TERLEPAS dari apakah ada
    // titik sentuh terdeteksi atau tidak di bawah ini.
    s_touchConsecutiveFailures = 0;

    uint8_t touchCount = Wire.read() & 0x0F;
    if (touchCount == 0) {
        // Baca sisa buffer supaya tidak mengotori transaksi berikutnya,
        // walau tidak ada titik sentuh valid. INI BUKAN KEGAGALAN --
        // komunikasi I2C sukses, cuma memang tidak ada sentuhan saat
        // ini (kondisi NORMAL yang terjadi terus-menerus saat layar
        // idle) -- TIDAK menambah s_touchConsecutiveFailures.
        for (int i = 0; i < 4; i++) Wire.read();
        return false;
    }

    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();

    *outX = ((xh & 0x0F) << 8) | xl;
    *outY = ((yh & 0x0F) << 8) | yl;

    // Clamp koordinat ke batas resolusi panel -- DISALIN dari source
    // resmi vendor (FT3168.cpp getTouch(): "if(*x > EXAMPLE_LCD_H_RES)
    // *x = EXAMPLE_LCD_H_RES; if(*y > EXAMPLE_LCD_V_RES) *y =
    // EXAMPLE_LCD_V_RES;"). Sebelumnya TIDAK ADA di implementasi ini --
    // tanpa clamp, noise/glitch controller touch bisa menghasilkan
    // koordinat X/Y yang sedikit melebihi batas panel (mengingat field
    // X/Y hanya 12-bit dari register, bukan dibatasi hardware ke
    // resolusi 280x456 spesifik), yang bisa membuat LVGL indev
    // menerima titik di luar area widget/screen.
    if (*outX > LV_PORT_HOR_RES) *outX = LV_PORT_HOR_RES;
    if (*outY > LV_PORT_VER_RES) *outY = LV_PORT_VER_RES;

    return true;
}

// ------------------------------------------------------------
// LVGL indev (touch) read callback.
// ------------------------------------------------------------
static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    uint16_t x = 0, y = 0;
    if (ft6146_read_touch(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lv_port_init(void) {
    // --- Display fisik dulu ---
    s_gfx->begin();
    // BUG DITEMUKAN & DIPERBAIKI LEWAT COMPILE AKTUAL: `BLACK` bukan
    // konstanta yang didefinisikan generik oleh Arduino_GFX (itu nama
    // konstanta khas library Adafruit_GFX/TFT lama) -- Arduino_GFX
    // punya namespace warna sendiri, RGB565_BLACK (lihat
    // Arduino_GFX.h: "#define RGB565_BLACK RGB565(0, 0, 0)").
    s_gfx->fillScreen(RGB565_BLACK);

    // --- Touch I2C ---
    Wire.begin(TOUCH_PIN_SDA, TOUCH_PIN_SCL);
    if (TOUCH_PIN_RST >= 0) {
        pinMode(TOUCH_PIN_RST, OUTPUT);
        digitalWrite(TOUCH_PIN_RST, LOW);
        delay(10);
        digitalWrite(TOUCH_PIN_RST, HIGH);
        delay(50);  // waktu boot chip touch setelah reset -- nilai umum, longgar sengaja
    }

    // Kembalikan controller touch ke normal mode -- DISALIN dari
    // source resmi vendor (FT3168.cpp Touch_Init():
    // "I2C_writr_buff(I2C_ADDR_FT3168, 0x00, &data, 1);" dengan
    // data=0x00). Sebelumnya TIDAK dipanggil di lv_port_init() ini --
    // mungkin tetap bekerja kalau controller sudah default di normal
    // mode saat power-on, TAPI mengikuti initialization resmi lebih
    // aman untuk memastikan touch selalu siap tanpa bergantung pada
    // state default yang tidak terjamin di semua kondisi (mis. setelah
    // soft-reset ESP32 tanpa power-cycle penuh pada chip touch).
    {
        uint8_t normalModeCmd = 0x00;
        Wire.beginTransmission(FT6146_I2C_ADDR);
        Wire.write((uint8_t)0x00);       // register 0x00 (device mode)
        Wire.write(normalModeCmd);        // 0x00 = normal mode
        Wire.endTransmission();
    }

    // --- LVGL core ---
    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr, LV_PORT_HOR_RES * (LV_PORT_VER_RES / 10));

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LV_PORT_HOR_RES;
    s_disp_drv.ver_res = LV_PORT_VER_RES;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.rounder_cb = disp_rounder_cb;  // WAJIB untuk CO5300 -- lihat komentar disp_rounder_cb() di atas
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    s_lastTickMs = millis();
}

void lv_port_tick(void) {
    uint32_t now = millis();
    uint32_t delta = now - s_lastTickMs;  // unsigned, aman terhadap millis() overflow/wraparound
    if (delta > 0) {
        lv_tick_inc(delta);
        s_lastTickMs = now;
    }
    lv_timer_handler();
}
