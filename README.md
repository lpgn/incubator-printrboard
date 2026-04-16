# Printrboard Egg Incubator Controller

Custom firmware for the **Printrboard Rev D** (AT90USB1286) repurposed as an automated egg incubator controller.

## Features

- 🥚 **7 bird species presets** — Chicken, Pigeon, Quail, Duck, Turkey, Goose, Guinea Fowl + Custom
- 🌡️ **PID temperature control** with autotune (Ziegler-Nichols relay method)
- 💧 **Humidity monitoring** via DHT22 sensor + passive water tray evaporation
- 🔄 **Automated egg turning** — stepper motor with configurable schedule
- 💨 **Smart fan control** — dual-purpose temperature + humidity regulation
- 💾 **Power recovery** — EEPROM state persistence, auto-resume after power loss
- 🖥️ **USB terminal interface** — full control from PC or Raspberry Pi
- ⚠️ **Safety systems** — over-temp cutoff, sensor failure detection, alarms

## Hardware Requirements

| Component | Details |
|---|---|
| **Controller** | Printrboard Rev D (AT90USB1286) |
| **Heater** | 12V heating element (silicone pad, ceramic, etc.) |
| **Temperature sensor** | NTC 100K thermistor (from 3D printer, connects to heatbed thermistor input) |
| **Humidity sensor** | DHT22 / AM2302 (connects to EXP2 header, PD2 pin) |
| **Egg turner motor** | NEMA 17 stepper motor (connects to X-axis stepper driver) |
| **Fan** | 12V DC fan (connects to FAN header) |
| **Power supply** | 12V DC, sufficient for heater + motor + fan |
| **Humidity source** | Water tray at bottom of incubator |
| **Optional** | Status LED (EXP1 PB5), Piezo buzzer (EXP2 PD4) |

## Wiring

| Printrboard Connector | Connect To |
|---|---|
| Heatbed MOSFET | 12V Heating element |
| Heatbed Thermistor | NTC 100K thermistor (near eggs) |
| X Stepper | Egg turning motor (NEMA 17) |
| FAN header | 12V circulation fan |
| EXP2 pin PD2 | DHT22 data pin (+ 4.7kΩ pull-up to 5V) |
| EXP1 pin PB5 | Status LED (optional) |
| EXP2 pin PD4 | Piezo buzzer (optional) |
| USB | PC or Raspberry Pi |

> **Important:** DHT22 has 3 pins: VCC (5V), GND, and DATA. Connect DATA to PD2 with a 4.7kΩ pull-up resistor between DATA and VCC.
>
> **Heater wiring:** Connect the 12V heating element to the **Heatbed MOSFET** connector (not the Extruder Heater). The firmware controls the bed output (pin 14). The extruder heater output (pin 15) is unused and will stay off.

## Building & Flashing

### Prerequisites
- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- USBasp ISP programmer + 6-pin/10-pin adapter cable
- Zadig (for USBasp driver on Windows) — included in `tools/zadig.exe`

### Build
```bash
pio run
```

### Flash
1. **Install the USBasp driver** (Windows only):
   - Plug in the USBasp programmer
   - Run `tools\zadig.exe`
   - Select `USBasp` from the device list
   - Choose driver `libusbK (v3.1.0.0)` and click **Install Driver**
2. **Connect USBasp to the Printrboard ICSP header**
   - Pin 1 (MISO, usually marked with a dot/red wire) is closest to the **SD card slot**
   - Use a 10-pin to 6-pin adapter if your USBasp has a 10-pin header
3. Run:
```bash
pio run --target upload
```

### Serial Monitor
```bash
pio device monitor
```

### Web Dashboard
A simple webapp is included to monitor and control the incubator from a browser over USB.

```bash
cd webapp
npm install
npm start
```

Then open **http://localhost:3000** in your browser.

**Requirements:**
- [Node.js](https://nodejs.org/) installed
- The Printrboard USB serial port (default `COM8`) available

You can change the serial port if needed:
```bash
set SERIAL_PORT=COM3
npm start
```

The dashboard shows live temperature, humidity, heater/fan levels, state, and day. It also provides buttons for common commands and a serial log viewer.

## Usage

### Quick Start
```
> species
Available presets:
  1. Chicken    - 21 days, 37.5C, 45-55% RH
  2. Pigeon     - 18 days, 37.5C, 50-60% RH
  ...

> select chicken
>> Species set to Chicken (21 days)

> start
>> PREHEATING... Stabilizing temperature.
```

### Commands

| Command | Description |
|---|---|
| `species` | List available species presets |
| `select <name>` | Select species (e.g., `select chicken`) |
| `start` | Begin incubation cycle |
| `stop` | Emergency stop |
| `autotune` | Run PID autotune (~10-20 minutes) |
| `pause` | Pause incubation |
| `resume` | Resume incubation / power recovery |
| `status` | Show detailed status |
| `set temp <C>` | Override temperature setpoint |
| `set humidity <lo> <hi>` | Override humidity range (%) |
| `set pid <Kp> <Ki> <Kd>` | Set PID tuning parameters |
| `set turns <N>` | Set turns per day |
| `set fan <min> <max>` | Set fan speed range (0-255) |
| `turn` | Force an immediate egg turn |
| `log` | Show event log |
| `silence` | Silence buzzer alarm |
| `reset` | Factory reset (clear EEPROM) |
| `help` | Show all commands |

### Auto-Reporting
Every 60 seconds during incubation, the firmware prints:
```
[DAY 03/21] T=37.4C H=52% HTR=45% FAN=30% STATE=INCUBATING
```
This can be piped to a log file on a Raspberry Pi for monitoring.

## Species Presets

| Species | Days | Temp | Humidity (Setter) | Humidity (Lockdown) | Turns/Day |
|---|---|---|---|---|---|
| Chicken | 21 | 37.5°C | 45-55% | 65-70% | 5 |
| Pigeon | 18 | 37.5°C | 50-60% | 65-75% | 3 |
| Quail | 18 | 37.5°C | 45-50% | 65-70% | 4 |
| Duck | 28 | 37.5°C | 55-60% | 65-75% | 4 |
| Turkey | 28 | 37.5°C | 50-60% | 65-70% | 5 |
| Goose | 30 | 37.5°C | 50-55% | 70-75% | 5 |
| Guinea Fowl | 28 | 37.5°C | 45-50% | 65-70% | 4 |

## Incubation Phases

1. **PREHEATING** — Heater warms up to 37.5°C. Waits for temperature to stabilize (±0.5°C for 30 minutes).
2. **INCUBATING** — Active phase. PID maintains temperature, eggs are turned on schedule, humidity monitored.
3. **LOCKDOWN** — Egg turning stops. Humidity target increases. Do not open incubator.
4. **HATCHING** — Final watch phase. Monitor for chicks.
5. **DONE** — Incubation complete.

## Power Recovery

The firmware saves state to EEPROM every 10 minutes. After a power outage:

1. On boot, the firmware detects saved state and displays recovery info
2. Type `resume` to continue where you left off
3. Type `reset` to start fresh

> **⚠️ Warning:** A UPS (uninterruptible power supply) is highly recommended. Extended power loss can kill developing embryos.

## Safety

| Condition | Action |
|---|---|
| Temperature > 40°C | Heater OFF, fan 100%, buzzer alarm |
| Temperature < 35°C for 10+ min | Buzzer warning |
| Thermistor disconnected | Heater OFF, buzzer alarm |
| DHT22 failure | Warning, continues with last reading |
| Humidity > 80% | Fan ramps up, warning |
| Humidity < 30% | Fan ramps up, warning — refill water! |

## License

This project is open source. Use at your own risk — you are responsible for the safety of the incubator and the lives within it.
