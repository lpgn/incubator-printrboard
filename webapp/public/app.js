const logEl = document.getElementById('log');
const connBadge = document.getElementById('conn-badge');
const cmdInput = document.getElementById('cmd-input');

const els = {
  temp: document.getElementById('val-temp'),
  humidity: document.getElementById('val-humidity'),
  dhtTemp: document.getElementById('val-dht-temp'),
  heater: document.getElementById('val-heater'),
  fan: document.getElementById('val-fan'),
  state: document.getElementById('val-state'),
  stateReason: document.getElementById('val-state-reason'),
  day: document.getElementById('val-day'),
};

const ledMap = {
  sensorFail: { el: document.getElementById('led-sensorFail'), style: 'danger' },
  overTemp:   { el: document.getElementById('led-overTemp'),   style: 'danger' },
  underTemp:  { el: document.getElementById('led-underTemp'),  style: 'warn' },
  dhtFail:    { el: document.getElementById('led-dhtFail'),    style: 'info' },
  humidHigh:  { el: document.getElementById('led-humidHigh'),  style: 'info' },
  humidLow:   { el: document.getElementById('led-humidLow'),   style: 'info' },
  errorState: { el: document.getElementById('led-errorState'), style: 'danger' },
};

let overrideActive = false;
let lastErrorReason = '';
let tempChart = null;
let ws;
let reconnectTimer;

// --- Tabs ---
function showTab(name) {
  document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  const panel = document.getElementById('tab-' + name);
  const btn = document.querySelector('.tab-btn[data-tab="' + name + '"]');
  if (panel) panel.classList.add('active');
  if (btn) btn.classList.add('active');
}

// --- WebSocket ---
function connect() {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${proto}//${window.location.host}`);

  ws.onopen = () => {
    clearTimeout(reconnectTimer);
  };

  ws.onmessage = (ev) => {
    const data = JSON.parse(ev.data);
    if (data.type === 'connection') {
      updateConnection(data.connected);
    } else if (data.type === 'status') {
      updateStatus(data);
    } else if (data.type === 'alarms') {
      updateAlarms(data.alarms);
    } else if (data.type === 'history') {
      if (!tempChart) initChart();
      appendHistory(data.history);
    } else if (data.type === 'caltable') {
      updateCalTable(data.points);
    } else if (data.type === 'log') {
      appendLog(data.line);
    }
  };

  ws.onclose = () => {
    updateConnection(false);
    reconnectTimer = setTimeout(connect, 2000);
  };

  ws.onerror = () => {
    updateConnection(false);
  };
}

function updateConnection(connected) {
  connBadge.textContent = connected ? 'Connected' : 'Disconnected';
  connBadge.className = 'badge ' + (connected ? 'connected' : 'disconnected');
}

function updateStatus(s) {
  if (s.temp !== null && s.temp !== undefined) {
    els.temp.textContent = (s.temp === -999.0) ? 'ERR' : s.temp.toFixed(1);
  }
  if (s.humidity !== null && s.humidity !== undefined) els.humidity.textContent = s.humidity;
  if (s.dhtTemp !== null && s.dhtTemp !== undefined) els.dhtTemp.textContent = s.dhtTemp.toFixed(1);
  if (s.heater !== null && s.heater !== undefined) els.heater.textContent = s.heater > 0 ? 'ON' : 'OFF';
  if (s.fan !== null && s.fan !== undefined) els.fan.textContent = s.fan > 0 ? 'ON' : 'OFF';

  if (s.state) {
    els.state.textContent = s.state;
    els.state.style.color = stateColor(s.state);
    const headerState = document.getElementById('header-state');
    if (headerState) {
      headerState.textContent = s.state;
      headerState.style.color = stateColor(s.state);
    }
    if (s.state === 'ERROR') {
      els.stateReason.textContent = lastErrorReason || 'Unknown error';
    } else {
      els.stateReason.textContent = '';
      lastErrorReason = '';
    }
  }

  if (s.day !== null && s.totalDays !== null && s.totalDays !== undefined) {
    els.day.textContent = `${s.day} / ${s.totalDays}`;
    const headerDay = document.getElementById('header-day');
    if (headerDay) headerDay.textContent = `Day ${s.day} / ${s.totalDays}`;
  } else if (s.state === 'IDLE') {
    els.day.textContent = '-';
    const headerDay = document.getElementById('header-day');
    if (headerDay) headerDay.textContent = 'Day -';
  }

  // Calibration live display
  if (s.adc !== null && s.adc !== undefined) {
    const adcEl = document.getElementById('cal-adc');
    if (adcEl) adcEl.textContent = s.adc;
  }
  if (s.temp !== null && s.temp !== undefined) {
    const ftEl = document.getElementById('cal-firmware-temp');
    if (ftEl) ftEl.textContent = s.temp.toFixed(1);
  }

  // Target display
  const calTarget = document.getElementById('cal-target');
  const calMode = document.getElementById('cal-mode');
  const adcBadge = document.getElementById('adc-mode-badge');
  if (s.adcTarget !== null && s.adcTarget !== undefined) {
    if (calTarget) calTarget.textContent = 'ADC ' + s.adcTarget;
    if (calMode) calMode.textContent = 'ADC Mode';
    if (adcBadge) {
      adcBadge.textContent = 'ADC Mode (target ' + s.adcTarget + ')';
      adcBadge.style.color = 'var(--accent)';
    }
  } else if (s.targetTemp !== null && s.targetTemp !== undefined) {
    if (calTarget) calTarget.textContent = s.targetTemp.toFixed(1) + '°C';
    if (calMode) calMode.textContent = 'Temp Mode';
    if (adcBadge) {
      adcBadge.textContent = 'Temp Mode';
      adcBadge.style.color = 'var(--muted)';
    }
  }
}

function updateAlarms(alarms) {
  for (const [key, cfg] of Object.entries(ledMap)) {
    if (alarms[key]) {
      cfg.el.classList.add('on', cfg.style);
    } else {
      cfg.el.classList.remove('on', 'danger', 'warn', 'info');
    }
  }
  if (alarms.overridden !== undefined) {
    overrideActive = alarms.overridden;
    const btn = document.getElementById('btn-override');
    if (overrideActive) {
      btn.textContent = 'Override Active — Click to Disable';
      btn.classList.add('active');
    } else {
      btn.textContent = 'Override Errors';
      btn.classList.remove('active');
    }
  }
}

function toggleOverride() {
  send(overrideActive ? 'override off' : 'override on');
}

// --- Chart ---
function initChart() {
  const ctx = document.getElementById('tempChart').getContext('2d');
  const gradTemp = ctx.createLinearGradient(0, 0, 0, 300);
  gradTemp.addColorStop(0, 'rgba(239,68,68,0.45)');
  gradTemp.addColorStop(1, 'rgba(239,68,68,0.02)');
  const gradHum = ctx.createLinearGradient(0, 0, 0, 300);
  gradHum.addColorStop(0, 'rgba(56,189,248,0.35)');
  gradHum.addColorStop(1, 'rgba(56,189,248,0.02)');

  Chart.defaults.color = '#94a3b8';
  Chart.defaults.borderColor = '#334155';

  tempChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [
        {
          label: 'Temperature (°C)',
          data: [],
          borderColor: '#ef4444',
          backgroundColor: gradTemp,
          borderWidth: 2,
          tension: 0.4,
          fill: true,
          pointRadius: 0,
          pointHoverRadius: 5,
          yAxisID: 'y'
        },
        {
          label: 'Humidity (%)',
          data: [],
          borderColor: '#38bdf8',
          backgroundColor: gradHum,
          borderWidth: 2,
          tension: 0.4,
          fill: true,
          pointRadius: 0,
          pointHoverRadius: 5,
          yAxisID: 'y1'
        },
        {
          label: 'Target Temp (°C)',
          data: [],
          borderColor: '#f59e0b',
          borderWidth: 2,
          borderDash: [6, 6],
          fill: false,
          pointRadius: 0,
          pointHoverRadius: 0,
          yAxisID: 'y',
          tension: 0
        },
        {
          label: 'DHT Temp (°C)',
          data: [],
          borderColor: '#10b981',
          borderWidth: 2,
          fill: false,
          pointRadius: 0,
          pointHoverRadius: 5,
          yAxisID: 'y',
          tension: 0.4
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 0 },
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: {
          labels: { color: '#e2e8f0', font: { size: 12 } }
        },
        tooltip: {
          backgroundColor: 'rgba(15,23,42,0.95)',
          titleColor: '#e2e8f0',
          bodyColor: '#cbd5e1',
          borderColor: '#334155',
          borderWidth: 1
        }
      },
      scales: {
        x: {
          ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 8 },
          grid: { color: '#1e293b' }
        },
        y: {
          type: 'linear',
          display: true,
          position: 'left',
          min: 0,
          max: 50,
          grid: { color: '#1e293b' }
        },
        y1: {
          type: 'linear',
          display: true,
          position: 'right',
          suggestedMin: 0,
          suggestedMax: 100,
          grid: { drawOnChartArea: false }
        }
      }
    }
  });
}

function appendHistory(points) {
  if (!tempChart) return;
  const labels = tempChart.data.labels;
  const d0 = tempChart.data.datasets[0].data;
  const d1 = tempChart.data.datasets[1].data;
  const d2 = tempChart.data.datasets[2].data;
  const d3 = tempChart.data.datasets[3].data;

  let batchTarget = null;
  for (const p of points) {
    if (p.targetTemp != null) { batchTarget = p.targetTemp; break; }
  }
  if (batchTarget != null) {
    for (let i = 0; i < d2.length; i++) {
      if (d2[i] == null) d2[i] = batchTarget;
    }
  }

  for (const p of points) {
    const t = new Date(p.t);
    const label = t.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    labels.push(label);
    d0.push(p.temp);
    d1.push(p.humidity);
    d2.push(p.targetTemp != null ? p.targetTemp : null);
    d3.push(p.dhtTemp != null ? p.dhtTemp : null);
  }

  const max = 2880;
  if (labels.length > max) {
    const drop = labels.length - max;
    labels.splice(0, drop);
    d0.splice(0, drop);
    d1.splice(0, drop);
    d2.splice(0, drop);
    d3.splice(0, drop);
  }

  tempChart.update('none');
}

function stateColor(state) {
  switch (state) {
    case 'IDLE': return '#94a3b8';
    case 'PREHEATING': return '#f59e0b';
    case 'INCUBATING': return '#22c55e';
    case 'LOCKDOWN': return '#38bdf8';
    case 'HATCHING': return '#a78bfa';
    case 'DONE': return '#22c55e';
    case 'ERROR': return '#ef4444';
    case 'AUTOTUNE': return '#eab308';
    default: return '#e2e8f0';
  }
}

// --- Log ---
function appendLog(line) {
  const errMatch = line.match(/ERROR STATE: (.+?) !!!/);
  if (errMatch) {
    lastErrorReason = errMatch[1];
    if (els.state.textContent === 'ERROR') {
      els.stateReason.textContent = lastErrorReason;
    }
  }
  if (line.includes('RECOVERED from error')) {
    lastErrorReason = '';
    els.stateReason.textContent = '';
  }

  const time = new Date().toLocaleTimeString();
  const div = document.createElement('div');
  div.className = 'line';
  div.innerHTML = `<span class="time">${time}</span>${escapeHtml(line)}`;
  logEl.appendChild(div);
  logEl.scrollTop = logEl.scrollHeight;
  while (logEl.children.length > 300) {
    logEl.removeChild(logEl.firstChild);
  }

  // Collect generated C code into calibration panel
  const codeEl = document.getElementById('cal-code');
  if (codeEl) {
    if (line.includes('HARDCODED CALIBRATION TABLE')) {
      codeEl.textContent = '';
      codeEl.dataset.collecting = 'true';
    }
    if (codeEl.dataset.collecting === 'true') {
      codeEl.textContent += line + '\n';
      if (line.includes('=================================================')) {
        codeEl.dataset.collecting = 'false';
      }
    }
  }
}

function escapeHtml(text) {
  const map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' };
  return text.replace(/[&<>"]/g, (c) => map[c]);
}

// --- Commands ---
function send(command) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ command }));
  } else {
    appendLog('[ERR] WebSocket not connected');
  }
}

function sendInput() {
  const val = cmdInput.value.trim();
  if (!val) return;
  send(val);
  cmdInput.value = '';
}

cmdInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') sendInput();
});

// --- Settings helpers ---
function sendTargetTemp() {
  const val = document.getElementById('target-temp').value;
  if (val !== '') send('set temp ' + val);
}

function sendAdcTarget() {
  const val = document.getElementById('target-adc').value;
  if (val === '') {
    appendLog('[ERR] Enter an ADC target value');
    return;
  }
  send('set adc ' + val);
}

function sendMaxTemp() {
  const val = document.getElementById('maxtemp-range').value;
  send('set maxtemp ' + val);
}

function sendHumidityRange() {
  const lo = document.getElementById('humid-lo').value;
  const hi = document.getElementById('humid-hi').value;
  send('set humidity ' + lo + ' ' + hi);
}

function updateTurnStepsLabel() {
  const deg = Number(document.getElementById('turn-deg').value) || 0;
  const steps = Math.round(deg * 3200 / 360);
  document.getElementById('turn-steps').textContent = '(~' + steps + ' steps)';
}

function sendTurnDeg() {
  const deg = document.getElementById('turn-deg').value;
  if (deg !== '') send('set turn deg ' + deg);
}

function sendTurnRPM() {
  const rpm = document.getElementById('turn-rpm').value;
  if (rpm !== '') send('set turn rpm ' + rpm);
}

function sendTurnsPerDay() {
  const turns = document.getElementById('turns-per-day').value;
  if (turns !== '') send('set turns ' + turns);
}

function sendPID() {
  const kp = document.getElementById('pid-kp').value;
  const ki = document.getElementById('pid-ki').value;
  const kd = document.getElementById('pid-kd').value;
  send('set pid ' + kp + ' ' + ki + ' ' + kd);
}

function sendPreheatMax() {
  const pwm = document.getElementById('preheat-range').value;
  send('set preheat ' + pwm);
}

function sendCalTemp() {
  const val = document.getElementById('trusted-temp').value;
  if (val === '') {
    appendLog('[ERR] Enter a temperature value');
    return;
  }
  send('cal temp actual ' + val);
}

function updateCustomHumid() {
  const lo = document.getElementById('custom-humid-lo').value;
  const hi = document.getElementById('custom-humid-hi').value;
  send('custom humid ' + lo + ' ' + hi);
}

function updateCustomLock() {
  const lo = document.getElementById('custom-lock-lo').value;
  const hi = document.getElementById('custom-lock-hi').value;
  send('custom lock ' + lo + ' ' + hi);
}

// --- Calibration ---
function updateCalTable(points) {
  const tbody = document.getElementById('cal-tbody');
  if (!tbody) return;
  tbody.innerHTML = '';
  if (!points || points.length === 0) {
    tbody.innerHTML = '<tr class="empty"><td colspan="3">No calibration points yet.</td></tr>';
    window.currentCalPoints = [];
    return;
  }

  window.currentCalPoints = [...points];

  for (let i = 0; i < points.length; i++) {
    const p = points[i];
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${i}</td>
      <td><input type="number" class="cal-input" step="1" value="${p.adc}" onchange="updateCalPointLocal(${i}, 'adc', this.value)"></td>
      <td><input type="number" class="cal-input" step="0.1" value="${p.temp.toFixed(1)}" onchange="updateCalPointLocal(${i}, 'temp', this.value)"></td>`;
    tbody.appendChild(tr);
  }
  
  // Make sure the generated code output is always in sync with the table when it updates
  generateLocalCode();
}

function updateCalPointLocal(idx, field, val) {
  if (window.currentCalPoints && window.currentCalPoints[idx]) {
    window.currentCalPoints[idx][field] = Number(val);
    generateLocalCode();
  }
}

function addEmptyLocalPoint() {
  if (!window.currentCalPoints) window.currentCalPoints = [];
  window.currentCalPoints.push({adc: 0, temp: 0.0});
  updateCalTable(window.currentCalPoints);
}

function generateLocalCode() {
  const codeEl = document.getElementById('cal-code');
  if (!codeEl) return;
  const pts = window.currentCalPoints || [];
  if (pts.length === 0) {
    codeEl.textContent = "// No points available to generate code.";
    return;
  }
  
  pts.sort((a,b) => a.adc - b.adc);

  let out = "// ========== HARDCODED CALIBRATION TABLE ==========\n";
  out += "// Paste this block into src/heater.cpp, then set\n";
  out += "// #define USE_HARDCODED_CAL_TABLE 1 in include/heater.h\n\n";
  out += `static const uint8_t hardcodedCalCount = ${pts.length};\n`;
  out += "static const CalibrationPoint PROGMEM hardcodedCalTable[] = {\n";
  for (let i=0; i<pts.length; i++) {
     out += `    {${pts[i].adc}, ${pts[i].temp.toFixed(1)}f}`;
     if (i < pts.length - 1) out += ",";
     out += "\n";
  }
  out += "};\n";
  out += "// =================================================\n";
  
  codeEl.textContent = out;
}

function addCalPoint() {
  const val = document.getElementById('cal-actual').value;
  if (val === '') {
    appendLog('[ERR] Enter the actual thermometer reading');
    return;
  }
  send('cal point ' + val);
  
  // Explicitly command table refresh from frontend because backend node may not have restarted yet
  setTimeout(() => send('cal table'), 1000);
}

function clearCalPoints() {
  send('cal clear points');
  setTimeout(() => send('cal table'), 1000);
}

connect();
