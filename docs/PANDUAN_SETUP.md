# Panduan Setup SmartProctor IoT

## Gambaran Arsitektur

```
[Kamera/Webcam] → [PC Vision (Python)] ──┐
                                          ├──► [Firebase Realtime Database] ──► [Web Dashboard]
[ESP32 Node] ────────────────────────────┘
  ├── Sensor PIR (Gerak)
  ├── Sensor Suara
  └── Buzzer
```

---

## Langkah 1: Buat Project Firebase

1. Buka [console.firebase.google.com](https://console.firebase.google.com)
2. Klik **"Add project"** → beri nama (misal: `smartproctor`)
3. Masuk ke project → pilih menu **"Realtime Database"** di sidebar
4. Klik **"Create Database"** → pilih lokasi → mulai dalam **Test Mode** (untuk pengembangan)
5. Salin URL database (contoh: `https://smartproctor-xxxxx-default-rtdb.firebaseio.com`)

### Dapatkan Database Secret (untuk Python & ESP32)

1. Firebase Console → ⚙️ **Project Settings**
2. Tab **"Service accounts"**
3. Scroll ke bawah → **"Database secrets"**
4. Klik **"Show"** → salin secret key

### Dapatkan Web Config (untuk Dashboard)

1. Firebase Console → ⚙️ **Project Settings**
2. Tab **"General"** → scroll ke **"Your apps"**
3. Klik ikon `</>` (Web) → daftarkan app
4. Salin objek `firebaseConfig`

---

## Langkah 2: Konfigurasi PC Vision (Python)

Edit file `pc_vision/deteksi_ujian.py`, cari bagian:

```python
FIREBASE_URL  = "https://YOUR_PROJECT_ID-default-rtdb.firebaseio.com"
FIREBASE_AUTH = "YOUR_FIREBASE_DATABASE_SECRET"
PESERTA_ID    = "peserta_01"
PESERTA_NAMA  = "Peserta 01"
```

Ganti dengan nilai dari Firebase project kamu.

### Jalankan:

```bash
cd pc_vision
source venv/bin/activate
pip install -r requirements.txt   # jika belum
python deteksi_ujian.py
```

> Tekan `q` pada jendela kamera untuk berhenti.

---

## Langkah 3: Konfigurasi ESP32

### Hardware yang Dibutuhkan:

| Komponen      | Jumlah | GPIO (default) |
|---------------|--------|----------------|
| ESP32 Dev Board | 1    | —              |
| Sensor PIR (HC-SR501) | 1 | GPIO **14** |
| Sensor Suara (modul digital) | 1 | GPIO **27** |
| Buzzer aktif  | 1      | GPIO **26**    |

### Koneksi Wiring:

```
PIR Sensor:
  VCC  → 3.3V ESP32
  GND  → GND ESP32
  OUT  → GPIO 14

Sensor Suara (modul KY-038 atau sejenis):
  VCC  → 3.3V atau 5V (cek datasheet modul)
  GND  → GND ESP32
  D0   → GPIO 27  ← Pin digital, bukan analog

Buzzer Aktif:
  (+) → GPIO 26  (via transistor jika perlu arus lebih)
  (-) → GND ESP32
```

### Library yang Dibutuhkan di Arduino IDE:

1. **ArduinoJson** by Benoit Blanchon (versi 6.x)
   - Arduino IDE → Tools → Manage Libraries → cari "ArduinoJson"

2. Board ESP32 sudah terinstall:
   - Arduino IDE → Preferences → Additional Boards Manager URLs:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → cari "esp32"

### Edit Konfigurasi di `esp32_node.ino`:

```cpp
const char* WIFI_SSID     = "NAMA_WIFI_KAMU";
const char* WIFI_PASSWORD = "PASSWORD_WIFI_KAMU";
const char* FIREBASE_HOST = "YOUR_PROJECT_ID-default-rtdb.firebaseio.com";
const char* FIREBASE_AUTH = "YOUR_FIREBASE_DATABASE_SECRET";
const char* NODE_ID       = "node_01";
```

### Upload ke ESP32:

1. Buka `esp32_node/esp32_node.ino` di Arduino IDE
2. Pilih Board: `Tools → Board → ESP32 Arduino → ESP32 Dev Module`
3. Pilih Port yang sesuai
4. Klik **Upload** (→)
5. Buka Serial Monitor (115200 baud) untuk melihat log

---

## Langkah 4: Konfigurasi Web Dashboard

Edit file `web_dashboard/app.js`, cari:

```javascript
const firebaseConfig = {
  apiKey:            "YOUR_API_KEY",
  authDomain:        "YOUR_PROJECT_ID.firebaseapp.com",
  databaseURL:       "https://YOUR_PROJECT_ID-default-rtdb.firebaseio.com",
  projectId:         "YOUR_PROJECT_ID",
  // ...
};
```

Ganti dengan config yang didapat dari langkah 1.

### Buka Dashboard:

Buka file `web_dashboard/index.html` langsung di browser.

> ⚠️ **Catatan CORS**: Jika data tidak muncul, buka via HTTP server:
> ```bash
> cd web_dashboard
> python3 -m http.server 8080
> # Lalu buka: http://localhost:8080
> ```

### Mode Demo:

Jika `firebaseConfig` belum diisi, dashboard otomatis masuk **Mode Demo** dengan data simulasi — cocok untuk presentasi tanpa koneksi internet.

---

## Langkah 5: Atur Firebase Rules (Keamanan)

Untuk pengembangan, set rules berikut di Firebase Console → Realtime Database → Rules:

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

> ⚠️ Untuk produksi, gunakan rules yang lebih ketat dengan autentikasi.

---

## Struktur Data Firebase

```
SmartProctor/
├── peserta/
│   └── peserta_01/
│       ├── nama:          "Peserta 01"
│       ├── arah_wajah:    "Ke Depan"
│       ├── status_visual: "Aman (Fokus)"
│       ├── level_risiko:  0          ← 0=aman, 1=curiga, 2=curang
│       ├── durasi_menoleh: 0.0
│       ├── wajah_terdeteksi: true
│       └── last_update:   "2026-05-14 13:00:00"
│
├── node/
│   └── node_01/
│       ├── sensor_gerak:   false
│       ├── sensor_suara:   false
│       ├── buzzer_aktif:   false
│       ├── sinyal_wifi:    -62
│       ├── jumlah_alert:   0
│       ├── status_koneksi: "online"
│       └── uptime_detik:   123
│
└── log/
    └── {push_id}/
        ├── sumber:     "visual" | "sensor" | "sistem"
        ├── jenis:      "gerak" | "suara" | "visual" | "curang"
        ├── pesan:      "Deskripsi kejadian"
        ├── peserta_id: "peserta_01"   ← atau node_id
        └── timestamp:  "2026-05-14 13:00:00"
```

---

## Troubleshooting

| Masalah | Kemungkinan Penyebab | Solusi |
|---------|----------------------|--------|
| `AttributeError: module 'mediapipe' has no attribute 'solutions'` | mediapipe versi baru | `pip install mediapipe==0.10.14` |
| ESP32 tidak bisa connect WiFi | SSID/password salah | Periksa konfigurasi, pastikan WiFi 2.4GHz |
| Firebase error 401 | Auth/Secret salah | Periksa FIREBASE_AUTH |
| Dashboard kosong | Firebase belum dikonfigurasi | Mode demo aktif, isi `firebaseConfig` di app.js |
| Kamera tidak muncul | `VideoCapture(0)` gagal | Ganti ke `VideoCapture(1)` untuk kamera eksternal |
| Buzzer terus bunyi | Sensor suara terlalu sensitif | Putar potensiometer sensitivitas pada modul sensor |

---

*SmartProctor IoT — Sistem Anti-Kecurangan Ujian*
