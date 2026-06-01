"""
SmartProctor IoT - PC Vision (Deteksi Wajah)
============================================================
Fungsi:
  - Mendeteksi arah wajah peserta menggunakan webcam
  - Menentukan status kecurangan berdasarkan durasi menoleh
  - Mengirim data real-time ke Firebase Realtime Database

Dependencies: Lihat requirements.txt
  pip install -r requirements.txt

Cara Menjalankan:
  1. Isi bagian KONFIGURASI di bawah ini
  2. Aktifkan venv: source venv/bin/activate
  3. Jalankan: python deteksi_ujian.py
  4. Tekan 'q' untuk berhenti
============================================================
"""

# pyrefly: ignore [missing-import]
import cv2
# pyrefly: ignore [missing-import]
import mediapipe as mp
# pyrefly: ignore [missing-import]
import numpy as np
import time
import threading
import requests
import json
from datetime import datetime

# ============================================================
#  KONFIGURASI - ISI BAGIAN INI SESUAI PROYEKMU
# ============================================================

# Firebase Realtime Database URL (dari Firebase Console > Realtime Database)
# Contoh: "https://smartproctor-abc12-default-rtdb.firebaseio.com"
FIREBASE_URL  = "https://iot-anticheatsystem-default-rtdb.asia-southeast1.firebasedatabase.app"

# Firebase Database Secret
# Firebase Console > Project Settings > Service Accounts > Database Secrets > Show
FIREBASE_AUTH = "S47nw4xUV6EslppYxVMjAt4VjGuaYFZckt2ForE3"

# ID Peserta yang sedang diawasi PC ini
PESERTA_ID    = "peserta_01"
PESERTA_NAMA  = "Peserta 01"

# Waktu (detik) sebelum status berubah menjadi "KECURANGAN TERDETEKSI"
BATAS_WAKTU_CURANG = 3.0

# Interval pengiriman data ke Firebase (detik)
INTERVAL_FIREBASE = 1.0

# ============================================================
#  INISIALISASI MEDIAPIPE
# ============================================================
mp_face_mesh = mp.solutions.face_mesh
face_mesh    = mp_face_mesh.FaceMesh(
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5
)

# ============================================================
#  STATE GLOBAL (shared antara main thread & firebase thread)
# ============================================================
state_lock = threading.Lock()
state = {
    "arah_wajah":    "Tidak Terdeteksi",
    "status_visual": "Tidak Ada Wajah",
    "level_risiko":  0,    # 0=aman, 1=mencurigakan, 2=curang
    "durasi_menoleh": 0.0,
    "wajah_terdeteksi": False,
}

firebase_berjalan = True  # Flag untuk menghentikan thread Firebase

# ============================================================
#  FUNGSI FIREBASE
# ============================================================

def update_firebase(data: dict) -> bool:
    """
    Mengirim data ke Firebase via REST API (HTTP PATCH).
    Tidak melempar exception agar tidak mengganggu loop utama.
    """
    try:
        url = f"{FIREBASE_URL}/SmartProctor/peserta/{PESERTA_ID}.json?auth={FIREBASE_AUTH}"
        resp = requests.patch(url, json=data, timeout=3)
        return resp.status_code == 200
    except Exception as e:
        print(f"[Firebase] Gagal mengirim: {e}")
        return False


def kirim_log_firebase(jenis: str, pesan: str):
    """Mengirim log kecurangan ke Firebase (POST = push entry baru)."""
    try:
        url = f"{FIREBASE_URL}/SmartProctor/log.json?auth={FIREBASE_AUTH}"
        payload = {
            "sumber":     "visual",
            "peserta_id": PESERTA_ID,
            "jenis":      jenis,
            "pesan":      pesan,
            "timestamp":  datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        }
        requests.post(url, json=payload, timeout=3)
    except Exception:
        pass


def thread_firebase():
    """
    Thread terpisah untuk mengirim data ke Firebase secara berkala.
    Berjalan di background agar tidak memblokir loop kamera.
    """
    # Daftarkan peserta ke Firebase saat pertama kali
    update_firebase({
        "nama":          PESERTA_NAMA,
        "peserta_id":    PESERTA_ID,
        "status_visual": "Memulai...",
        "arah_wajah":    "-",
        "level_risiko":  0,
        "last_update":   datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    })
    print(f"[Firebase] Peserta '{PESERTA_NAMA}' terdaftar.")

    level_risiko_sebelumnya = -1  # Untuk deteksi perubahan (trigger log)

    while firebase_berjalan:
        with state_lock:
            data_kirim = dict(state)

        data_kirim["last_update"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        sukses = update_firebase(data_kirim)
        status_str = "OK" if sukses else "GAGAL"
        print(f"[Firebase] Kirim → {data_kirim['status_visual']} | {status_str}")

        # Kirim log jika level risiko naik ke 2 (curang)
        lvl = data_kirim["level_risiko"]
        if lvl == 2 and lvl != level_risiko_sebelumnya:
            kirim_log_firebase("visual", f"{PESERTA_NAMA} terdeteksi menoleh selama lebih dari {BATAS_WAKTU_CURANG} detik")
        level_risiko_sebelumnya = lvl

        time.sleep(INTERVAL_FIREBASE)

    # Kirim status offline saat program ditutup
    update_firebase({
        "status_visual": "Sesi Berakhir",
        "level_risiko":  0,
        "last_update":   datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    })
    print("[Firebase] Status offline dikirim. Thread selesai.")


# ============================================================
#  FUNGSI UTILITAS TAMPILAN
# ============================================================

def gambar_overlay(image, arah, status, durasi, wajah_ada):
    """Menggambar teks status pada frame kamera."""
    h, w = image.shape[:2]

    # Background panel bawah (semi-transparan)
    overlay = image.copy()
    cv2.rectangle(overlay, (0, h - 90), (w, h), (10, 10, 10), -1)
    cv2.addWeighted(overlay, 0.6, image, 0.4, 0, image)

    # Tentukan warna berdasarkan status
    if "KECURANGAN" in status:
        warna = (0, 0, 255)    # Merah
    elif "Mencurigakan" in status:
        warna = (0, 200, 255)  # Kuning
    else:
        warna = (0, 220, 80)   # Hijau

    # Teks arah wajah
    if wajah_ada:
        cv2.putText(image, f"Arah: {arah}",
                    (15, h - 60), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (200, 200, 200), 1, cv2.LINE_AA)

    # Teks status
    cv2.putText(image, status,
                (15, h - 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, warna, 2, cv2.LINE_AA)

    # Durasi menoleh (jika sedang menoleh)
    if durasi > 0:
        cv2.putText(image, f"{durasi:.1f}s",
                    (w - 80, h - 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, warna, 2, cv2.LINE_AA)

    # Label sistem (pojok kanan atas)
    cv2.putText(image, "SmartProctor IoT",
                (w - 200, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (150, 150, 150), 1, cv2.LINE_AA)
    cv2.putText(image, f"ID: {PESERTA_ID}",
                (w - 200, 45), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (120, 120, 120), 1, cv2.LINE_AA)

    return image


# ============================================================
#  PROGRAM UTAMA
# ============================================================

def main():
    global firebase_berjalan

    # Validasi konfigurasi Firebase
    if "YOUR_PROJECT_ID" in FIREBASE_URL or "YOUR_FIREBASE" in FIREBASE_AUTH:
        print("=" * 60)
        print("  ⚠  PERHATIAN: Firebase belum dikonfigurasi!")
        print("  Edit variabel FIREBASE_URL dan FIREBASE_AUTH")
        print("  di bagian KONFIGURASI pada file ini.")
        print("  Program tetap berjalan tanpa koneksi Firebase.")
        print("=" * 60)
        firebase_aktif = False
    else:
        firebase_aktif = True

    # Buka kamera (0 = kamera default, ganti ke 1 untuk kamera eksternal)
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("[ERROR] Tidak bisa membuka kamera! Periksa koneksi kamera.")
        return

    # Jalankan thread Firebase
    if firebase_aktif:
        fb_thread = threading.Thread(target=thread_firebase, daemon=True)
        fb_thread.start()
        print("[Firebase] Thread berjalan di background.")

    # Variabel timer kecurangan
    waktu_mulai_menoleh = None

    print(f"\n[Sistem] ✓ Deteksi wajah dimulai untuk '{PESERTA_NAMA}'")
    print("[Sistem]   Tekan 'q' untuk keluar.\n")

    try:
        while cap.isOpened():
            success, image = cap.read()
            if not success:
                continue

            # Konversi BGR → RGB untuk MediaPipe
            image_rgb = cv2.cvtColor(cv2.flip(image, 1), cv2.COLOR_BGR2RGB)
            image_rgb.flags.writeable = False
            results = face_mesh.process(image_rgb)
            image_rgb.flags.writeable = True
            image = cv2.cvtColor(image_rgb, cv2.COLOR_RGB2BGR)

            img_h, img_w, _ = image.shape
            wajah_terdeteksi = False
            arah             = "Ke Depan"
            status_peserta   = "Tidak Ada Wajah"
            level_risiko     = 0
            durasi           = 0.0

            if results.multi_face_landmarks:
                wajah_terdeteksi = True

                for face_landmarks in results.multi_face_landmarks:
                    face_2d, face_3d = [], []

                    for idx, lm in enumerate(face_landmarks.landmark):
                        if idx in [33, 263, 1, 61, 291, 199]:
                            if idx == 1:
                                nose_2d = (lm.x * img_w, lm.y * img_h)

                            x, y = int(lm.x * img_w), int(lm.y * img_h)
                            face_2d.append([x, y])
                            face_3d.append([x, y, lm.z])

                    face_2d = np.array(face_2d, dtype=np.float64)
                    face_3d = np.array(face_3d, dtype=np.float64)

                    focal_length = 1 * img_w
                    cam_matrix   = np.array([
                        [focal_length, 0,           img_h / 2],
                        [0,           focal_length, img_w / 2],
                        [0,           0,            1        ]
                    ])
                    dist_matrix = np.zeros((4, 1), dtype=np.float64)

                    success_pnp, rot_vec, _ = cv2.solvePnP(
                        face_3d, face_2d, cam_matrix, dist_matrix
                    )
                    rmat, _ = cv2.Rodrigues(rot_vec)
                    angles, _, _, _, _, _ = cv2.RQDecomp3x3(rmat)

                    y_angle = angles[1] * 360

                    # Tentukan arah wajah
                    if y_angle < -10:
                        arah = "Menoleh Kiri"
                    elif y_angle > 10:
                        arah = "Menoleh Kanan"
                    else:
                        arah = "Ke Depan"

                    # Logika timer kecurangan
                    if arah in ["Menoleh Kiri", "Menoleh Kanan"]:
                        if waktu_mulai_menoleh is None:
                            waktu_mulai_menoleh = time.time()

                        durasi = time.time() - waktu_mulai_menoleh

                        if durasi >= BATAS_WAKTU_CURANG:
                            status_peserta = "KECURANGAN TERDETEKSI!"
                            level_risiko   = 2
                        else:
                            status_peserta = f"Mencurigakan... ({int(durasi)}s)"
                            level_risiko   = 1
                    else:
                        waktu_mulai_menoleh = None
                        status_peserta      = "Aman (Fokus)"
                        level_risiko        = 0
                        durasi              = 0.0

            # Update state global (thread-safe)
            with state_lock:
                state["arah_wajah"]       = arah
                state["status_visual"]    = status_peserta
                state["level_risiko"]     = level_risiko
                state["durasi_menoleh"]   = round(durasi, 1)
                state["wajah_terdeteksi"] = wajah_terdeteksi

            # Gambar overlay pada frame
            image = gambar_overlay(image, arah, status_peserta, durasi, wajah_terdeteksi)

            # Tampilkan frame
            cv2.imshow("SmartProctor - Deteksi Wajah", image)

            if cv2.waitKey(5) & 0xFF == ord('q'):
                print("\n[Sistem] 'q' ditekan. Menghentikan sistem...")
                break

    finally:
        firebase_berjalan = False
        cap.release()
        cv2.destroyAllWindows()

        if firebase_aktif:
            fb_thread.join(timeout=5)

        print("[Sistem] ✓ Program selesai.")


if __name__ == "__main__":
    main()