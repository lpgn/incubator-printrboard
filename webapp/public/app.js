const logEl = document.getElementById('log');
const connBadge = document.getElementById('conn-badge');
const cmdInput = document.getElementById('cmd-input');

const els = {
  temp: document.getElementById('val-temp'),
  humidity: document.getElementById('val-humidity'),
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

function connect() {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${proto}//${window.location.host}`);

  ws.onopen = () => {
    console.log('WS connected');
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
      console.log('WS caltable:', data);
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
  if (s.temp !== null && s.temp !== undefined) els.temp.textContent = s.temp.toFixed(1);
  if (s.humidity !== null && s.humidity !== undefined) els.humidity.textContent = s.humidity;
  if (s.heater !== null && s.heater !== undefined) els.heater.textContent = s.heater;
  if (s.fan !== null && s.fan !== undefined) els.fan.textContent = s.fan;
  if (s.state) {
    els.state.textContent = s.state;
    els.state.style.color = stateColor(s.state);
    if (s.state === 'ERROR') {
      els.stateReason.textContent = lastErrorReason || 'Unknown error';
    } else {
      els.stateReason.textContent = '';
      lastErrorReason = '';
    }
  }
  if (s.day !== null && s.totalDays !== null && s.totalDays !== undefined) {
    els.day.textContent = `${s.day} / ${s.totalDays}`;
  } else if (s.state === 'IDLE') {
    els.day.textContent = '-';
  }

  // Update calibration live display
  if (s.adc !== null && s.adc !== undefined) {
    console.log('Status adc:', s.adc);
    const adcEl = document.getElementById('cal-adc');
    if (adcEl) adcEl.textContent = s.adc;
  }
  if (s.temp !== null && s.temp !== undefined) {
    const ftEl = document.getElementById('cal-firmware-temp');
    if (ftEl) ftEl.textContent = s.temp.toFixed(1);
  }
  if (s.adcTarget !== null && s.adcTarget !== undefined) {
    const ttEl = document.getElementById('cal-target-temp');
    if (ttEl) ttEl.textContent = 'ADC=' + s.adcTarget;
  } else if (s.targetTemp !== null && s.targetTemp !== undefined) {
    const ttEl = document.getElementById('cal-target-temp');
    if (ttEl) ttEl.textContent = s.targetTemp.toFixed(1);
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
  const cmd = overrideActive ? 'override off' : 'override on';
  send(cmd);
}

function updateMaxTempLabel(val) {
  document.getElementById('maxtemp-val').textContent = parseFloat(val).toFixed(1) + '°C';
}

function sendMaxTemp() {
  const val = document.getElementById('maxtemp-range').value;
  send('set maxtemp ' + val);
}

function updateTurnStepsLabel() {
  const deg = Number(document.getElementById('turn-deg').value) || 0;
  const steps = Math.round(deg * 3200 / 360);
  document.getElementById('turn-steps').textContent = '(~' + steps + ' steps)';
}

function sendTurnDeg() {
  const deg = document.getElementById('turn-deg').value;
  send('set turn deg ' + deg);
}

function sendTurnRPM() {
  const rpm = document.getElementById('turn-rpm').value;
  send('set turn rpm ' + rpm);
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

function sendPID() {
  const kp = document.getElementById('pid-kp').value;
  const ki = document.getElementById('pid-ki').value;
  const kd = document.getElementById('pid-kd').value;
  send('set pid ' + kp + ' ' + ki + ' ' + kd);
}

function sendTargetTemp() {
  const val = document.getElementById('target-temp').value;
  send('set temp ' + val);
}

function sendAdcTarget() {
  const val = document.getElementById('target-adc').value;
  if (val === '') {
    appendLog('[ERR] Enter an ADC target value');
    return;
  }
  send('set adc ' + val);
}

function sendHumidityRange() {
  const lo = document.getElementById('humid-lo').value;
  const hi = document.getElementById('humid-hi').value;
  send('set humidity ' + lo + ' ' + hi);
}

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
          suggestedMin: 20,
          suggestedMax: 45,
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

  // Detect targetTemp from the incoming batch to backfill if needed
  let batchTarget = null;
  for (const p of points) {
    if (p.targetTemp != null) { batchTarget = p.targetTemp; break; }
  }
  // Backfill existing null target points so the line appears across the whole graph
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
  }

  const max = 400;
  if (labels.length > max) {
    const drop = labels.length - max;
    labels.splice(0, drop);
    d0.splice(0, drop);
    d1.splice(0, drop);
    d2.splice(0, drop);
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
    default: return '#e2e8f0';
  }
}

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
  // Keep log from growing forever
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

function updateCalTable(points) {
  console.log('updateCalTable called with', points);
  const tbody = document.getElementById('cal-tbody');
  if (!tbody) {
    console.warn('cal-tbody element not found');
    return;
  }
  tbody.innerHTML = '';
  if (!points || points.length === 0) {
    tbody.innerHTML = '<tr class="empty"><td colspan="3">No calibration points yet.</td></tr>';
    return;
  }
  for (let i = 0; i < points.length; i++) {
    const p = points[i];
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${i}</td><td>${p.adc}</td><td>${p.temp.toFixed(1)}</td>`;
    tbody.appendChild(tr);
  }
}

function addCalPoint() {
  const val = document.getElementById('cal-actual').value;
  if (val === '') {
    appendLog('[ERR] Enter the actual thermometer reading');
    return;
  }
  send('cal point ' + val);
  document.getElementById('cal-actual').value = '';
}

function escapeHtml(text) {
  const map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' };
  return text.replace(/[&<>"]/g, (c) => map[c]);
}

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

connect();
