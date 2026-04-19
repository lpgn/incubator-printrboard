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
  uptime: document.getElementById('val-uptime')
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
let pidHistory = [];  // Local cache for PID auto-analysis
let currentPresets = [];

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
      // Accumulate points for PID analysis (cap same as server: 7200)
      for (const p of data.history) {
        pidHistory.push(p);
      }
      if (pidHistory.length > 7200) {
        pidHistory = pidHistory.slice(pidHistory.length - 7200);
      }
    } else if (data.type === 'presets') {
      renderPresets(data.presets);
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
  const debugEl = document.getElementById('debug-last-status');
  if (debugEl) debugEl.textContent = JSON.stringify(s, null, 2);

  if (s.temp !== null && s.temp !== undefined) {
    els.temp.textContent = (s.temp === -999.0) ? 'ERR' : s.temp.toFixed(1);
  }
  if (s.humidity !== null && s.humidity !== undefined) els.humidity.textContent = s.humidity;
  if (s.dhtTemp !== null && s.dhtTemp !== undefined) els.dhtTemp.textContent = s.dhtTemp.toFixed(1);
  if (s.heater !== null && s.heater !== undefined) els.heater.textContent = s.heater + '%';
  if (s.fan !== null && s.fan !== undefined) els.fan.textContent = s.fan + '%';

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
    if (s.state !== 'IDLE') {
      const headerDay = document.getElementById('header-day');
      if (headerDay) headerDay.textContent = `Day ${s.day} / ${s.totalDays}`;
    }
  } else if (s.state === 'IDLE') {
    const headerDay = document.getElementById('header-day');
    if (headerDay) headerDay.textContent = 'Day -';
  }

  // Update Turner Angle display
  if (s.turnDeg !== null && s.turnDeg !== undefined) {
    const degInput = document.getElementById('turn-deg');
    const degHint = document.getElementById('turn-deg-hint');
    if (degInput) degInput.value = s.turnDeg;
    if (degHint) degHint.textContent = s.turnsCompleted !== null ?
      `Turn ${s.turnsCompleted}/${s.turnsPerDay} today` : 'Live from firmware';
  }

  // Update Uptime Display
  if (s.uptime !== null && s.uptime !== undefined) {
    if (s.state !== 'IDLE') {
      const hours = Math.floor(s.uptime / 3600);
      const minutes = Math.floor((s.uptime % 3600) / 60);
      els.uptime.textContent = `${hours}h ${minutes}m`;
    } else {
      els.uptime.textContent = '';
    }
  }

  // ADC target mode badge in settings
  const adcBadge = document.getElementById('adc-mode-badge');
  if (s.adcTarget !== null && s.adcTarget !== undefined) {
    if (adcBadge) {
      adcBadge.textContent = 'ADC Mode (target ' + s.adcTarget + ')';
      adcBadge.style.color = 'var(--accent)';
    }
  } else if (s.targetTemp !== null && s.targetTemp !== undefined) {
    if (adcBadge) {
      adcBadge.textContent = 'Temp Mode';
      adcBadge.style.color = 'var(--muted)';
    }
  }

  // Update Smart Toggles
  const btnStart = document.getElementById('btn-toggle-start');
  const btnPause = document.getElementById('btn-toggle-pause');
  if (btnStart && btnPause && s.hasOwnProperty('state')) {
    const isRunning = (s.state !== 'IDLE' && s.state !== 'DONE' && s.state !== 'ERROR');
    if (isRunning) {
      btnStart.textContent = '■ Stop';
      btnStart.className = 'btn btn-danger';
      window._currentStartAction = 'stop';
    } else {
      btnStart.textContent = '▶ Start';
      btnStart.className = 'btn btn-success';
      window._currentStartAction = 'start';
    }

    if (s.state === 'PAUSED') {
      btnPause.textContent = '⏵ Resume';
      btnPause.className = 'btn btn-primary';
      window._currentPauseAction = 'resume';
    } else {
      btnPause.textContent = '⏸ Pause';
      btnPause.className = 'btn';
      window._currentPauseAction = 'pause';
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

function sendTurnDeg() {
  // Degrees per turn is hardcoded to 55° — nothing to send
}

function sendTurnRPM() {
  const rpm = document.getElementById('turn-rpm').value;
  if (rpm !== '') send('set turn rpm ' + rpm);
}

function sendTurnsPerDay() {
  const turns = parseInt(document.getElementById('turns-per-day').value, 10);
  if (isNaN(turns)) return;
  if (turns % 2 === 0) {
    alert('Turns per day must be an odd number.');
    return;
  }
  send('set turns ' + turns);
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

function toggleStart() {
  if (window._currentStartAction === 'stop') {
    send('stop');
  } else {
    // Auto-sync RTC from browser time before starting incubation
    syncRTC();
    // Small delay so RTC command is processed before start
    setTimeout(() => send('start'), 300);
  }
}

function togglePause() {
  if (window._currentPauseAction) send(window._currentPauseAction);
  else send('pause');
}

function syncRTC() {
  const now = new Date();
  const y = now.getFullYear();
  const mo = String(now.getMonth() + 1).padStart(2, '0');
  const d = String(now.getDate()).padStart(2, '0');
  const h = String(now.getHours()).padStart(2, '0');
  const mi = String(now.getMinutes()).padStart(2, '0');
  const s = String(now.getSeconds()).padStart(2, '0');
  send(`time set ${y} ${mo} ${d} ${h} ${mi} ${s}`);
}

function sendSetDay() {
  const val = document.getElementById('set-day').value;
  if (val !== '') {
    send('set day ' + val);
    // Auto-save after setting day
    setTimeout(() => send('save'), 500);
  }
}

function sendSetElapsed() {
  const val = document.getElementById('set-elapsed').value;
  if (val !== '') {
    send('set elapsed ' + val);
    // Auto-save after setting elapsed
    setTimeout(() => send('save'), 500);
  }
}

function sendThermistor() {
  const r25 = document.getElementById('therm-r25').value;
  const beta = document.getElementById('therm-beta').value;
  if (r25 === '' || beta === '') {
    appendLog('[ERR] Enter both R25 and Beta values');
    return;
  }
  send('set thermistor ' + r25 + ' ' + beta);
}

function sendTestHeater() {
  const pwm = document.getElementById('test-heater-pwm').value;
  send('test heater ' + pwm);
}

function analyzeLogsForPID() {
  const msgEl = document.getElementById('pid-analysis-msg');
  if (!msgEl) return;
  
  if (pidHistory.length < 60) {
    msgEl.textContent = 'Not enough data (needs ~3 mins of logs) ⏳';
    return;
  }

  msgEl.textContent = 'Analyzing...';

  // Grab the last 15 minutes max
  let cutoff = new Date(Date.now() - 15 * 60000);
  let relevant = pidHistory.filter(h => new Date(h.t) >= cutoff);
  
  // Check if system has a target set
  const target = relevant[relevant.length - 1].targetTemp;
  if (!target) {
    msgEl.textContent = 'No target temp set 🚫';
    return;
  }

  // Smooth the temperature array to ignore tiny sensor noise (5-point moving average)
  let smoothed = [];
  for(let i=0; i<relevant.length; i++) {
    let sum = 0;
    let count = 0;
    for(let j=Math.max(0, i-2); j<=Math.min(relevant.length-1, i+2); j++) {
      if (relevant[j].temp != null) {
        sum += relevant[j].temp;
        count++;
      }
    }
    smoothed.push(count > 0 ? sum/count : 0);
  }

  // Find peaks and valleys looking left/right. 
  // Window of 15 seconds means a cycle must take at least 30s to be recognized as a distinct wave.
  let peaks = [];
  let valleys = [];
  let window = 15; 
  for(let i=window; i<smoothed.length-window; i++) {
    let isPeak = true;
    let isValley = true;
    for(let j=i-window; j<=i+window; j++) {
      if (i===j) continue;
      if (smoothed[j] >= smoothed[i]) isPeak = false;
      if (smoothed[j] <= smoothed[i]) isValley = false;
    }
    if (isPeak) peaks.push({idx: i, t: new Date(relevant[i].t).getTime(), val: smoothed[i]});
    if (isValley) valleys.push({idx: i, t: new Date(relevant[i].t).getTime(), val: smoothed[i]});
  }

  if (peaks.length < 2 || valleys.length < 2) {
    msgEl.innerHTML = 'System looks stable (no wave pattern found) ✅';
    return;
  }

  // Find average Amplitude
  let avgPeak = peaks.reduce((a, b) => a + b.val, 0) / peaks.length;
  let avgValley = valleys.reduce((a, b) => a + b.val, 0) / valleys.length;
  let tempAmplitude = (avgPeak - avgValley) / 2;

  if (tempAmplitude < 0.15) {
    msgEl.innerHTML = 'Stable: Oscillation is less than 0.15°C ✅';
    return;
  }

  // Find Period (Pu) in seconds
  let periods = [];
  for(let i=1; i<peaks.length; i++) periods.push((peaks[i].t - peaks[i-1].t)/1000);
  for(let i=1; i<valleys.length; i++) periods.push((valleys[i].t - valleys[i-1].t)/1000);
  let Pu = periods.reduce((a, b) => a + b, 0) / periods.length;

  // Find Heater Amplitude
  let hMax = 0;
  let hMin = 100;
  for(let i=0; i<relevant.length; i++) {
    if (relevant[i].heater > hMax) hMax = relevant[i].heater;
    if (relevant[i].heater < hMin) hMin = relevant[i].heater;
  }
  
  if (hMax === hMin) {
    msgEl.innerHTML = 'Heater is stuck at ' + hMax + '% (Cannot tune)';
    return;
  }

  // Ku calculation: the heater output is typically displayed 0-100%, 
  // but firmware PID operates on 0-255 scale.
  // We need Ku based on firmware scale because the user applies Kp to the firmware
  let dpwm = ((hMax - hMin) / 100.0) * 255.0;
  let heaterAmplitude = dpwm / 2.0;

  // Ultimate Gain (Relay feedback formula)
  let Ku = (4 * heaterAmplitude) / (Math.PI * tempAmplitude);

  // Tyreus-Luyben Tuning (Excellent limits for slow, lagging systems like incubators)
  let Kp = Ku / 2.2;
  let Ti = 2.2 * Pu;
  let Td = Pu / 6.3;

  // Firmware implements: Output = Kp*Err + Int(Ki*Err) - Kd*d(T)
  // Converting standard Ti/Td to Ki/Kd for a 1-second sample time loop:
  let Ki = Kp / Ti;
  let Kd = Kp * Td;

  msgEl.innerHTML = `Found Oscillation! (Amplitude: <b>±${tempAmplitude.toFixed(2)}°C</b>, Period: <b>${Math.round(Pu)}s</b>).<br>` + 
                    `Press Set PID to apply the values below.`;
  msgEl.style.color = "var(--warn)";

  // Populate inputs
  document.getElementById('pid-kp').value = Math.max(0.1, Kp).toFixed(2);
  document.getElementById('pid-ki').value = Math.max(0.001, Ki).toFixed(3);
  document.getElementById('pid-kd').value = Math.max(0, Kd).toFixed(0);
}

// --- Species Presets ---
function renderPresets(presets) {
  currentPresets = presets;
  const tbody = document.querySelector('#presets-table tbody');
  if (!tbody) return;
  tbody.innerHTML = '';

  for (const p of presets) {
    const tr = document.createElement('tr');
    tr.dataset.idx = p.idx;
    const tempC = (p.temp / 10).toFixed(1);

    tr.innerHTML = `
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${p.idx}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${escapeHtml(p.name)}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${p.days}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${p.stop}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${tempC}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${p.humidLo} – ${p.humidHi}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${p.lockLo} – ${p.lockHi}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${p.turns}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">${p.deg}</td>
      <td style="padding:8px; border-bottom:1px solid rgba(255,255,255,0.05);">
        <button class="btn" onclick="editPresetRow(${p.idx})">Edit</button>
      </td>
    `;
    tbody.appendChild(tr);
  }
}

function editPresetRow(idx) {
  const tr = document.querySelector(`#presets-table tbody tr[data-idx="${idx}"]`);
  if (!tr) return;
  const p = currentPresets.find(pr => pr.idx === idx);
  if (!p) return;

  const tempC = (p.temp / 10).toFixed(1);
  const cells = tr.children;

  cells[1].innerHTML = `<input type="text" class="preset-input" value="${escapeHtml(p.name)}" style="width:90px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[2].innerHTML = `<input type="number" class="preset-input" value="${p.days}" min="1" max="60" style="width:60px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[3].innerHTML = `<input type="number" class="preset-input" value="${p.stop}" min="1" max="60" style="width:60px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[4].innerHTML = `<input type="number" class="preset-input" value="${tempC}" min="30" max="42" step="0.1" style="width:60px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[5].innerHTML = `<input type="number" class="preset-input" value="${p.humidLo}" min="20" max="90" style="width:48px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;"> – <input type="number" class="preset-input" value="${p.humidHi}" min="20" max="90" style="width:48px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[6].innerHTML = `<input type="number" class="preset-input" value="${p.lockLo}" min="20" max="90" style="width:48px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;"> – <input type="number" class="preset-input" value="${p.lockHi}" min="20" max="90" style="width:48px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[7].innerHTML = `<input type="number" class="preset-input" value="${p.turns}" min="1" max="24" style="width:60px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[8].innerHTML = `<input type="number" class="preset-input" value="${p.deg}" min="15" max="360" style="width:60px; background:rgba(0,0,0,0.3); color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px 8px; font-family:inherit; font-size:0.9rem;">`;
  cells[9].innerHTML = `
    <button class="btn btn-success" onclick="savePreset(${idx})">Save</button>
    <button class="btn" onclick="cancelPresetEdit(${idx})">Cancel</button>
  `;
}

function savePreset(idx) {
  const tr = document.querySelector(`#presets-table tbody tr[data-idx="${idx}"]`);
  if (!tr) return;
  const p = currentPresets.find(pr => pr.idx === idx);
  if (!p) return;

  const inputs = tr.querySelectorAll('input');
  const name = inputs[0].value.trim();
  const days = parseInt(inputs[1].value, 10);
  const stop = parseInt(inputs[2].value, 10);
  const temp = Math.round(parseFloat(inputs[3].value) * 10);
  const humidLo = parseInt(inputs[4].value, 10);
  const humidHi = parseInt(inputs[5].value, 10);
  const lockLo = parseInt(inputs[6].value, 10);
  const lockHi = parseInt(inputs[7].value, 10);
  const turns = parseInt(inputs[8].value, 10);
  const deg = parseInt(inputs[9].value, 10);

  const presetName = p.name;

  if (name !== p.name) send(`preset ${presetName} name ${name}`);
  if (days !== p.days) send(`preset ${presetName} days ${days}`);
  if (stop !== p.stop) send(`preset ${presetName} stop ${stop}`);
  if (temp !== p.temp) send(`preset ${presetName} temp ${temp}`);
  if (humidLo !== p.humidLo) send(`preset ${presetName} humidlo ${humidLo}`);
  if (humidHi !== p.humidHi) send(`preset ${presetName} humidhi ${humidHi}`);
  if (lockLo !== p.lockLo) send(`preset ${presetName} locklo ${lockLo}`);
  if (lockHi !== p.lockHi) send(`preset ${presetName} lockhi ${lockHi}`);
  if (turns !== p.turns) send(`preset ${presetName} turns ${turns}`);
  if (deg !== p.deg) send(`preset ${presetName} deg ${deg}`);

  cancelPresetEdit(idx);
}

function cancelPresetEdit(idx) {
  renderPresets(currentPresets);
}

document.addEventListener('DOMContentLoaded', () => {
  const chickBtn = document.getElementById('btn-species-chicken');
  if (chickBtn) chickBtn.style.borderColor = 'var(--accent)';
});

connect();
