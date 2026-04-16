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
  heater: null,
  fan: null,
  state: null,
  targetTemp: null,
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

  // Match: [DAY 01/21] T=21.8C ADC=642 TARGET=37.5C H=0% HTR=0% FAN=29% STATE=ERROR
  // or:   [DAY 01/21] T=21.8C ADC=642 ADCTARGET=580 H=0% HTR=0% FAN=29% STATE=ERROR
  const dayMatch = line.match(/\[DAY\s+(\d+)\/(\d+)\]\s+T=([\d.-]+)C\s+ADC=(\d+)\s+(?:TARGET=([\d.]+)C|ADCTARGET=(\d+))\s+H=(\d+)%\s+HTR=(\d+)%\s+FAN=(\d+)%\s+STATE=(\S+)/);
  if (dayMatch) {
    const adcTarget = dayMatch[6] ? parseInt(dayMatch[6], 10) : null;
    lastStatus.targetTemp = adcTarget ? null : parseFloat(dayMatch[5]);
    result = {
      type: 'status',
      day: parseInt(dayMatch[1], 10),
      totalDays: parseInt(dayMatch[2], 10),
      temp: parseFloat(dayMatch[3]),
      adc: parseInt(dayMatch[4], 10),
      adcTarget: adcTarget,
      humidity: parseInt(dayMatch[7], 10),
      heater: parseInt(dayMatch[8], 10),
      fan: parseInt(dayMatch[9], 10),
      state: dayMatch[10]
    };
  }

  // Match: [IDLE] T=21.8C ADC=642 TARGET=37.5C H=0% HTR=0% FAN=0%
  // or:   [IDLE] T=21.8C ADC=642 ADCTARGET=580 H=0% HTR=0% FAN=0%
  const idleMatch = line.match(/\[IDLE\]\s+T=([\d.-]+)C\s+ADC=(\d+)\s+(?:TARGET=([\d.]+)C|ADCTARGET=(\d+))\s+H=(\d+)%\s+HTR=(\d+)%\s+FAN=(\d+)%/);
  if (idleMatch) {
    const adcTarget = idleMatch[4] ? parseInt(idleMatch[4], 10) : null;
    lastStatus.targetTemp = adcTarget ? null : parseFloat(idleMatch[3]);
    result = {
      type: 'status',
      day: 0,
      totalDays: 0,
      temp: parseFloat(idleMatch[1]),
      adc: parseInt(idleMatch[2], 10),
      adcTarget: adcTarget,
      humidity: parseInt(idleMatch[5], 10),
      heater: parseInt(idleMatch[6], 10),
      fan: parseInt(idleMatch[7], 10),
      state: 'IDLE'
    };
  }

  if (result) {
    result.targetTemp = lastStatus.targetTemp;
    const point = {
      t: new Date().toISOString(),
      temp: result.temp,
      humidity: result.humidity,
      heater: result.heater,
      fan: result.fan,
      state: result.state,
      targetTemp: lastStatus.targetTemp
    };
    history.push(point);
    if (history.length > MAX_HISTORY) history.shift();
    broadcast({ type: 'history', history: [point] });
    console.log('Parsed status:', result.state, 'adc=', result.adc, 'temp=', result.temp);
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

    // Request calibration table whenever port opens
    setTimeout(() => {
      if (port && port.isOpen) {
        port.write('cal table\r\n', (err) => {
          if (err) console.error('Failed to request cal table on open:', err.message);
          else console.log('Requested cal table on serial open');
        });
      }
    }, 500);

    parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
    parser.on('data', (line) => {
      const raw = line.trim();
      if (!raw) return;

      const prevAlarms = JSON.stringify(lastAlarms);
      parseAlarms(raw);
      if (JSON.stringify(lastAlarms) !== prevAlarms) {
        broadcast({ type: 'alarms', alarms: { ...lastAlarms } });
      }

      const targetMatch = raw.match(/target:\s*([\d.]+)C/i);
      if (targetMatch) {
        lastStatus.targetTemp = parseFloat(targetMatch[1]);
      }

      const calTableMatch = raw.match(/^\[CALTABLE\]\s+(\d+):\s*(.*)$/);
      if (calTableMatch) {
        const count = parseInt(calTableMatch[1], 10);
        const rest = calTableMatch[2].trim();
        const points = [];
        if (rest) {
          const pairs = rest.split(/\s+/);
          for (const p of pairs) {
            const parts = p.split(',');
            if (parts.length >= 2) {
              points.push({ adc: parseInt(parts[0], 10), temp: parseFloat(parts[1]) });
            }
          }
        }
        console.log('Parsed caltable:', count, points);
        broadcast({ type: 'caltable', count, points });
      }

      const status = parseStatusLine(raw);
      if (status) {
        Object.assign(lastStatus, status);
        broadcast(status);
      }

      if (raw.includes('[CAL] Point added:') || raw.includes('[CAL] Calibration points cleared.') || raw.includes('>> Calibration reset.')) {
        setTimeout(() => {
          if (port && port.isOpen) port.write('cal table\r\n');
        }, 500);
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

  // Ask firmware for calibration table
  setTimeout(() => {
    if (port && port.isOpen) {
      port.write('cal table\r\n', (err) => {
        if (err) console.error('Failed to request cal table on WS connect:', err.message);
        else console.log('Requested cal table on WS connect');
      });
    } else {
      console.log('Serial port not open yet, skipping cal table request on WS connect');
    }
  }, 500);

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
