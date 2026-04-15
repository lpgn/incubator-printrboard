const express = require('express');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const WebSocket = require('ws');
const http = require('http');
const path = require('path');

const PORT = process.env.PORT || 3000;
const SERIAL_PATH = process.env.SERIAL_PORT || 'COM8';
const SERIAL_BAUD = parseInt(process.env.SERIAL_BAUD || '115200', 10);

const app = express();
app.use(express.static(path.join(__dirname, 'public')));

const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

let lastStatus = {
  day: null,
  totalDays: null,
  temp: null,
  humidity: null,
  heater: null,
  fan: null,
  state: null,
  connected: false
};

let lastAlarms = {
  sensorFail: false,
  overTemp: false,
  underTemp: false,
  dhtFail: false,
  humidHigh: false,
  humidLow: false,
  errorState: false,
  overridden: false
};

const MAX_HISTORY = 7200;
let history = []; // { t: ISOString, temp, humidity, heater, fan }

function broadcast(data) {
  const msg = JSON.stringify(data);
  wss.clients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(msg);
    }
  });
}

function parseAlarms(line) {
  if (line.includes('ALARM: Thermistor sensor FAILED')) lastAlarms.sensorFail = true;
  if (line.includes('ALARM: OVER-TEMP')) lastAlarms.overTemp = true;
  if (line.includes('WARNING: Under-temp')) lastAlarms.underTemp = true;
  if (line.includes('DHT22 sensor read failures')) lastAlarms.dhtFail = true;
  if (line.includes('WARNING: Humidity HIGH')) lastAlarms.humidHigh = true;
  if (line.includes('WARNING: Humidity LOW')) lastAlarms.humidLow = true;
  if (line.includes('ERROR STATE')) lastAlarms.errorState = true;
  if (line.includes('RECOVERED from error')) lastAlarms.errorState = false;
  if (line.includes('Safety override ENABLED')) lastAlarms.overridden = true;
  if (line.includes('Safety override DISABLED')) lastAlarms.overridden = false;
  if (line.includes('Reset complete')) {
    lastAlarms = {
      sensorFail: false, overTemp: false, underTemp: false,
      dhtFail: false, humidHigh: false, humidLow: false,
      errorState: false, overridden: lastAlarms.overridden
    };
  }
  // Clear alarms when explicitly cleared or state goes normal
  if (line.includes('>> STOPPED.') || line.includes('>> RESUMED.')) {
    lastAlarms.errorState = false;
  }
}

function parseStatusLine(line) {
  let result = null;

  // Match: [DAY 01/21] T=21.8C H=0% HTR=0% FAN=29% STATE=ERROR
  const dayMatch = line.match(/\[DAY\s+(\d+)\/(\d+)\]\s+T=([\d.-]+)C\s+H=(\d+)%\s+HTR=(\d+)%\s+FAN=(\d+)%\s+STATE=(\S+)/);
  if (dayMatch) {
    result = {
      type: 'status',
      day: parseInt(dayMatch[1], 10),
      totalDays: parseInt(dayMatch[2], 10),
      temp: parseFloat(dayMatch[3]),
      humidity: parseInt(dayMatch[4], 10),
      heater: parseInt(dayMatch[5], 10),
      fan: parseInt(dayMatch[6], 10),
      state: dayMatch[7]
    };
  }

  // Match: [IDLE] T=21.8C H=0%
  const idleMatch = line.match(/\[IDLE\]\s+T=([\d.-]+)C\s+H=(\d+)%/);
  if (idleMatch) {
    result = {
      type: 'status',
      day: 0,
      totalDays: 0,
      temp: parseFloat(idleMatch[1]),
      humidity: parseInt(idleMatch[2], 10),
      heater: 0,
      fan: 0,
      state: 'IDLE'
    };
  }

  if (result) {
    const point = {
      t: new Date().toISOString(),
      temp: result.temp,
      humidity: result.humidity,
      heater: result.heater,
      fan: result.fan,
      state: result.state
    };
    history.push(point);
    if (history.length > MAX_HISTORY) history.shift();
    broadcast({ type: 'history', history: [point] });
  }

  return result;
}

let port = null;
let parser = null;

function openSerial() {
  console.log(`Opening serial port ${SERIAL_PATH} @ ${SERIAL_BAUD} baud...`);
  port = new SerialPort({
    path: SERIAL_PATH,
    baudRate: SERIAL_BAUD,
    autoOpen: false
  });

  port.open((err) => {
    if (err) {
      console.error('Serial open error:', err.message);
      lastStatus.connected = false;
      broadcast({ type: 'connection', connected: false, error: err.message });
      setTimeout(openSerial, 3000);
      return;
    }

    // Force DTR active (same as pio device monitor)
    port.set({ dtr: true }, (dtrErr) => {
      if (dtrErr) console.error('DTR set error:', dtrErr.message);
    });

    console.log('Serial port open.');
    lastStatus.connected = true;
    broadcast({ type: 'connection', connected: true, port: SERIAL_PATH });

    parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
    parser.on('data', (line) => {
      const raw = line.trim();
      if (!raw) return;

      const prevAlarms = JSON.stringify(lastAlarms);
      parseAlarms(raw);
      if (JSON.stringify(lastAlarms) !== prevAlarms) {
        broadcast({ type: 'alarms', alarms: { ...lastAlarms } });
      }

      const status = parseStatusLine(raw);
      if (status) {
        Object.assign(lastStatus, status);
        broadcast(status);
      }

      broadcast({ type: 'log', line: raw });
    });
  });

  port.on('close', () => {
    console.log('Serial port closed.');
    lastStatus.connected = false;
    broadcast({ type: 'connection', connected: false });
    setTimeout(openSerial, 3000);
  });

  port.on('error', (err) => {
    console.error('Serial error:', err.message);
    broadcast({ type: 'connection', connected: false, error: err.message });
  });
}

wss.on('connection', (ws) => {
  console.log('WebSocket client connected');

  // Send current state immediately
  ws.send(JSON.stringify({ type: 'connection', connected: lastStatus.connected, port: SERIAL_PATH }));
  ws.send(JSON.stringify(Object.assign({ type: 'status' }, lastStatus)));
  ws.send(JSON.stringify({ type: 'alarms', alarms: { ...lastAlarms } }));
  if (history.length) {
    ws.send(JSON.stringify({ type: 'history', history }));
  }

  ws.on('message', (message) => {
    let cmd = '';
    try {
      const data = JSON.parse(message);
      if (data && data.command) cmd = String(data.command).trim();
    } catch (e) {
      cmd = String(message).trim();
    }

    if (!cmd) return;

    console.log('WS command:', cmd);
    if (port && port.isOpen) {
      port.write(cmd + '\r\n', (err) => {
        if (err) {
          ws.send(JSON.stringify({ type: 'log', line: `[ERR] Failed to send: ${err.message}` }));
        }
      });
    } else {
      ws.send(JSON.stringify({ type: 'log', line: '[ERR] Serial port not open' }));
    }
  });

  ws.on('close', () => {
    console.log('WebSocket client disconnected');
  });
});

server.listen(PORT, () => {
  console.log(`Incubator webapp running at http://localhost:${PORT}`);
  openSerial();
});
