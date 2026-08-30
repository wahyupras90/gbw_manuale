# WIRING.md -- Diagram Wiring Hardware Final (GPIO + HX711)

## ⚠️ PERINGATAN KESELAMATAN -- BACA SEBELUM MENYAMBUNG APA PUN

**Cabut steker grinder dari listrik SEBELUM membuka casing atau menyentuh kabel apa pun di dalamnya.** Semua langkah verifikasi di bawah dilakukan dalam kondisi grinder **TIDAK** terhubung ke listrik.

Eureka Mignon **Manuale** dikontrol lewat **microswitch fisik 2-kabel** yang menyalakan motor langsung dari AC mains saat ditekan (BUKAN konektor sinyal 4-pin low-voltage seperti Eureka Specialita di referensi upstream `jaapp/smart-grind-by-weight` -- **project ini TIDAK memakai topologi itu**, jangan disamakan).

**Topologi yang dipakai project ini**: relay module dipasang **PARALEL dengan microswitch fisik** (bukan menggantikan) -- dua kabel yang keluar dari microswitch tetap utuh tersambung seperti aslinya, dan **kontak relay (dry contact) disambung paralel** ke dua titik yang sama. Relay menutup kontaknya untuk "menekan microswitch secara elektrik" tanpa harus menekan tombol fisik. Microswitch asli tetap berfungsi sebagai fallback manual kalau ESP32 mati/gagal.

**KONSEKUENSI KESELAMATAN PENTING**: karena relay dipasang paralel dengan microswitch yang mengalirkan AC mains langsung ke motor, **kedua kabel di sisi kontak relay HARUS dianggap AC MAINS bertegangan penuh** (bukan sinyal logic 3.3V/5V seperti pin 3 konektor Specialita) -- **sampai terbukti sebaliknya lewat verifikasi fisik**. Ini beda fundamental dari asumsi "GPIO langsung ke pin sinyal" yang berlaku untuk topologi Specialita upstream.

### Langkah verifikasi WAJIB sebelum menyambung relay ke microswitch

1. Cabut steker grinder.
2. Buka casing, temukan microswitch fisik yang dipakai untuk menyalakan motor grinding (biasanya di dekat corong/tuas manual).
3. Lepas salah satu kabel microswitch, ukur dengan multimeter (mode continuity/resistansi, grinder MATI total dari listrik) antara kedua titik sambungan microswitch -- pastikan benar itu jalur yang mengalirkan daya ke motor saat microswitch ditekan (bukan jalur sinyal low-voltage ke PCB kontrol terpisah).
4. **Asumsikan kedua titik itu adalah AC mains** sampai diverifikasi sebaliknya. JANGAN sambung relay/apa pun sebelum yakin.
5. Kalau ragu sama sekali soal level tegangan atau topologi motor unit Manuale kamu, konsultasikan dengan teknisi listrik/orang yang paham sebelum lanjut -- ini bagian yang paling berisiko di seluruh project.

Warna kabel bisa berbeda antar unit -- jangan percaya warna kabel, verifikasi posisi secara fisik/dengan multimeter.

---

## Diagram wiring -- Motor (relay paralel dengan microswitch)

**MODUL RELAY YANG DIPAKAI -- CONFIRMED dari foto unit fisik (bukan lagi dugaan dari listing toko).** Part tercetak di badan relay: **`JQC3F-03VDC-C`**. Board fisik punya **3-pin terminal kontrol kanan** (`GND`/`IN`/`VCC`), **3-pin terminal kontak kiri** (`NC`/`COM`/`NO`), dan **4 pad kecil di tengah PCB** dekat chip optocoupler berlabel `V-`/`GND`/`V+`/`VCC` -- INI istilah yang dipakai modul fisik ini untuk rail suplai coil terpisah, BUKAN `JD-VCC` seperti istilah generik yang dipakai draft sebelumnya. Fungsinya sama (rail suplai coil terpisah dari rail kontrol optocoupler), cuma labelnya beda.

**Yang PASTI dari deskripsi produk + foto:** (1) **active-HIGH** ("triggered by high level signal, which can be input from microcontroller IO"), (2) **optocoupler isolation** (menjaga seluruh sisi kontrol -- `IN`/`GND`/`VCC`/`V+`/`V-`, TERMASUK coil relay -- galvanically terpisah dari sisi kontak `COM`/`NO`/`NC` yang membawa AC mains), (3) **coil voltage 3V/3.3V**, (4) kontak rated 10A 250VAC / 10A 30VDC.

**Arus coil persis TIDAK bisa dipastikan dari nama part saja** -- estimasi ~120mA berdasar pola industri relay form-factor sejenis, **WAJIB diverifikasi dengan multimeter** untuk unit fisik yang dipegang (lihat langkah di bawah).

### Langkah verifikasi arus coil relay (WAJIB sebelum menganggap angka arus manapun benar)

Catatan penting sebelum mulai: pengukuran ini harus dilakukan di jalur **`V+`** (suplai coil, lihat penjelasan jumper di bawah), BUKAN di jalur `VCC` (suplai optocoupler/LED, arus kecil ~5mA yang TIDAK merepresentasikan arus coil sama sekali).

1. Lepas dulu jumper `VCC`↔`V+` (lihat penjelasan lengkap di bawah) supaya `VCC` dan `V+` benar-benar dua jalur terpisah yang bisa diukur sendiri-sendiri. Jumper `GND`↔`V-` **TIDAK perlu dilepas** -- boleh dibiarkan tersambung (lihat penjelasan di bawah).
2. Sambungkan relay: `IN`->`VCC` dijumper langsung sesaat (energized manual, TANPA GPIO ESP32 dulu) supaya optocoupler aktif dan relay energized. `V+`/`V-` disambung ke sumber 3V terpisah (step-down 5V->3V yang sudah dibeli). **JANGAN sambungkan `NC`/`COM`/`NO` ke apa pun dulu**.
3. Set multimeter ke mode pengukuran ARUS DC (biasanya port terpisah "10A" atau "mA" di multimeter, BUKAN port voltage) -- putuskan salah satu kabel di jalur `V+` (antara step-down dan relay), sambungkan multimeter IN-LINE (seri) di titik potong itu.
4. Baca angka arus yang muncul di multimeter SAAT relay energized (klik menyala). Ini angka arus coil SEBENARNYA untuk unit relay fisik yang kamu pegang.
5. Kalau angka terbaca mendekati ~120mA -- aman, lanjut wiring seperti biasa. Kalau angka jauh berbeda (lebih besar, mis. >200mA) -- perlu evaluasi ulang kapasitas modul step-down 3V yang dibeli.
6. **Setelah arus terkonfirmasi lewat pengukuran**, baru lanjut sambungkan `IN` ke GPIO6 ESP32 dan lakukan langkah test active-HIGH/LOW seperti di bawah.

Terminal kontrol di sisi kanan (3-pin, screw terminal blok): `GND`, `IN`, `VCC` (sinyal kontrol). **Pad `V-`/`GND`/`V+`/`VCC` adalah 4 titik solder TERPISAH di tengah PCB** (dekat chip optocoupler, BUKAN bagian dari terminal kontrol screw yang sama) -- `V-`/`GND` dan `V+`/`VCC` masing-masing DIJUMPER (disatukan) dari pabrik secara default. Terminal kontak di sisi kiri (3-pin, screw terminal blok): `NC`/`COM`/`NO`.

**KEPUTUSAN FINAL (per diskusi): `V+` (suplai coil) TIDAK disuplai dari pin 3.3V board ESP32-C6-Touch-AMOLED-1.64.** Sebagai gantinya, dipakai **modul step-down 5V→3V terpisah**, diberi daya dari buck converter 5V yang sudah ada di project ini. `VCC` (suplai optocoupler, arus kecil) TETAP boleh dari pin 3.3V ESP32 on-board -- ini AMAN karena arusnya kecil (~5mA), TIDAK sama dengan arus coil yang lewat `V+`.

### Fungsi jumper `VCC`↔`V+` dan `GND`↔`V-` (CONFIRMED dari foto unit fisik)

Modul relay 1-channel dengan optocoupler seperti ini punya **DUA rail suplai internal terpisah**: `VCC`/`GND` (untuk sisi LED optocoupler/kontrol, arus kecil ~5mA) dan `V+`/`V-` (untuk sisi coil relay, arus besar ~120mA) -- secara default kedua pasang ini disatukan lewat jumper/solder-blob kecil di PCB:

- **Jumper `VCC`↔`V+` TERPASANG (default pabrik)**: `VCC` dan `V+` DISATUKAN -- satu sumber tunggal menyuplai KEDUANYA sekaligus, termasuk arus besar coil. **HARUS DILEPAS** untuk project ini -- supaya coil (`V+`) disuplai dari step-down 3V terpisah, TIDAK ikut membebani pin 3.3V ESP32 yang menyuplai `VCC`.
- **Jumper `GND`↔`V-` TERPASANG (default pabrik)**: `GND` dan `V-` DISATUKAN -- ini **BOLEH DIBIARKAN tersambung**, TIDAK perlu dilepas. Alasannya: `GND` dan `V-` sama-sama cuma jalur RETURN (0V), tidak membawa arus besar terarah (arus besar coil masuk lewat `V+`, bukan lewat `V-`/`GND`). Menyatukan dua titik return 0V tidak membebani apa pun, dan justru MEMUDAHKAN pemenuhan syarat common ground wajib di bawah.

**Konsekuensi penting soal common ground:** terlepas dari jumper di atas, **`GND` step-down 3V tetap WAJIB disambung ke `GND` ESP32** -- ini soal REFERENSI SINYAL (`IN` dari GPIO6 perlu level tegangan yang terbaca benar oleh optocoupler, yang mengharuskan ground yang sama), BUKAN soal isolasi daya. Isolasi optocoupler (`EL817`, chip yang terlihat di foto board) yang sebenarnya berlaku adalah **antara sisi kontrol keseluruhan (VCC/GND/IN + V+/V-, semua common-ground dengan ESP32) DAN sisi kontak relay (`COM`/`NO`/`NC`, yang membawa AC mains)** -- itu isolasi yang benar-benar penting untuk keselamatan (mencegah AC mains "bocor" balik ke ESP32 lewat optocoupler), bukan isolasi antar dua rail suplai DC internal modul.

**Layout PCB modul relay murah bisa bervariasi antar batch produksi -- posisi jumper dan jalur PCB unit fisik SEBAIKNYA tetap diverifikasi visual (ikuti jalur PCB dari jumper ke pin VCC/V+ dengan mata, bantuan kaca pembesar kalau perlu) sebelum menganggap penjelasan di atas berlaku 100% untuk unit spesifik yang dipegang.**

### Langkah TEST relay sebelum disambung ke microswitch (WAJIB untuk active-HIGH/LOW meski sudah terkonfirmasi dari listing -- konfirmasi akhir sebelum sambung ke AC mains)

1. Cek/lepas jumper `VCC`↔`V+` (lihat penjelasan di atas) -- pastikan `VCC` (kontrol) dan `V+` (coil) TERPISAH, supaya coil benar-benar disuplai dari step-down 3V terpisah, bukan ikut ditarik dari sumber kontrol ESP32. Jumper `GND`↔`V-` boleh dibiarkan tersambung.
2. Sambungkan modul relay ke ESP32: `IN`->GPIO6, `VCC`->ESP32 3.3V (arus sisi ini kecil ~5mA, TIDAK signifikan seperti arus coil), `V+`->step-down 3V (SUDAH diverifikasi arusnya lewat langkah verifikasi arus di atas), DAN **GND (baik dari ESP32 maupun dari step-down 3V) disambung jadi SATU titik common ground** -- WAJIB, lihat penjelasan di atas. **JANGAN sambungkan apa pun ke sisi `NC`/`COM`/`NO` dulu**.

   **WAJIB juga di langkah ini -- resistor pull-down 10k-ohm dari GPIO6 ke GND (KONFIRMASI dari 2 audit independen):**
   ```
   GPIO6 ──┬──────────────► ke relay IN (jalur sinyal utama, TETAP ADA)
           │
           └──[resistor 10k-ohm]──► GND
   ```
   Kaki A resistor disambung ke titik yang sama dengan kabel GPIO6->IN (di titik mana saja sepanjang jalur itu), kaki B ke GND (titik common ground yang sama dengan seluruh sistem). **Ini BUKAN pengganti kabel GPIO6->IN, melainkan cabang tambahan paralel.**

   **Kenapa ini perlu:** sebelum firmware sempat jalan (periode bootloader, sebelum `setup()`/`begin()` dipanggil), GPIO6 dalam kondisi floating/high-impedance -- levelnya tidak terdefinisi, rentan terbaca HIGH oleh noise/induksi di sekitarnya. Karena relay ini **active-HIGH**, level HIGH yang tidak disengaja itu bisa membuat relay energized sesaat saat ESP32 baru saja diberi daya/reset -- motor bisa menyentak menyala tanpa perintah, berbahaya karena terhubung ke AC mains. Resistor pull-down "menjangkarkan" GPIO6 ke LOW (0V) selama tidak ada yang aktif men-drive-nya, tanpa mengganggu kemampuan ESP32 mendrive HIGH nanti setelah firmware jalan (output driver ESP32 jauh lebih kuat dari resistor 10k-ohm).

   GPIO6 pada ESP32-C6 BUKAN strapping pin (dikonfirmasi dari datasheet resmi Espressif -- strapping pins ESP32-C6 hanya GPIO4/5/8/9/15), jadi risiko spesifik ke pin ini lebih rendah dibanding pin strapping. Tapi window floating sebelum firmware jalan tetap ada secara fisik untuk GPIO manapun -- Espressif sendiri merekomendasikan pull-up/pull-down untuk pin manapun yang terhubung ke aktuator kritis, independen dari status strapping. Resistor ini murah dan tidak ada downside, tetap dipasang meski risikonya tidak setinggi pin strapping.

   Software (`GpioMotorController::begin()`) sudah diperbaiki supaya `digitalWrite(LOW)` dipanggil SEBELUM `pinMode(OUTPUT)` -- tapi ini cuma lapis kedua, TIDAK menggantikan resistor fisik, karena window sebelum `begin()` sempat dipanggil sama sekali (termasuk periode bootloader) tidak bisa ditutup software manapun.
3. Flash firmware, buka Serial Monitor, ketik `stop` dulu (pastikan motor GPIO OFF/LOW) -- relay seharusnya DIAM (tidak energized), konsisten dengan active-HIGH.
4. Test motor ON sesaat lewat command Serial yang sesuai -- relay seharusnya "klik" menyala.
5. **Setelah dikonfirmasi berperilaku sesuai (diam saat LOW, menyala saat HIGH)**, baru sambungkan sisi `NC`/`COM`/`NO` ke microswitch (lihat diagram di bawah).

```
Buck converter 5V              Modul step-down          Relay Module (part fisik JQC3F-03VDC-C,
(sudah ada di project)          5V -> 3V (BARU)          coil 3V, ~120mA ekspektasi -- VERIFIKASI
                                                          dengan multimeter, optocoupler EL817,
                                                          ACTIVE HIGH, CONFIRMED dari foto fisik)
    +5V OUT ------------------>| IN (5V)      |         +---------------------------+     Microswitch
                                |          3V  |----+--->| V+  (suplai COIL relay,    |     Eureka
                                |          OUT  |    |    |      lewat jumper VCC-V+   |     (2 kabel,
    GND -----------------------|GND        GND |--+ |    |      DILEPAS/dipisah)      |     AC mains)
                                +--------------+  | |    |                            |
                                                   | +--->| V-  (return coil, jumper   |
                                                   |      |      GND-V- BOLEH tetap    |
                                                   |      |      tersambung)           |
ESP32 3.3V ---------------------------------------------->| VCC (suplai optocoupler,   |
                                                   |      |      arus kecil ~5mA saja) |
ESP32 GND ------------------------------------------+---->| GND (kontrol, common       |
                                                          |      ground WAJIB)         |
ESP32 GPIO6 --------------------------------------------->| IN  (sinyal kontrol)       |
                                                          |                            |
                                                          |  COM (kontak) ------------>|-------- Kabel microswitch #1
                                                          |  NO  (kontak) ------------>|-------- Kabel microswitch #2
                                                          +---------------------------+
```

**Penjelasan:**
- **`V+` (suplai coil relay) disuplai dari modul step-down 5V->3V terpisah** (diberi daya dari buck converter 5V yang sudah ada di project ini) -- BUKAN dari pin 3.3V board ESP32-C6-Touch-AMOLED-1.64, karena arus coil relay (~120mA ekspektasi, WAJIB diverifikasi lewat multimeter) berisiko membebani regulator on-board board tersebut berlebihan bersamaan dengan beban lain (display AMOLED, ESP32 core, HX711). **`VCC` (suplai optocoupler/kontrol) dari ESP32 3.3V** -- arus sisi ini kecil (~5mA, dominasi arus datang dari LED optocoupler bukan coil), jadi TIDAK signifikan seperti arus `V+`.
- **Jumper `VCC`↔`V+` HARUS DILEPAS** (posisi terpisah, bukan posisi default "link in place") supaya `VCC` dan `V+` benar-benar dua rail terpisah. **Kalau jumper masih terpasang, `V+`/coil akan ikut ditarik dari sumber yang sama dengan `VCC`** -- yang berarti kalau `VCC` disuplai dari ESP32, coil JUGA ikut menarik ~120mA dari ESP32, PERSIS masalah yang sedang kita hindari.
- **Jumper `GND`↔`V-` BOLEH DIBIARKAN tersambung** (posisi default pabrik) -- keduanya cuma jalur return 0V, tidak membawa arus besar terarah, jadi aman disatukan. Ini justru memudahkan pemenuhan syarat common ground di bawah.
- **Verifikasi posisi kedua jumper secara visual (ikuti jalur PCB) SEBELUM menyambung apa pun.**
- **`GND` WAJIB disambung bersama** antara: GND buck converter 5V, GND modul step-down 3V, GND relay (`GND` kontrol, otomatis termasuk `V-` lewat jumper yang dibiarkan tersambung), DAN GND ESP32 -- SEMUANYA satu titik common ground yang sama. Ini soal REFERENSI SINYAL (`GPIO6`->`IN` perlu level tegangan yang terbaca benar oleh optocoupler LED), BUKAN soal isolasi daya. Titik temu ground ini bisa pakai terminal block kecil/breadboard sebagai simpul bersama.
- Sisi **kontak** relay (`COM` dan `NO` di blok terminal kiri, dry contact) disambung PARALEL ke kedua kabel microswitch fisik -- sisi ini membawa AC mains penuh, TIDAK disatukan dengan ground ESP32/step-down dalam kondisi apa pun. Isolasi optocoupler (`EL817`) yang berlaku di sini adalah ANTARA seluruh sisi kontrol (VCC/V+/V-/GND/IN, semua common-ground dengan ESP32) DAN sisi kontak (`COM`/`NO`/`NC`, AC mains) -- BUKAN isolasi antar `VCC` dan `V+` itu sendiri (keduanya sama-sama di sisi kontrol/DC, cuma dipisah supaya arus besar coil tidak membebani sumber kontrol).
- **Logika HIGH/LOW: TERKONFIRMASI active-HIGH** dari halaman produk -- `MOTOR_GPIO_ACTIVE_HIGH=true` di `config.h` SUDAH BENAR untuk modul ini, tidak perlu diubah ke `false`.
- Motor Eureka Mignon **momentary/held** (dikonfirmasi langsung ke unit fisik) -- motor menyala SELAMA sinyal HIGH ditahan (relay menutup terus), berhenti seketika begitu LOW (relay membuka). Bukan pulse/toggle.
- **JANGAN gunakan optocoupler diskrit (PC817/EL817) yang dirakit sendiri untuk switching AC mains langsung** -- itu cocok untuk sinyal low-voltage (seperti pin 3 konektor Specialita di referensi upstream), TAPI TIDAK cocok/aman untuk switching AC mains tanpa desain isolasi/rating tegangan yang tepat. Untuk topologi paralel-microswitch ini, **relay module siap pakai yang memang dirating untuk AC mains adalah komponen yang benar**, bukan optocoupler diskrit rakitan sendiri.

**Kenapa GPIO6 (bukan GPIO4 seperti versi sebelumnya)**: firmware ini sekarang menyambungkan UI LVGL untuk panel AMOLED board ESP32-C6-Touch-AMOLED-1.64 -- panel itu memakai GPIO4/GPIO5 secara permanen untuk QSPI (terverifikasi dari source resmi vendor, lihat `src/ui/lv_port.h` & `vendor-reference/`). Pin motor & HX711 dipindah ke GPIO yang dipastikan bebas dari bentrok itu.

---

## Diagram wiring -- HX711 + Load Cell

```
Load Cell (4-wire)              HX711                    ESP32-C6
+--------------+          +--------------+          +--------------+
| Merah  (E+)  |--------->| E+           |          |              |
| Hitam  (E-)  |--------->| E-           |          |              |
| Putih  (A-)  |--------->| A-           |          |              |
| Hijau  (A+)  |--------->| A+           |          |              |
| Kuning       |--------->| GND (shield) |          |              |
| (shield)     |          |              |          |              |
+--------------+          |          VCC |<---------| 3.3V         |
                          |          GND |<---------| GND          |
                          |          DT  |--------->| GPIO2 (DOUT) |
                          |          SCK |<---------| GPIO3 (SCK)  |
                          +--------------+          +--------------+
```

**Catatan:**
- HX711 hanya punya 1 pin GND -- solder kabel shield (kuning) ke bagian belakang pin header GND (lihat catatan upstream di `jaapp/smart-grind-by-weight` docs).
- Jaga kabel load cell sependek mungkin untuk kurangi noise.
- Pin GPIO2/GPIO3 di atas dipilih supaya TIDAK bentrok dengan QSPI display board ESP32-C6-Touch-AMOLED-1.64 (yang memakai GPIO4/GPIO5 secara permanen) DAN tidak bentrok dengan USB Serial/JTAG Controller ESP32-C6 (GPIO12/GPIO13, dipakai default -- firmware ini bergantung padanya untuk Serial monitor lewat `-DARDUINO_USB_CDC_ON_BOOT=1`, lihat `platformio.ini`) maupun strapping pin (GPIO4/5/8/9/15). Bisa diganti pin lain kalau perlu, sesuaikan `HX711_DOUT_PIN`/`HX711_SCK_PIN` di `config.h` -- cek daftar GPIO bebas di `src/ui/lv_port.h` dulu sebelum memilih pin baru (JANGAN pakai GPIO12/GPIO13).

---

## Buck Converter (5V/3.3V dari mains atau sumber DC)

Project ini sekarang memakai **DUA** buck converter terpisah:

1. **Buck converter 5V** (yang sudah ada sebelumnya) -- suplai umum ke HX711/ESP32 dari sumber DC eksternal (bukan lewat USB). Kalau dipakai, pastikan:
   - Output diukur dengan multimeter SEBELUM disambung ke ESP32/HX711 -- pastikan tegangan output sesuai (5V untuk VCC HX711 kalau modul HX711 kamu 5V-native, atau 3.3V kalau modul mendukung -- cek datasheet modul HX711 yang dibeli).
   - GND buck converter, ESP32, dan HX711 harus satu jalur bersama (common ground) -- tanpa ini, pembacaan HX711 bisa tidak stabil/salah.
   - Jangan sambung output buck converter ke pin 5V ESP32 dan ke sumber USB power secara bersamaan (risiko back-feed antar sumber power).

2. **Modul step-down 5V->3V (BARU)** -- KHUSUS untuk suplai `V+` relay module (lihat section "Diagram wiring -- Motor" di atas untuk alasan lengkap: arus coil relay ~120mA ekspektasi -- WAJIB diverifikasi lewat multimeter untuk unit fisik yang dipakai -- dianggap terlalu besar untuk dibebankan ke regulator 3.3V on-board board display ESP32-C6-Touch-AMOLED-1.64). Input modul step-down ini diambil dari OUTPUT buck converter 5V di atas (bukan sumber terpisah lagi) -- jadi rantainya: sumber DC eksternal -> buck converter 5V -> modul step-down 5V->3V -> `V+` relay.
   - **GND WAJIB satu jalur bersama** di SEMUA titik: sumber DC eksternal, buck converter 5V, modul step-down 3V, relay (sisi kontrol, termasuk `V-` yang dijumper ke `GND`), DAN ESP32 -- lihat penjelasan lengkap soal pentingnya common ground ini di section "Diagram wiring -- Motor" di atas.
   - Ukur output modul step-down dengan multimeter (harus ~3V, sesuai rating coil relay `JQC3F-03VDC-C`) SEBELUM disambung ke `V+` relay.

---

## Ringkasan pin (sesuaikan di `config.h` kalau beda)

| Fungsi | GPIO ESP32-C6 | Keterangan |
|---|---|---|
| Motor control (via relay module, paralel microswitch) | GPIO6 | `MOTOR_GPIO_PIN` |
| HX711 DOUT | GPIO2 | `HX711_DOUT_PIN` |
| HX711 SCK | GPIO3 | `HX711_SCK_PIN` |

**Kenapa bukan GPIO4/5/6/12 seperti versi-versi lama**: board display ESP32-C6-Touch-AMOLED-1.64 memakai GPIO4/GPIO5 secara permanen untuk QSPI ke panel AMOLED (terverifikasi dari source resmi vendor, lihat `src/ui/lv_port.h` & folder `vendor-reference/`). GPIO12 (dicoba di draft sebelumnya) ternyata pin default USB Serial/JTAG Controller ESP32-C6 -- bentrok dengan Serial monitor project ini (`-DARDUINO_USB_CDC_ON_BOOT=1` di `platformio.ini`). Pin motor/HX711 akhirnya dipindah ke GPIO2/GPIO3/GPIO6 -- semuanya dipastikan bukan strapping pin, bukan USB-JTAG, dan tidak bentrok display/touch/IMU/SD card.

## Setelah wiring selesai

1. Ketik `raw` di Serial Monitor -- cek apakah HX711 merespons (nilai raw berubah kalau load cell ditekan/dilepas beban).
2. Ikuti README bagian "Cara kalibrasi HX711" untuk isi `HX711_CALIBRATION_OFFSET`/`HX711_CALIBRATION_SCALE` di `config.h` dengan angka asli.
3. Test motor dengan hati-hati -- jauhkan tangan/benda dari burr grinder saat testing GPIO motor pertama kali (ada risiko motor menyala tak terduga kalau wiring/logic level relay salah). Verifikasi dulu relay "klik" dengan benar SEBELUM menyambung ke microswitch fisik (lihat langkah verifikasi active-HIGH/active-LOW di atas).
