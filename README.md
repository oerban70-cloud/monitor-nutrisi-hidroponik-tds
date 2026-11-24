# Monitor Nutrisi Hidroponik TDS — ESP32 + Sensor TDS + LCD I2C + Buzzer

Sayur hidroponikmu kurus padahal daunnya hijau? Satu angka ini jawabannya.
Alat mungil ini membaca pekat nutrisi langsung dari air tandon: probe TDS
dicelup ke larutan nutrisi, modul TDS mengubah daya hantar listrik larutan
jadi tegangan analog, ESP32 membacanya lewat ADC, merata-rata 30 sampel,
mengonversinya jadi angka ppm, lalu menampilkannya di LCD 16x2 I2C tiap
2 detik. Begitu angka jatuh di bawah 800 atau di atas 1200 ppm, buzzer
berbunyi dan LED merah menyala — LED hijau menyala kalau ppm aman.

Video Short (60 detik): link menyusul setelah upload

## Cara kerja

1. Probe TDS dicelup ke larutan nutrisi di tandon hidroponik.
2. Modul sensor TDS mengubah daya hantar listrik (EC) larutan menjadi
   tegangan analog 0-2,3 V pada pin A.
3. ESP32 membaca pin A lewat ADC 12-bit (GPIO34) dan merata-rata 30
   sampel tiap pengukuran supaya angka di LCD tidak lompat-lompat.
4. Tegangan diubah jadi ppm dengan rumus standar modul TDS: EC dihitung
   dari polinomial (133,42 x V pangkat 3 - 255,86 x V pangkat 2 +
   857,39 x V) x 0,5, lalu ppm = EC x 500 (faktor konversi NaCl).
5. Angka ppm dan status (NORMAL, RENDAH!, atau TINGGI!) tampil di LCD
   tiap 2 detik. Di luar rentang 800-1200 ppm, buzzer berbunyi, LED
   merah menyala, dan LED hijau padam.
6. Kalibrasi dilakukan sekali dengan larutan kalibrasi 1413 uS/cm
   (lihat komentar di sketch): sesuaikan `FAKTOR_KALIBRASI` sampai angka
   yang tampil cocok (sekitar 707 ppm).

Tanpa sensor suhu, rumus memakai suhu acuan 25 derajat C. Library
OneWire / DallasTemperature belum terinstal di mesin ini, jadi DS18B20
tidak dipakai — suhu larutan hidroponik rumahan umumnya dekat 25 derajat
C sehingga faktor kompensasinya mendekati 1. Kalau tandonmu di luar
ruangan dan suhunya jauh dari 25 derajat, pasang DS18B20 dan ganti
`SUHU_ACUAN_C` dengan suhu terukur (lihat komentar di sketch).

## Wiring

| Komponen | Pin modul | ESP32 |
| --- | --- | --- |
| Modul sensor TDS A | A | GPIO34 (ADC1) |
| Modul sensor TDS VCC | VCC | 5V (VIN) |
| Modul sensor TDS GND | GND | GND |
| LCD 16x2 I2C (0x27) SDA | SDA | GPIO21 |
| LCD 16x2 I2C (0x27) SCL | SCL | GPIO22 |
| LCD 16x2 I2C VCC | VCC | 5V (VIN) |
| LCD 16x2 I2C GND | GND | GND |
| Buzzer aktif HIGH (+) | + | GPIO25 |
| Buzzer (-) | - | GND |
| LED merah (seri 220 Ω) | Anoda | GPIO33 |
| LED merah (seri 220 Ω) | Katoda | GND |
| LED hijau (seri 220 Ω) | Anoda | GPIO32 |
| LED hijau (seri 220 Ω) | Katoda | GND |
| ESP32 | USB | Adaptor USB 5V |

Skema lengkap (gaya Fritzing): lihat `schematic.png`.

PERHATIAN: modul TDS dicatu 5V (VIN), tapi keluaran analognya hanya
0-2,3 V, jadi aman untuk ADC ESP32 (maksimal 3,3 V). Rentang 800-1200 ppm
adalah rentang umum untuk sayur daun (selada, sawi, kangkung); tanaman
buah seperti tomat dan cabai biasanya butuh ppm lebih tinggi — sesuaikan
`PPM_MIN_AMAN` dan `PPM_MAX_AMAN` dengan tanamanmu.

## Konfigurasi

Di `MonitorNutrisiHidroponikTds/MonitorNutrisiHidroponikTds.ino`:

- `PIN_TDS_ANALOG` (34) — keluaran analog modul TDS (ADC1).
- `PIN_BUZZER` (25) / `PIN_LED_MERAH` (33) / `PIN_LED_HIJAU` (32).
- `PIN_LCD_SDA` (21) / `PIN_LCD_SCL` (22) — bus I2C LCD.
- `PPM_MIN_AMAN` (800) / `PPM_MAX_AMAN` (1200) — rentang ppm aman.
- `FAKTOR_KALIBRASI` (1,0) — sesuaikan setelah kalibrasi 1413 uS/cm.
- `JUMLAH_SAMPEL` (30) / `JEDA_SAMPEL_MS` (20) — perata-rata sampel.
- `INTERVAL_UKUR_MS` (2000) — ukur dan tampil tiap 2 detik.
- `USE_TELEGRAM` (false) — Telegram dimatikan, alat bekerja mandiri.
- `LCD_ALAMAT` (0x27) — alamat I2C LCD.

## Komponen

- ESP32 DevKit
- Modul sensor TDS / EC meter + probe TDS
- LCD 16x2 I2C (alamat 0x27)
- Buzzer aktif HIGH (5V)
- LED merah + LED hijau + resistor 220 Ω (2 buah)
- Adaptor USB 5V untuk ESP32

## Compile (terverifikasi)

Terverifikasi 24 Nov 2026 dengan `arduino-cli` 1.6.0, ESP32 core 4.0.0:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 MonitorNutrisiHidroponikTds
```

- Sketch memakai 322.497 byte (24%) dari program storage (maks. 1.310.720 byte).
- Variabel global memakai 24.704 byte (7%) dari dynamic memory (maks. 327.680 byte).

## Library

Pasang lewat Arduino Library Manager (Sketch > Include Library >
Manage Libraries), atau lewat arduino-cli:

```
arduino-cli lib install "LiquidCrystal I2C"
```

- LiquidCrystal_I2C 1.1.2

## Lisensi

MIT — lihat `LICENSE`.
