const logEl = document.getElementById('log');
const connBadge = document.getElementById('conn-badge');
const cmdInput = document.getElementById('cmd-input');

const els = {
  temp: document.getElementById('val-temp'),
  humidity: document.getElementById('val-humidity'),
  heater: document.getElementById('val-heater'),
  fan: document.getElementById('val-fan'),
  state: document.getElementById('val-state'),
  day: document.getElementById('val-day'),
};

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
  }
  if (s.day !== null && s.totalDays !== null && s.totalDays !== undefined) {
    els.day.textContent = `${s.day} / ${s.totalDays}`;
  } else if (s.state === 'IDLE') {
    els.day.textContent = '-';
  }
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
