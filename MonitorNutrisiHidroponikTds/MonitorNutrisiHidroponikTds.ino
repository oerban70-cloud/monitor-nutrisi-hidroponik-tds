// MonitorNutrisiHidroponikTds — Monitor nutrisi hidroponik TDS
// (ESP32 + sensor TDS/EC + LCD I2C + buzzer alarm)
// ---------------------------------------------------------------------------
// Probe TDS dicelup ke larutan nutrisi hidroponik di tandon. Modul sensor
// TDS mengubah daya hantar listrik larutan menjadi tegangan analog 0-2,3 V.
// ESP32 membaca tegangan itu lewat ADC, merata-rata 30 sampel,
// mengonversinya jadi angka ppm (parts per million) dengan rumus kalibrasi
// standar modul TDS (polinomial DFRobot Gravity TDS), lalu menampilkannya
// di LCD 16x2 I2C tiap 2 detik.
//
// Rentang aman sayur daun (selada, sawi, kangkung): 800 sampai 1200 ppm —
// rentang umum untuk sayuran daun hidroponik. Di bawah 800 ppm nutrisi
// terlalu encer (sayur kerdil), di atas 1200 ppm terlalu pekat (akar bisa
// terbakar). Di luar rentang itu buzzer berbunyi dan LED merah menyala,
// LED hijau padam. Di dalam rentang aman, LED hijau menyala dan buzzer diam.
//
// Kompensasi suhu: rumus di bawah memakai suhu acuan 25 derajat C.
// Proyek ini TIDAK memakai sensor suhu DS18B20 karena library OneWire /
// DallasTemperature belum terinstal di mesin ini, dan suhu larutan
// hidroponik rumahan umumnya dekat 25 derajat C, sehingga faktor
// kompensasinya mendekati 1. Kalau tandonmu di luar ruangan dan suhunya
// jauh dari 25 derajat, pasang DS18B20 dan ganti SUHU_ACUAN_C dengan suhu
// terukur, lalu aktifkan faktor kompensasi di teganganKePpm().
//
// Kalibrasi (lakukan sekali dengan larutan kalibrasi 1413 uS/cm):
//   1. Celup probe ke larutan kalibrasi, catat tegangan dari Serial Monitor.
//   2. Kalau angka ppm yang tampil jauh dari 707 ppm (1413 uS/cm x 0,5),
//      sesuaikan FAKTOR_KALIBRASI sampai angkanya cocok.
//
// Catatan ADC: modul dicatu 5 V (VIN), tapi keluaran analognya hanya
// 0-2,3 V, jadi aman untuk ADC ESP32 (maksimal 3,3 V).
//
// Wiring (lihat schematic.png):
//   Modul sensor TDS  A (analog) -> GPIO34 (ADC1, hanya input)
//   Modul sensor TDS  VCC -> 5V (VIN), GND -> GND
//   LCD 16x2 I2C (0x27) SDA -> GPIO21, SCL -> GPIO22, VCC -> 5V, GND -> GND
//   Buzzer aktif HIGH   +  -> GPIO25, - -> GND
//   LED merah (seri resistor 220 ohm)  anoda -> GPIO33, katoda -> GND
//   LED hijau (seri resistor 220 ohm)  anoda -> GPIO32, katoda -> GND
//   ESP32 dicatu USB 5V.
//
// Library yang dipakai:
//   LiquidCrystal_I2C 1.1.2 (lihat folder libraries/)
// ---------------------------------------------------------------------------

#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// ------------------------- Konfigurasi -------------------------------------
// Pin sensor dan aktuator.
constexpr uint8_t PIN_TDS_ANALOG = 34;  // keluaran analog modul TDS (ADC1_CH6)
constexpr uint8_t PIN_BUZZER     = 25;  // buzzer aktif HIGH
constexpr uint8_t PIN_LED_MERAH  = 33;  // LED merah alarm, aktif HIGH
constexpr uint8_t PIN_LED_HIJAU  = 32;  // LED hijau normal, aktif HIGH

// Pin I2C LCD (pin default Wire ESP32).
constexpr uint8_t PIN_LCD_SDA = 21;
constexpr uint8_t PIN_LCD_SCL = 22;

// Alamat dan ukuran LCD.
constexpr uint8_t LCD_ALAMAT = 0x27;
constexpr uint8_t LCD_KOLOM  = 16;
constexpr uint8_t LCD_BARIS  = 2;

// ADC ESP32: resolusi 12 bit, referensi 3,3 V.
constexpr uint16_t ADC_MAKS        = 4095;
constexpr float    ADC_TEGANGAN_REF = 3.3f;

// Kalibrasi TDS (rumus standar modul TDS DFRobot, suhu acuan 25 C).
// EC (mS/cm) = (133,42*V^3 - 255,86*V^2 + 857,39*V) * 0,5
// ppm = EC * 500 (faktor konversi NaCl 500 ppm per mS/cm).
constexpr float TDS_KOEF_A = 133.42f;  // koefisien V^3
constexpr float TDS_KOEF_B = -255.86f; // koefisien V^2
constexpr float TDS_KOEF_C = 857.39f;  // koefisien V
constexpr float TDS_FAKTOR_EC   = 0.5f;   // faktor 0,5 dari rumus EC
constexpr float TDS_PPM_PER_EC  = 500.0f; // ppm per mS/cm (faktor NaCl)
constexpr float FAKTOR_KALIBRASI = 1.0f;   // sesuaikan setelah kalibrasi
constexpr float SUHU_ACUAN_C     = 25.0f; // suhu acuan rumus (tanpa DS18B20)

// Rentang ppm aman untuk sayur daun hidroponik (rentang umum).
constexpr float PPM_MIN_AMAN = 800.0f;
constexpr float PPM_MAX_AMAN = 1200.0f;

// Sampling dan interval ukur.
constexpr uint8_t  JUMLAH_SAMPEL    = 30;   // sampel yang dirata-rata
constexpr uint32_t JEDA_SAMPEL_MS   = 20;   // jeda antar sampel
constexpr uint32_t INTERVAL_UKUR_MS = 2000; // ukur dan tampil tiap 2 detik
constexpr uint32_t BAUD_SERIAL      = 115200;

// Telegram dimatikan untuk proyek ini: alat bekerja mandiri tanpa WiFi,
// jadi tidak perlu token bot maupun kredensial WiFi. Ubah ke true hanya
// bila sudah menambahkan kode kirim Telegram (butuh WiFiClientSecure dan
// UniversalTelegramBot) — kode di bawah tidak memanggil Telegram apa pun
// selama toggle ini false.
constexpr bool USE_TELEGRAM = false;

// ------------------------- Objek global ------------------------------------
LiquidCrystal_I2C lcd(LCD_ALAMAT, LCD_KOLOM, LCD_BARIS);

uint32_t waktuUkurTerakhir = 0;  // millis() pengukuran terakhir

// ------------------------- Fungsi ------------------------------------------
// Baca tegangan keluaran modul TDS (volt), rata-rata dari beberapa sampel
// supaya angka di LCD tidak lompat-lompat.
float bacaTeganganTds() {
  uint32_t jumlah = 0;
  for (uint8_t i = 0; i < JUMLAH_SAMPEL; i++) {
    jumlah += analogRead(PIN_TDS_ANALOG);
    delay(JEDA_SAMPEL_MS);
  }
  float rataRata = jumlah / static_cast<float>(JUMLAH_SAMPEL);
  return rataRata * ADC_TEGANGAN_REF / ADC_MAKS;
}

// Ubah tegangan (volt) menjadi ppm memakai rumus standar modul TDS.
// Tanpa sensor suhu, faktor kompensasi suhu = 1 (suhu dianggap 25 C).
float teganganKePpm(float tegangan) {
  float v = tegangan;
  float ec = (TDS_KOEF_A * v * v * v + TDS_KOEF_B * v * v + TDS_KOEF_C * v)
             * TDS_FAKTOR_EC;
  if (ec < 0.0f) {
    ec = 0.0f;
  }
  return ec * TDS_PPM_PER_EC * FAKTOR_KALIBRASI;
}

// Teks status berdasarkan rentang aman.
const char* statusPpm(float ppm) {
  if (ppm < PPM_MIN_AMAN) {
    return "RENDAH!";
  }
  if (ppm > PPM_MAX_AMAN) {
    return "TINGGI!";
  }
  return "NORMAL";
}

// Tampilkan ppm dan status di LCD (baris 1 angka, baris 2 status).
void tampilkanLcd(float ppm, const char* status) {
  lcd.setCursor(0, 0);
  lcd.print("PPM: ");
  lcd.print(static_cast<int>(ppm + 0.5f));
  lcd.print("    ");
  lcd.setCursor(0, 1);
  lcd.print("Status: ");
  lcd.print(status);
  lcd.print("     ");
}

// Atur buzzer dan LED sesuai kondisi alarm (true = di luar rentang aman).
void aturAlarm(bool alarm) {
  digitalWrite(PIN_BUZZER, alarm ? HIGH : LOW);
  digitalWrite(PIN_LED_MERAH, alarm ? HIGH : LOW);
  digitalWrite(PIN_LED_HIJAU, alarm ? LOW : HIGH);
}

void setup() {
  Serial.begin(BAUD_SERIAL);
  Serial.println("Monitor nutrisi hidroponik TDS mulai...");
  if (USE_TELEGRAM) {
    Serial.println("USE_TELEGRAM aktif tapi kode Telegram belum dipasang.");
  }

  pinMode(PIN_TDS_ANALOG, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  aturAlarm(false);

  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Nutrisi hidroponik");
  lcd.setCursor(0, 1);
  lcd.print("ESP32 + TDS");
  delay(2000);
  lcd.clear();
}

void loop() {
  uint32_t sekarang = millis();
  if (sekarang - waktuUkurTerakhir < INTERVAL_UKUR_MS) {
    return;
  }
  waktuUkurTerakhir = sekarang;

  float tegangan = bacaTeganganTds();
  float ppm = teganganKePpm(tegangan);
  const char* status = statusPpm(ppm);
  bool alarm = (ppm < PPM_MIN_AMAN) || (ppm > PPM_MAX_AMAN);

  tampilkanLcd(ppm, status);
  aturAlarm(alarm);

  Serial.print("Tegangan: ");
  Serial.print(tegangan, 3);
  Serial.print(" V  PPM: ");
  Serial.print(static_cast<int>(ppm + 0.5f));
  Serial.print("  Status: ");
  Serial.println(status);
}
