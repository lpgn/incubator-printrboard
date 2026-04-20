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
  adc: null,
  adcTarget: null,
  humidity: null,
  dhtTemp: null,
  heater: null,
  fan: null,
  state: null,
  targetTemp: null,
  uptime: 0,
  connected: false
};

let bootType = null; // null, 'normal', 'recovery'

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
  if (line.includes('DHT sensor read failures')) lastAlarms.dhtFail = true;
  if (line.includes('WARNING: Humidity HIGH')) lastAlarms.humidHigh = true;
  if (line.includes('WARNING: Humidity LOW')) lastAlarms.humidLow = true;
  if (line.includes('ERROR STATE')) lastAlarms.errorState = true;
  if (line.includes('RECOVERED from error')) lastAlarms.errorState = false;
  if (line.includes('RECOVERED: Thermistor sensor OK')) lastAlarms.sensorFail = false;
  if (line.includes('RECOVERED: Over-temp cleared')) lastAlarms.overTemp = false;
  if (line.includes('RECOVERED: Under-temp cleared')) lastAlarms.underTemp = false;
  if (line.includes('RECOVERED: Humidity HIGH cleared')) lastAlarms.humidHigh = false;
  if (line.includes('RECOVERED: Humidity LOW cleared')) lastAlarms.humidLow = false;
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

  // --- Robust field-by-field extraction ---
  const dayHdr   = line.match(/\[DAY\s+(\d+)\/(\d+)\]/);
  const idleHdr  = line.match(/\[IDLE\]/);
  const tempM    = line.match(/T=([\d.-]+)C/);
  const adcM     = line.match(/ADC=(\d+)/);
  const targetM  = line.match(/TARGET=([\d.]+)C/);
  const adcTarM  = line.match(/ADCTARGET=(\d+)/);
  const humidM   = line.match(/H=(\d+)%/);
  const dhtM     = line.match(/DHT=([\d.-]+)C/);
  const htrM     = line.match(/HTR=(\d+)%/);
  const fanM     = line.match(/FAN=(\d+)%/);
  const stateM   = line.match(/STATE=(\S+)/);
  const turnDegM = line.match(/TURNDEG=(\d+)/);
  const turnsM   = line.match(/TURNS=(\d+)\/(\d+)/);
  const uptimeM  = line.match(/UPTIME=(\d+)s/);

  if (!tempM || !adcM || !humidM || !htrM || !fanM) {
    // Not a status line we recognise
    return null;
  }
  // STATE= is only present in [DAY ...] lines, not [IDLE]
  if (!dayHdr && !idleHdr) {
    return null;
  }

  const adcTarget = adcTarM ? parseInt(adcTarM[1], 10) : null;
  if (targetM) lastStatus.targetTemp = parseFloat(targetM[1]);

  if (dayHdr) {
    result = {
      type: 'status',
      day: parseInt(dayHdr[1], 10),
      totalDays: parseInt(dayHdr[2], 10),
      temp: parseFloat(tempM[1]),
      adc: parseInt(adcM[1], 10),
      adcTarget: adcTarget,
      humidity: parseInt(humidM[1], 10),
      dhtTemp: dhtM ? parseFloat(dhtM[1]) : null,
      heater: parseInt(htrM[1], 10),
      fan: parseInt(fanM[1], 10),
      state: stateM[1],
      turnDeg: turnDegM ? parseInt(turnDegM[1], 10) : null,
      turnsCompleted: turnsM ? parseInt(turnsM[1], 10) : null,
      turnsPerDay: turnsM ? parseInt(turnsM[2], 10) : null,
      uptime: uptimeM ? parseInt(uptimeM[1], 10) : 0
    };
  } else if (idleHdr) {
    result = {
      type: 'status',
      day: 0,
      totalDays: 0,
      temp: parseFloat(tempM[1]),
      adc: parseInt(adcM[1], 10),
      adcTarget: adcTarget,
      humidity: parseInt(humidM[1], 10),
      dhtTemp: dhtM ? parseFloat(dhtM[1]) : null,
      heater: parseInt(htrM[1], 10),
      fan: parseInt(fanM[1], 10),
      state: 'IDLE',
      turnDeg: turnDegM ? parseInt(turnDegM[1], 10) : null,
      turnsCompleted: turnsM ? parseInt(turnsM[1], 10) : null,
      turnsPerDay: turnsM ? parseInt(turnsM[2], 10) : null,
      uptime: uptimeM ? parseInt(uptimeM[1], 10) : 0
    };
  }

  if (result) {
    result.targetTemp = lastStatus.targetTemp;
    lastStatus.dhtTemp = result.dhtTemp;
    const point = {
      t: new Date().toISOString(),
      temp: result.temp,
      humidity: result.humidity,
      dhtTemp: result.dhtTemp,
      heater: result.heater,
      fan: result.fan,
      state: result.state,
      targetTemp: lastStatus.targetTemp
    };
    history.push(point);
    if (history.length > MAX_HISTORY) history.shift();
    broadcast({ type: 'history', history: [point] });
    console.log('Parsed status:', result.state, 'adc=', result.adc, 'temp=', result.temp, 'uptime=', result.uptime);
  } else {
    // Debug: we had some fields but not the header
    console.log('Unrecognised status line:', line);
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

      // Boot detection
      if (raw.includes('EGG INCUBATOR v1.0')) {
        bootType = 'normal';
        broadcast({ type: 'boot', state: 'started', recovering: false });
      }
      if (raw.includes('POWER RECOVERY DETECTED')) {
        bootType = 'recovery';
        broadcast({ type: 'boot', state: 'started', recovering: true });
      }
      if (raw.includes('No saved state found')) {
        bootType = 'normal';
        broadcast({ type: 'boot', state: 'started', recovering: false });
      }
      if (raw.includes("AUTO-RESUMING incubation")) {
        broadcast({ type: 'boot', state: 'resume_ready' });
      }
      if (raw.includes("Type 'species' to see options") || raw.includes("Type 'reset' to start fresh") || raw.includes("Type 'stop' if you wish")) {
        if (bootType) {
          bootType = null;
          broadcast({ type: 'boot', state: 'complete' });
        }
      }
      if (bootType && raw.startsWith('> ')) {
        bootType = null;
        broadcast({ type: 'boot', state: 'complete' });
      }

      const prevAlarms = JSON.stringify(lastAlarms);
      parseAlarms(raw);
      if (JSON.stringify(lastAlarms) !== prevAlarms) {
        broadcast({ type: 'alarms', alarms: { ...lastAlarms } });
      }

      const targetMatch = raw.match(/target:\s*([\d.]+)C/i);
      if (targetMatch) {
        lastStatus.targetTemp = parseFloat(targetMatch[1]);
      }

      const status = parseStatusLine(raw);
      if (status) {
        Object.assign(lastStatus, status);
        broadcast(status);
        if (bootType) {
          bootType = null;
          broadcast({ type: 'boot', state: 'complete' });
        }
      }

      // Parse preset list lines
      const presetMatch = raw.match(/^\[PRESET\]\s+(\d+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)/);
      if (presetMatch) {
        if (!lastStatus.presets) lastStatus.presets = [];
        lastStatus.presets.push({
          idx: parseInt(presetMatch[1], 10),
          name: presetMatch[2],
          days: parseInt(presetMatch[3], 10),
          stop: parseInt(presetMatch[4], 10),
          temp: parseInt(presetMatch[5], 10),
          humidLo: parseInt(presetMatch[6], 10),
          humidHi: parseInt(presetMatch[7], 10),
          lockLo: parseInt(presetMatch[8], 10),
          lockHi: parseInt(presetMatch[9], 10),
          turns: parseInt(presetMatch[10], 10),
          deg: parseInt(presetMatch[11], 10)
        });
        // Only broadcast when we have all 8
        if (lastStatus.presets.length === 8) {
          broadcast({ type: 'presets', presets: lastStatus.presets });
          lastStatus.presets = [];
        }
      }

      // Fallback: always hunt for standalone uptime in any line
      const uptimeFallback = raw.match(/UPTIME=(\d+)s/);
      if (uptimeFallback) {
        lastStatus.uptime = parseInt(uptimeFallback[1], 10);
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
  if (bootType) {
    ws.send(JSON.stringify({ type: 'boot', state: 'started', recovering: bootType === 'recovery' }));
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

    if (cmd.toLowerCase().startsWith('set temp ')) {
      const parts = cmd.split(' ');
      if (parts.length >= 3) lastStatus.targetTemp = parseFloat(parts[2]);
    }
    if (cmd.toLowerCase().startsWith('select ')) {
      // All presets default to 37.5C; custom may differ but we don't know here
      lastStatus.targetTemp = 37.5;
    }

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
