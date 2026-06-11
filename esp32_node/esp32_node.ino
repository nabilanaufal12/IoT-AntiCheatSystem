/**
 * SmartProctor IoT - ESP32 Node (TWO-WAY CONTROL)
 * ============================================================
 * Fungsi:
 * - Membaca Sensor PIR (Deteksi Gerak) di GPIO 13
 * - Membaca Sensor Suara (KY-037) di GPIO 32
 * - Mengaktifkan Buzzer di GPIO 14
 * - Mengaktifkan LED Merah/Hijau/Kuning di GPIO 27, 26, & 25
 * - Mengirim status ke Firebase Realtime Database (PATCH)
 * - Menerima perintah kontrol dari Firebase (GET polling)
 *   → allow_pir, allow_suara, force_buzzer
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
const char* WIFI_SSID     = "jiee";      // WiFi Hotspot HP
const char* WIFI_PASSWORD = "11sebelas";            // Password Hotspot

const char* FIREBASE_HOST = "iot-anticheatsystem-default-rtdb.asia-southeast1.firebasedatabase.app"; // Ganti!
const char* FIREBASE_AUTH = "S47nw4xUV6EslppYxVMjAt4VjGuaYFZckt2ForE3"; // Ganti!

// Samakan NODE_ID dengan yang dituju oleh dashboard (misal node_01)
const char* NODE_ID = "node_02"; 

// ============================================================
//  KONFIGURASI PIN HARDWARE (Sesuai Breadboard Kita)
// ============================================================
const int PIN_PIR       = 13;   
const int PIN_SUARA     = 32;   
const int PIN_BUZZER    = 14;   
const int PIN_LED_MERAH = 27;   // Indikator Curang / Ada suara & gerak
const int PIN_LED_HIJAU = 26;   // Indikator Aman & Ready
const int PIN_LED_KUNING = 25;  // Indikator WiFi Terputus

// ============================================================
//  VARIABEL SISTEM
// ============================================================
const unsigned long UPDATE_INTERVAL  = 1000;  // Update Firebase tiap 1 dtk
const unsigned long KONTROL_INTERVAL = 2000;  // Polling kontrol tiap 2 dtk
unsigned long timer_update  = 0;
unsigned long timer_kontrol = 0;

// Variabel Timer untuk menahan Buzzer
unsigned long timer_buzzer = 0;
const unsigned long DURASI_BUZZER = 1000; // Buzzer menyala ditahan minimal 1 detik (1000ms)

bool status_gerak = false;
bool status_suara = false;
bool status_buzzer = false;
int jumlah_alert = 0;

// ============================================================
//  VARIABEL KONTROL DARI DASHBOARD (Two-Way)
// ============================================================
bool allow_pir       = true;   // Izinkan pembacaan sensor PIR
bool allow_suara     = true;   // Izinkan pembacaan sensor Suara
String force_buzzer  = "AUTO"; // "AUTO", "ON", "OFF"

// ============================================================
//  FUNGSI FIREBASE
// ============================================================

// Fungsi untuk mengirim data ke Firebase (PATCH)
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

// Fungsi untuk membaca kontrol dari Firebase (GET polling)
void bacaKontrolFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) 
             + "/SmartProctor/control/" + String(NODE_ID) 
             + ".json?auth=" + String(FIREBASE_AUTH);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    
    // Parse JSON response
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      // Baca nilai kontrol dari Firebase
      if (doc.containsKey("allow_pir")) {
        allow_pir = doc["allow_pir"].as<bool>();
      }
      if (doc.containsKey("allow_suara")) {
        allow_suara = doc["allow_suara"].as<bool>();
      }
      if (doc.containsKey("force_buzzer")) {
        force_buzzer = doc["force_buzzer"].as<String>();
      }
      
      Serial.printf("[Kontrol] PIR:%s | Suara:%s | Buzzer:%s\n",
                    allow_pir ? "ON" : "OFF",
                    allow_suara ? "ON" : "OFF",
                    force_buzzer.c_str());
    } else {
      Serial.printf("[Kontrol] JSON parse error: %s\n", error.c_str());
    }
  } else if (httpCode == 200 && http.getString() == "null") {
    // Node kontrol belum ada di Firebase, gunakan default
    Serial.println("[Kontrol] Node kontrol belum ada, menggunakan default");
  } else {
    Serial.printf("[Kontrol] HTTP GET gagal: %d\n", httpCode);
  }
  
  http.end();
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
  pinMode(PIN_LED_KUNING, OUTPUT);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);
  digitalWrite(PIN_LED_HIJAU, HIGH); // Hijau nyala = Ready
  digitalWrite(PIN_LED_KUNING, LOW); // Kuning mati saat awal

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
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_LED_KUNING, HIGH); // <-- Kuning menyala saat WiFi terputus
    
    WiFi.reconnect();
    return; // Berhenti mengeksekusi kode bawahnya sampai terhubung lagi
  } else {
    digitalWrite(PIN_LED_KUNING, LOW);  // <-- Kuning dimatikan jika WiFi terhubung aman
  }

  // ============================================================
  //  POLLING KONTROL DARI FIREBASE (setiap 2 detik)
  // ============================================================
  if (millis() - timer_kontrol >= KONTROL_INTERVAL) {
    timer_kontrol = millis();
    bacaKontrolFirebase();
  }

  // ============================================================
  //  1. BACA SENSOR (dengan pengecekan allow flag)
  // ============================================================
  // Sensor hanya dibaca jika diizinkan oleh dashboard
  bool ada_gerak = allow_pir   ? (digitalRead(PIN_PIR) == HIGH)   : false;
  bool ada_suara = allow_suara ? (digitalRead(PIN_SUARA) == HIGH) : false;

  // Menyimpan (latching) status jika terdeteksi, agar bisa dikirim ke Firebase
  if (ada_gerak) status_gerak = true;
  if (ada_suara) status_suara = true;

  // ============================================================
  //  2. LOGIKA ALARM DENGAN OVERRIDE BUZZER
  // ============================================================
  
  if (force_buzzer == "ON") {
    // === MODE PAKSA NYALA ===
    // Buzzer dipaksa nyala terus, terlepas dari sensor
    digitalWrite(PIN_BUZZER, HIGH);
    digitalWrite(PIN_LED_MERAH, HIGH);
    digitalWrite(PIN_LED_HIJAU, LOW);
    status_buzzer = true;
    
  } else if (force_buzzer == "OFF") {
    // === MODE PAKSA MATI ===
    // Buzzer dipaksa mati, meskipun sensor mendeteksi sesuatu
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_LED_HIJAU, HIGH);
    status_buzzer = false;
    // Reset status sensor agar tidak menumpuk
    status_gerak = false;
    status_suara = false;
    
  } else {
    // === MODE AUTO (logika asli) ===
    // Jika terdeteksi gerak atau suara, mulai timer buzzer
    if (ada_gerak || ada_suara) {
      status_buzzer = true;
      timer_buzzer = millis(); // Catat waktu mulai bunyi
    }
    
    if (status_buzzer) {
      digitalWrite(PIN_BUZZER, HIGH);
      digitalWrite(PIN_LED_MERAH, HIGH);
      digitalWrite(PIN_LED_HIJAU, LOW);

      // Matikan alarm otomatis jika sudah melebihi DURASI_BUZZER (1 detik)
      // dan bersihkan status sensor untuk pembacaan berikutnya
      if (millis() - timer_buzzer >= DURASI_BUZZER) {
        status_buzzer = false;
        status_gerak = false; 
        status_suara = false;
      }
    } else {
      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_LED_MERAH, LOW);
      digitalWrite(PIN_LED_HIJAU, HIGH);
    }
  }

  // ============================================================
  //  3. UPDATE KE FIREBASE TIAP 1 DETIK
  // ============================================================
  if (millis() - timer_update >= UPDATE_INTERVAL) {
    timer_update = millis();

    // Logika penambahan jumlah alert untuk dashboard
    if (status_buzzer && force_buzzer == "AUTO") {
      jumlah_alert++;
    }

    // Membuat JSON Payload (Disinkronkan dengan kebutuhan app.js)
    StaticJsonDocument<256> doc;
    doc["sensor_gerak"]   = status_gerak;
    doc["sensor_suara"]   = status_suara;
    doc["buzzer_aktif"]   = status_buzzer;
    doc["sinyal_wifi"]    = WiFi.RSSI();
    doc["jumlah_alert"]   = jumlah_alert;
    doc["status_koneksi"] = "online";
    doc["uptime_detik"]   = millis() / 1000;
    // Kirim juga status kontrol agar dashboard tahu kondisi aktual
    doc["allow_pir"]      = allow_pir;
    doc["allow_suara"]    = allow_suara;
    doc["force_buzzer"]   = force_buzzer;

    String payload;
    serializeJson(doc, payload);

    // MENGIRIM KE PATH YANG BENAR AGAR TERBACA OLEH DASHBOARD
    bool sukses = kirimKeFirebase("/SmartProctor/node/" + String(NODE_ID), payload);
    
    // Tampilkan di Serial Monitor
    Serial.printf("Gerak: %d | Suara: %d | Buzzer: %s | Mode: %s | Firebase: %s\n", 
                  status_gerak, status_suara, status_buzzer ? "ON" : "OFF", 
                  force_buzzer.c_str(), sukses ? "OK" : "FAIL");
  }

  // PERHATIAN: delay(100); di baris terbawah sudah SAYA HAPUS. 
  // Ini penting agar pembacaan sensor sangat responsif!
}
