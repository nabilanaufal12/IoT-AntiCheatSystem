/**
 * SmartProctor IoT — Dashboard Logic
 * ============================================================
 * Menghubungkan dashboard ke Firebase Realtime Database dan
 * menampilkan data sensor secara real-time.
 *
 * KONFIGURASI: Isi objek firebaseConfig di bawah ini.
 * Cara mendapat config: Firebase Console > Project Settings > Your apps > SDK setup
 * ============================================================
 */

// ============================================================
//  KONFIGURASI FIREBASE — ISI SESUAI PROYEKMU
// ============================================================
const firebaseConfig = {

  apiKey: "AIzaSyCS9ZP6J1GzroyWVTr3iRS9XcTfziITzvg",

  authDomain: "iot-anticheatsystem.firebaseapp.com",

  databaseURL: "https://iot-anticheatsystem-default-rtdb.asia-southeast1.firebasedatabase.app",

  projectId: "iot-anticheatsystem",

  storageBucket: "iot-anticheatsystem.firebasestorage.app",

  messagingSenderId: "950527625460",

  appId: "1:950527625460:web:9f51b4d796e6977d6cf6a1"

};


// ============================================================
//  INISIALISASI
// ============================================================
const DEMO_MODE = firebaseConfig.apiKey === "YOUR_API_KEY";

let db = null;
let sessionStartTime = Date.now();
let totalLogCount = 0;

// State lokal untuk menghitung statistik
const localState = {
  peserta:   {},  // { peserta_id: { ...data } }
  nodes:     {},  // { node_id:    { ...data } }
  logEntries: [], // Array log lokal (buat tampilan)
};

// ============================================================
//  DOM REFERENCES
// ============================================================
const elSessionTimer  = document.getElementById('session-timer');
const elCurrentTime   = document.getElementById('current-time');
const elConnDot       = document.getElementById('conn-dot');
const elConnLabel     = document.getElementById('conn-label');
const elValPeserta    = document.getElementById('val-total-peserta');
const elValAlert      = document.getElementById('val-alert-aktif');
const elValNode       = document.getElementById('val-sensor-online');
const elValLog        = document.getElementById('val-log-count');
const elPesertaGrid   = document.getElementById('peserta-grid');
const elNodeGrid      = document.getElementById('node-grid');
const elLogContainer  = document.getElementById('log-container');
const elBadgePeserta  = document.getElementById('badge-peserta-count');
const elBadgeNode     = document.getElementById('badge-node-count');
const elStatAlert     = document.getElementById('stat-alert-aktif');
const elBtnClear      = document.getElementById('btn-clear-log');
const elLogEmpty      = document.getElementById('log-empty');

// Control Panel DOM References
const elCtrlAllowPir    = document.getElementById('ctrl-allow-pir');
const elCtrlAllowSuara  = document.getElementById('ctrl-allow-suara');
const elCtrlForceBuzzer = document.getElementById('ctrl-force-buzzer');
const elCtrlKameraAktif = document.getElementById('ctrl-kamera-aktif');

// ============================================================
//  UTILITAS
// ============================================================

function formatTime(date) {
  return date.toLocaleTimeString('id-ID', { hour12: false });
}

function formatDateTime(date) {
  return date.toLocaleTimeString('id-ID', { hour12: false });
}

function padTwo(n) {
  return String(n).padStart(2, '0');
}

function updateClock() {
  const now = new Date();
  elCurrentTime.textContent = formatTime(now);

  // Session timer
  const elapsed = Math.floor((Date.now() - sessionStartTime) / 1000);
  const h = Math.floor(elapsed / 3600);
  const m = Math.floor((elapsed % 3600) / 60);
  const s = elapsed % 60;
  elSessionTimer.textContent = `${padTwo(h)}:${padTwo(m)}:${padTwo(s)}`;
}

function setConnectionStatus(state) {
  // state: 'connecting' | 'online' | 'error'
  elConnDot.className = 'conn-dot';
  if (state === 'online') {
    elConnDot.classList.add('online');
    elConnLabel.textContent = 'Firebase Terhubung';
  } else if (state === 'error') {
    elConnDot.classList.add('error');
    elConnLabel.textContent = 'Koneksi Gagal';
  } else if (state === 'demo') {
    elConnDot.classList.add('warning');
    elConnLabel.textContent = 'Mode Demo (Tanpa Firebase)';
  } else {
    elConnLabel.textContent = 'Menghubungkan...';
  }
}

// ============================================================
//  STATISTIK
// ============================================================

function updateStats() {
  const totalPeserta = Object.keys(localState.peserta).length;
  const alertAktif   = Object.values(localState.peserta)
    .filter(p => p.level_risiko >= 2).length;
  const nodeOnline   = Object.values(localState.nodes)
    .filter(n => n.status_koneksi === 'online').length;

  elValPeserta.textContent = totalPeserta || '0';
  elValAlert.textContent   = alertAktif   || '0';
  elValNode.textContent    = nodeOnline   || '0';
  elValLog.textContent     = totalLogCount;

  // Glow efek pada stat alert jika ada alert
  if (alertAktif > 0) {
    elStatAlert.classList.add('has-alert');
  } else {
    elStatAlert.classList.remove('has-alert');
  }
}

// ============================================================
//  RENDER PESERTA CARD
// ============================================================

function getStatusClass(levelRisiko, wajahAda) {
  if (!wajahAda) return 'status-offline';
  if (levelRisiko >= 2) return 'status-curang';
  if (levelRisiko === 1) return 'status-curiga';
  return 'status-aman';
}

function getStatusBadge(status, levelRisiko, wajahAda) {
  if (!wajahAda) {
    return `<div class="card-status-badge badge-offline">
      <span>📵</span> Kamera Tidak Aktif
    </div>`;
  }
  if (levelRisiko >= 2 || (status && status.includes('KECURANGAN'))) {
    return `<div class="card-status-badge badge-curang">
      <span>🚨</span> KECURANGAN TERDETEKSI
    </div>`;
  }
  if (levelRisiko === 1 || (status && status.includes('Mencurigakan'))) {
    return `<div class="card-status-badge badge-curiga">
      <span>⚠️</span> ${status || 'Mencurigakan'}
    </div>`;
  }
  return `<div class="card-status-badge badge-aman">
    <span>✅</span> ${status || 'Aman (Fokus)'}
  </div>`;
}

function renderPesertaCard(id, data) {
  const statusClass = getStatusClass(data.level_risiko, data.wajah_terdeteksi);
  const badgeHtml   = getStatusBadge(data.status_visual, data.level_risiko, data.wajah_terdeteksi);
  const inisial     = (data.nama || id).charAt(0).toUpperCase();
  const lastUpdate  = data.last_update || '—';
  const durasi      = data.durasi_menoleh > 0 ? `${data.durasi_menoleh}s` : '0s';

  return `
    <div class="peserta-card ${statusClass}" id="card-${id}">
      <div class="card-top">
        <div class="card-avatar">${inisial}</div>
        <div class="card-meta">
          <div class="card-nama">${data.nama || id}</div>
          <div class="card-id">${id}</div>
        </div>
      </div>

      ${badgeHtml}

      <div class="card-details">
        <div class="detail-item">
          <div class="detail-label">Arah Wajah</div>
          <div class="detail-value">${data.arah_wajah || '—'}</div>
        </div>
        <div class="detail-item">
          <div class="detail-label">Durasi Menoleh</div>
          <div class="detail-value">${durasi}</div>
        </div>
      </div>

      <div class="card-footer">
        <span class="card-last-update">⏱ ${lastUpdate}</span>
      </div>
    </div>
  `;
}

function updatePesertaGrid() {
  const pesertaIds = Object.keys(localState.peserta);
  elBadgePeserta.textContent = `${pesertaIds.length} peserta`;

  if (pesertaIds.length === 0) {
    elPesertaGrid.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">📡</div>
        <p>Menunggu data peserta...</p>
        <small>Pastikan <code>deteksi_ujian.py</code> berjalan</small>
      </div>`;
    return;
  }

  elPesertaGrid.innerHTML = pesertaIds
    .map(id => renderPesertaCard(id, localState.peserta[id]))
    .join('');
}

// ============================================================
//  RENDER NODE CARD
// ============================================================

function renderNodeCard(nodeId, data) {
  const isOnline    = data.status_koneksi === 'online';
  const statusClass = isOnline ? 'node-online' : 'node-offline';
  const pillClass   = isOnline ? 'pill-online'  : 'pill-offline';
  const pillLabel   = isOnline ? 'ONLINE'       : 'OFFLINE';
  const buzzerOn    = data.buzzer_aktif;
  const gerakOn     = data.sensor_gerak;
  const suaraOn     = data.sensor_suara;

  return `
    <div class="node-card ${statusClass}" id="node-${nodeId}">
      <div class="node-header">
        <div class="node-name">
          <div class="node-icon">📟</div>
          <span>${nodeId}</span>
        </div>
        <span class="node-status-pill ${pillClass}">${pillLabel}</span>
      </div>

      <div class="node-sensors">
        <div class="sensor-item ${gerakOn ? 'active' : ''}">
          <div class="sensor-name">Sensor Gerak</div>
          <div class="sensor-val">${gerakOn ? '🔴 ADA' : '🟢 AMAN'}</div>
        </div>
        <div class="sensor-item ${suaraOn ? 'active' : ''}">
          <div class="sensor-name">Sensor Suara</div>
          <div class="sensor-val">${suaraOn ? '🔴 ADA' : '🟢 AMAN'}</div>
        </div>
      </div>

      <div class="buzzer-indicator ${buzzerOn ? 'on' : 'off'}">
        🔔 Buzzer: ${buzzerOn ? 'AKTIF' : 'Mati'}
      </div>

      <div class="node-footer" style="margin-top: 10px;">
        <span>WiFi: ${data.sinyal_wifi || '—'} dBm</span>
        <span>Alert: ${data.jumlah_alert || 0}x</span>
      </div>
    </div>
  `;
}

function updateNodeGrid() {
  const nodeIds = Object.keys(localState.nodes);
  elBadgeNode.textContent = `${nodeIds.length} node`;

  if (nodeIds.length === 0) {
    elNodeGrid.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">🔌</div>
        <p>Menunggu data node...</p>
        <small>Pastikan ESP32 menyala dan terhubung WiFi</small>
      </div>`;
    return;
  }

  elNodeGrid.innerHTML = nodeIds
    .map(id => renderNodeCard(id, localState.nodes[id]))
    .join('');
}

// ============================================================
//  RENDER LOG
// ============================================================

function addLogEntry(entry) {
  totalLogCount++;
  elValLog.textContent = totalLogCount;

  // Tentukan jenis dan class
  const jenis    = entry.jenis || entry.sumber || 'sistem';
  let dotClass   = 'sistem';
  let tagClass   = 'tag-sistem';
  let tagLabel   = 'SISTEM';

  if (jenis === 'visual' || jenis === 'wajah') {
    dotClass = 'visual'; tagClass = 'tag-visual'; tagLabel = 'VISUAL';
  } else if (jenis === 'sensor' || jenis === 'gerak' || jenis === 'suara') {
    dotClass = 'sensor'; tagClass = 'tag-sensor'; tagLabel = 'SENSOR';
  } else if (jenis === 'curang' || (entry.pesan && entry.pesan.includes('KECURANGAN'))) {
    dotClass = 'curang'; tagClass = 'tag-curang'; tagLabel = 'ALERT';
  }

  const waktu  = entry.timestamp || formatDateTime(new Date());
  const pesan  = entry.pesan     || '—';
  const sumber = entry.node_id   || entry.peserta_id || '—';

  const html = `
    <div class="log-entry">
      <div class="log-dot ${dotClass}"></div>
      <div class="log-body">
        <div class="log-pesan">${pesan}</div>
        <div class="log-meta">
          <span class="log-tag ${tagClass}">${tagLabel}</span>
          <span>${sumber}</span>
          <span>${waktu}</span>
        </div>
      </div>
    </div>
  `;

  // Hapus empty state
  if (elLogEmpty) elLogEmpty.remove();

  // Tambah ke atas
  elLogContainer.insertAdjacentHTML('afterbegin', html);

  // Batasi tampilan log (max 100 entri di DOM)
  const entries = elLogContainer.querySelectorAll('.log-entry');
  if (entries.length > 100) {
    entries[entries.length - 1].remove();
  }
}

// ============================================================
//  FIREBASE LISTENERS
// ============================================================

function initFirebase() {
  try {
    firebase.initializeApp(firebaseConfig);
    db = firebase.database();

    // Test koneksi
    db.ref('.info/connected').on('value', snap => {
      setConnectionStatus(snap.val() ? 'online' : 'connecting');
    });

    // Listener: Data Peserta
    db.ref('SmartProctor/peserta').on('value', snap => {
      const data = snap.val();
      if (data) {
        localState.peserta = data;
        updatePesertaGrid();
        updateStats();
      }
    });

    // Listener: Data Node ESP32
    db.ref('SmartProctor/node').on('value', snap => {
      const data = snap.val();
      if (data) {
        localState.nodes = data;
        updateNodeGrid();
        updateStats();
      }
    });

    // Listener: Log Aktivitas (hanya entri baru)
    db.ref('SmartProctor/log')
      .orderByKey()
      .limitToLast(50)
      .on('child_added', snap => {
        const entry = snap.val();
        if (entry) addLogEntry(entry);
      });

    // Inisialisasi panel kontrol two-way
    initControlPanel();

    console.log('[Firebase] ✓ Semua listener aktif');

  } catch (err) {
    console.error('[Firebase] Error inisialisasi:', err);
    setConnectionStatus('error');
    addLogEntry({
      jenis: 'sistem',
      pesan: 'Gagal terhubung ke Firebase. Periksa konfigurasi di app.js',
      timestamp: formatDateTime(new Date()),
    });
  }
}

// ============================================================
//  DEMO MODE (tanpa Firebase)
// ============================================================

function startDemoMode() {
  setConnectionStatus('demo');

  addLogEntry({ jenis: 'sistem',
    pesan: '🔧 Mode Demo aktif. Isi firebaseConfig di app.js untuk data real.',
    timestamp: formatDateTime(new Date()), node_id: 'sistem' });

  // Simulasi data peserta
  localState.peserta['peserta_01'] = {
    nama: 'Peserta 01', arah_wajah: 'Ke Depan',
    status_visual: 'Aman (Fokus)', level_risiko: 0,
    durasi_menoleh: 0, wajah_terdeteksi: true,
    last_update: formatDateTime(new Date()),
  };
  localState.peserta['peserta_02'] = {
    nama: 'Peserta 02', arah_wajah: 'Menoleh Kanan',
    status_visual: 'Mencurigakan... (2s)', level_risiko: 1,
    durasi_menoleh: 2.1, wajah_terdeteksi: true,
    last_update: formatDateTime(new Date()),
  };
  localState.nodes['node_01'] = {
    sensor_gerak: false, sensor_suara: false,
    buzzer_aktif: false, sinyal_wifi: -62,
    jumlah_alert: 0, status_koneksi: 'online',
  };

  updatePesertaGrid();
  updateNodeGrid();
  updateStats();

  // Simulasi perubahan data setiap beberapa detik
  let tick = 0;
  setInterval(() => {
    tick++;

    // Simulasi peserta 02 mencapai status curang
    if (tick % 10 === 0) {
      const prev = localState.peserta['peserta_02'].level_risiko;
      const next  = prev === 2 ? 0 : prev + 1;
      localState.peserta['peserta_02'].level_risiko = next;
      localState.peserta['peserta_02'].durasi_menoleh = next * 1.5;
      localState.peserta['peserta_02'].status_visual =
        next === 0 ? 'Aman (Fokus)' : next === 1 ? 'Mencurigakan... (1s)' : 'KECURANGAN TERDETEKSI!';
      localState.peserta['peserta_02'].arah_wajah =
        next === 0 ? 'Ke Depan' : 'Menoleh Kanan';
      localState.peserta['peserta_02'].last_update = formatDateTime(new Date());

      if (next === 2) {
        addLogEntry({ jenis: 'curang',
          pesan: '[DEMO] Peserta 02 terdeteksi menoleh terlalu lama',
          timestamp: formatDateTime(new Date()), peserta_id: 'peserta_02' });
      } else if (next === 0) {
        addLogEntry({ jenis: 'visual',
          pesan: '[DEMO] Peserta 02 kembali fokus ke depan',
          timestamp: formatDateTime(new Date()), peserta_id: 'peserta_02' });
      }
      updatePesertaGrid();
      updateStats();
    }

    // Simulasi sensor PIR
    if (tick % 15 === 0) {
      const gerak = !localState.nodes['node_01'].sensor_gerak;
      localState.nodes['node_01'].sensor_gerak = gerak;
      localState.nodes['node_01'].buzzer_aktif = gerak;
      if (gerak) {
        localState.nodes['node_01'].jumlah_alert++;
        addLogEntry({ jenis: 'sensor',
          pesan: '[DEMO] Gerakan mencurigakan terdeteksi oleh node_01',
          timestamp: formatDateTime(new Date()), node_id: 'node_01' });
      }
      updateNodeGrid();
      updateStats();
    }
  }, 2000);
}

// ============================================================
//  CONTROL PANEL — TWO-WAY CONTROL
// ============================================================

function initControlPanel() {
  if (!db) return;

  // ----- REF FIREBASE CONTROL -----
  const refNodeControl    = db.ref('SmartProctor/control/node_02');
  const refPesertaControl = db.ref('SmartProctor/control/peserta_01');

  // ----- INISIALISASI DEFAULT VALUES (jika belum ada di Firebase) -----
  refNodeControl.once('value', snap => {
    if (!snap.exists()) {
      refNodeControl.set({
        allow_pir: true,
        allow_suara: true,
        force_buzzer: 'AUTO'
      });
      console.log('[Control] Default kontrol node_02 dibuat di Firebase');
    }
  });

  refPesertaControl.once('value', snap => {
    if (!snap.exists()) {
      refPesertaControl.set({
        kamera_aktif: true
      });
      console.log('[Control] Default kontrol peserta_01 dibuat di Firebase');
    }
  });

  // ----- LISTENER: Sinkronkan UI dengan Firebase (agar multi-dashboard sinkron) -----
  refNodeControl.on('value', snap => {
    const data = snap.val();
    if (!data) return;

    // Sync toggle switches tanpa trigger event listener lagi
    if (typeof data.allow_pir === 'boolean' && elCtrlAllowPir.checked !== data.allow_pir) {
      elCtrlAllowPir.checked = data.allow_pir;
    }
    if (typeof data.allow_suara === 'boolean' && elCtrlAllowSuara.checked !== data.allow_suara) {
      elCtrlAllowSuara.checked = data.allow_suara;
    }
    if (data.force_buzzer && elCtrlForceBuzzer.value !== data.force_buzzer) {
      elCtrlForceBuzzer.value = data.force_buzzer;
      updateBuzzerSelectStyle(data.force_buzzer);
    }

    console.log('[Control] Sinkronisasi kontrol node_02:', data);
  });

  refPesertaControl.on('value', snap => {
    const data = snap.val();
    if (!data) return;

    if (typeof data.kamera_aktif === 'boolean' && elCtrlKameraAktif.checked !== data.kamera_aktif) {
      elCtrlKameraAktif.checked = data.kamera_aktif;
    }

    console.log('[Control] Sinkronisasi kontrol peserta_01:', data);
  });

  // ----- EVENT: Toggle Sensor Gerak (PIR) -----
  elCtrlAllowPir.addEventListener('change', () => {
    const val = elCtrlAllowPir.checked;
    refNodeControl.update({ allow_pir: val });
    addLogEntry({
      jenis: 'sistem',
      pesan: `🎛️ Sensor Gerak (PIR) → ${val ? 'AKTIF' : 'NONAKTIF'}`,
      timestamp: formatDateTime(new Date()),
      node_id: 'node_02'
    });
    console.log(`[Control] allow_pir → ${val}`);
  });

  // ----- EVENT: Toggle Sensor Suara -----
  elCtrlAllowSuara.addEventListener('change', () => {
    const val = elCtrlAllowSuara.checked;
    refNodeControl.update({ allow_suara: val });
    addLogEntry({
      jenis: 'sistem',
      pesan: `🎛️ Sensor Suara → ${val ? 'AKTIF' : 'NONAKTIF'}`,
      timestamp: formatDateTime(new Date()),
      node_id: 'node_02'
    });
    console.log(`[Control] allow_suara → ${val}`);
  });

  // ----- EVENT: Override Buzzer -----
  elCtrlForceBuzzer.addEventListener('change', () => {
    const val = elCtrlForceBuzzer.value;
    refNodeControl.update({ force_buzzer: val });
    updateBuzzerSelectStyle(val);

    const modeLabel = val === 'AUTO' ? 'OTOMATIS' : val === 'ON' ? 'PAKSA NYALA' : 'PAKSA MATI';
    addLogEntry({
      jenis: 'sistem',
      pesan: `🎛️ Override Buzzer → ${modeLabel} (${val})`,
      timestamp: formatDateTime(new Date()),
      node_id: 'node_02'
    });
    console.log(`[Control] force_buzzer → ${val}`);
  });

  // ----- EVENT: Toggle Kamera Peserta -----
  elCtrlKameraAktif.addEventListener('change', () => {
    const val = elCtrlKameraAktif.checked;
    refPesertaControl.update({ kamera_aktif: val });
    addLogEntry({
      jenis: 'sistem',
      pesan: `🎛️ Kamera peserta_01 → ${val ? 'NYALA' : 'MATI'}`,
      timestamp: formatDateTime(new Date()),
      peserta_id: 'peserta_01'
    });
    console.log(`[Control] kamera_aktif → ${val}`);
  });

  console.log('[Control] ✓ Panel kontrol terinisialisasi');
}

function updateBuzzerSelectStyle(mode) {
  elCtrlForceBuzzer.classList.remove('buzzer-auto', 'buzzer-on', 'buzzer-off');
  elCtrlForceBuzzer.classList.add(`buzzer-${mode.toLowerCase()}`);
}

// ============================================================
//  EVENT LISTENERS
// ============================================================

elBtnClear.addEventListener('click', () => {
  elLogContainer.innerHTML = '<div class="log-empty" id="log-empty"><span>Log dibersihkan</span></div>';
  totalLogCount = 0;
  elValLog.textContent = '0';
});

// ============================================================
//  STARTUP
// ============================================================

// Clock update setiap detik
setInterval(updateClock, 1000);
updateClock();

// Inisialisasi koneksi
if (DEMO_MODE) {
  console.warn('[App] Firebase belum dikonfigurasi → Mode Demo aktif');
  console.warn('[App] Edit variabel firebaseConfig di app.js untuk koneksi nyata');
  startDemoMode();
} else {
  initFirebase();
}

console.log('[SmartProctor] Dashboard diinisialisasi.');
