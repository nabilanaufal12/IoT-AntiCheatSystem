/**
 * SmartProctor IoT - ESP32 Node
 * ============================================================
 * Fungsi:
 * - Membaca Sensor PIR (Deteksi Gerak) di GPIO 13
 * - Membaca Sensor Suara (KY-037) di GPIO 12
 * - Mengaktifkan Buzzer di GPIO 14
 * - Mengaktifkan LED Merah/Hijau di GPIO 27 & 26 (Opsional)
 * - Mengirim status ke Firebase Realtime Database
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
//  KONFIGURASI JARINGAN & FIREBASE
// ============================================================
const char* WIFI_SSID     = "NAMA_WIFI_KAMU";      // Ganti!
const char* WIFI_PASSWORD = "PASSWORD_WIFI_KAMU";    // Ganti!

const char* FIREBASE_HOST = "YOUR_PROJECT_ID-default-rtdb.firebaseio.com"; // Ganti!
const char* FIREBASE_AUTH = "YOUR_FIREBASE_DATABASE_SECRET"; // Ganti!

const char* NODE_ID = "meja_01"; 

// ============================================================
//  KONFIGURASI PIN HARDWARE (Sesuai Breadboard Kita)
// ============================================================
const int PIN_PIR       = 13;   
const int PIN_SUARA     = 12;   
const int PIN_BUZZER    = 14;   
const int PIN_LED_MERAH = 27;   // Opsional
const int PIN_LED_HIJAU = 26;   // Opsional

// ============================================================
//  VARIABEL SISTEM
// ============================================================
const unsigned long UPDATE_INTERVAL = 1000;   // Update Firebase tiap 1 dtk
unsigned long timer_update = 0;

bool status_gerak = false;
bool status_suara = false;
int jumlah_alert = 0;

// Fungsi untuk mengirim data ke Firebase
bool kirimKeFirebase(const String& path, const String& payload) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + path + ".json?auth=" + String(FIREBASE_AUTH);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PATCH(payload);
  http.end();
  
  return (httpCode == 200 || httpCode == 204);
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_SUARA, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);
  digitalWrite(PIN_LED_HIJAU, HIGH); // Hijau nyala = Ready

  // Koneksi WiFi
  Serial.print("Menghubungkan ke WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Terhubung!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return;
  }

  // 1. BACA SENSOR
  status_gerak = (digitalRead(PIN_PIR) == HIGH);
  status_suara = (digitalRead(PIN_SUARA) == HIGH); // Modul KY-037 umumnya HIGH saat berisik

  // 2. LOGIKA ALARM
  bool kondisi_curang = (status_gerak || status_suara);

  if (kondisi_curang) {
    digitalWrite(PIN_BUZZER, HIGH);
    digitalWrite(PIN_LED_MERAH, HIGH);
    digitalWrite(PIN_LED_HIJAU, LOW);
    jumlah_alert++;
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_LED_HIJAU, HIGH);
  }

  // 3. UPDATE KE FIREBASE TIAP 1 DETIK
  if (millis() - timer_update >= UPDATE_INTERVAL) {
    timer_update = millis();

    // Membuat JSON Payload
    StaticJsonDocument<200> doc;
    doc["sensor_gerak"] = status_gerak;
    doc["sensor_suara"] = status_suara;
    doc["status_alarm"] = kondisi_curang ? "CURANG" : "AMAN";
    doc["jumlah_alert"] = jumlah_alert;

    String payload;
    serializeJson(doc, payload);

    bool sukses = kirimKeFirebase("/kelas_a/ujian_1/node/" + String(NODE_ID), payload);
    
    Serial.printf("Gerak: %d | Suara: %d | Alarm: %s | Firebase: %s\n", 
                  status_gerak, status_suara, kondisi_curang ? "ON" : "OFF", sukses ? "OK" : "FAIL");
  }

  delay(100);
}
