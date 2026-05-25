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

// === TAMBAHAN LIBRARY UNTUK MEMATIKAN BROWNOUT ===
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
// =================================================

// ============================================================
//  KONFIGURASI JARINGAN & FIREBASE
// ============================================================
const char* WIFI_SSID     = "Infinix SMART 6";      // WiFi Hotspot HP
const char* WIFI_PASSWORD = "rtkrtkrtk";            // Password Hotspot

const char* FIREBASE_HOST = "YOUR_PROJECT_ID-default-rtdb.firebaseio.com"; // Ganti!
const char* FIREBASE_AUTH = "YOUR_FIREBASE_DATABASE_SECRET"; // Ganti!

// Samakan NODE_ID dengan yang dituju oleh dashboard (misal node_01)
const char* NODE_ID = "node_01"; 

// ============================================================
//  KONFIGURASI PIN HARDWARE (Sesuai Breadboard Kita)
// ============================================================
const int PIN_PIR       = 13;   
const int PIN_SUARA     = 32;   
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
bool status_buzzer = false;
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
  // === MEMATIKAN BROWNOUT DETECTOR ===
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  // ===================================

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
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Jika WiFi putus, coba hubungkan kembali
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    WiFi.reconnect();
    return;
  }

  // 1. BACA SENSOR
  status_gerak = (digitalRead(PIN_PIR) == HIGH);
  status_suara = (digitalRead(PIN_SUARA) == HIGH); // Modul KY-037 umumnya HIGH saat berisik

  // 2. LOGIKA ALARM
  status_buzzer = (status_gerak || status_suara);

  if (status_buzzer) {
    digitalWrite(PIN_BUZZER, HIGH);
    digitalWrite(PIN_LED_MERAH, HIGH);
    digitalWrite(PIN_LED_HIJAU, LOW);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_LED_HIJAU, HIGH);
  }

  // 3. UPDATE KE FIREBASE TIAP 1 DETIK
  if (millis() - timer_update >= UPDATE_INTERVAL) {
    timer_update = millis();

    // Logika penambahan jumlah alert untuk dashboard
    if (status_buzzer) {
      jumlah_alert++;
    }

    // Membuat JSON Payload (Disinkronkan dengan kebutuhan app.js)
    StaticJsonDocument<200> doc;
    doc["sensor_gerak"]   = status_gerak;
    doc["sensor_suara"]   = status_suara;
    doc["buzzer_aktif"]   = status_buzzer;
    doc["sinyal_wifi"]    = WiFi.RSSI();
    doc["jumlah_alert"]   = jumlah_alert;
    doc["status_koneksi"] = "online";
    doc["uptime_detik"]   = millis() / 1000;

    String payload;
    serializeJson(doc, payload);

    // MENGIRIM KE PATH YANG BENAR AGAR TERBACA OLEH DASHBOARD
    bool sukses = kirimKeFirebase("/SmartProctor/node/" + String(NODE_ID), payload);
    
    // Tampilkan di Serial Monitor
    Serial.printf("Gerak: %d | Suara: %d | Buzzer: %s | Sinyal: %d dBm | Firebase: %s\n", 
                  status_gerak, status_suara, status_buzzer ? "ON" : "OFF", WiFi.RSSI(), sukses ? "OK" : "FAIL");
  }

  delay(100);
}
