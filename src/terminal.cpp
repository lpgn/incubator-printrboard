#include "terminal.h"
#include "config.h"
#include "sdlogger.h"
#include "rtc.h"
#include <stdlib.h>
#include <string.h>

Terminal::Terminal()
    : _bufPos(0), _lastAutoReport(0),
      _sm(nullptr), _pid(nullptr), _heater(nullptr), _humid(nullptr),
      _turner(nullptr), _fan(nullptr), _clock(nullptr), _storage(nullptr),
      _safety(nullptr), _rtc(nullptr), _sd(nullptr) {}

void Terminal::begin() {
    Serial.begin(SERIAL_BAUD);
}

void Terminal::printBanner() {
    Serial.println();
    Serial.println(F("==================================================="));
    Serial.println(F("  EGG INCUBATOR v1.0 — Printrboard Rev D"));
    Serial.println(F("  AT90USB1286 | Custom Firmware"));
    Serial.println(F("==================================================="));
    Serial.println(F("  Type 'help' for commands."));
    Serial.println();
}

void Terminal::printPrompt() {
    Serial.print(F("> "));
}

void Terminal::setReferences(StateMachine* sm, PIDController* pid, Heater* heater,
                             HumiditySensor* humid, EggTurner* turner, FanController* fan,
                             SoftClock* clock, Storage* storage, SafetyMonitor* safety,
                             DS3231* rtc, SDLogger* sdLogger) {
    _sm = sm;
    _pid = pid;
    _heater = heater;
    _humid = humid;
    _turner = turner;
    _fan = fan;
    _clock = clock;
    _storage = storage;
    _safety = safety;
    _rtc = rtc;
    _sd = sdLogger;
}

bool Terminal::poll() {
    while (Serial.available()) {
        char c = Serial.read();

        // Discard escape sequences (arrow keys, function keys, etc.)
        if (c == '\x1B') {
            while (Serial.available()) {
                char peek = Serial.peek();
                if (peek == '[' || peek == '(' || peek == 'O' ||
                    (peek >= 'A' && peek <= 'Z') || (peek >= 'a' && peek <= 'z') ||
                    (peek >= '0' && peek <= '9') || peek == ';' || peek == '~') {
                    Serial.read();
                } else {
                    break;
                }
            }
            continue;
        }

        // Handle backspace / DEL
        if (c == '\b' || c == '\x7F') {
            if (_bufPos > 0) {
                _bufPos--;
                Serial.print(F("\b \b"));
            }
            continue;
        }

        // Ignore other non-printable characters
        if (c < 32 || c > 126) {
            if (c != '\n' && c != '\r') {
                continue;
            }
        }

        if (c == '\n' || c == '\r') {
            Serial.println();
            if (_bufPos > 0) {
                _buf[_bufPos] = '\0';
                processCommand(_buf);
                _bufPos = 0;
            }
            printPrompt();
            return true;
        } else if (_bufPos < TERMINAL_BUF_SIZE - 1) {
            _buf[_bufPos++] = c;
            Serial.print(c);
        }
    }
    return false;
}

bool Terminal::shouldAutoReport() {
    unsigned long now = millis();
    if (now - _lastAutoReport >= STATUS_REPORT_MS) {
        _lastAutoReport = now;
        return true;
    }
    return false;
}

void Terminal::printStatus() {
    if (!_sm || !_heater || !_humid || !_clock) return;

    float temp = _heater->readTemperature();
    float humidity = _humid->getHumidity();

    IncubatorState state = _sm->getState();

    float targetTemp = _sm->getTargetTemp();

    if (state == STATE_IDLE) {
        uint16_t adc = _heater->readRawADC();
        Serial.print(F("[IDLE] T="));
        Serial.print(temp, 1);
        Serial.print(F("C ADC="));
        Serial.print(adc);
        if (_sm->isAdcTargetMode()) {
            Serial.print(F(" ADCTARGET="));
            Serial.print(_sm->getAdcTarget());
        } else {
            Serial.print(F(" TARGET="));
            Serial.print(targetTemp, 1);
            Serial.print(F("C"));
        }
        Serial.print(F(" H="));
        Serial.print(humidity, 0);
        Serial.print(F("% DHT="));
        Serial.print(_humid->getTemperature(), 1);
        Serial.print(F("C HTR="));
        Serial.print((uint16_t)_heater->getOutput() * 100 / 255);
        Serial.print(F("% FAN="));
        Serial.print(_fan->getSpeedPercent());
        Serial.println(F("%"));
        return;
    }

    // Compact status line for logging
    Serial.print(F("[DAY "));
    if (_clock->getCurrentDay() < 10) Serial.print('0');
    Serial.print(_clock->getCurrentDay());
    Serial.print('/');
    Serial.print(_sm->getActivePreset().totalDays);
    Serial.print(F("] T="));
    Serial.print(temp, 1);
    uint16_t adc = _heater->readRawADC();
    Serial.print(F("C ADC="));
    Serial.print(adc);
    if (_sm->isAdcTargetMode()) {
        Serial.print(F(" ADCTARGET="));
        Serial.print(_sm->getAdcTarget());
    } else {
        Serial.print(F(" TARGET="));
        Serial.print(targetTemp, 1);
        Serial.print(F("C"));
    }
    Serial.print(F(" H="));
    Serial.print(humidity, 0);
    Serial.print(F("% DHT="));
    Serial.print(_humid->getTemperature(), 1);
    Serial.print(F("C HTR="));
    Serial.print((uint16_t)_heater->getOutput() * 100 / 255);
    Serial.print(F("% FAN="));
    Serial.print(_fan->getSpeedPercent());
    Serial.print(F("% STATE="));
    Serial.println(_sm->getStateName());
}

void Terminal::processCommand(const char* cmd) {
    // Skip leading whitespace
    while (*cmd == ' ') cmd++;

    if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cmdHelp();
    } else if (strcasecmp(cmd, "species") == 0) {
        cmdSpecies();
    } else if (strncasecmp(cmd, "select ", 7) == 0) {
        cmdSelect(cmd + 7);
    } else if (cmd[0] >= '1' && cmd[0] <= '9' && cmd[1] == '\0') {
        // Bare number is a quick-select shortcut
        cmdSelect(cmd);
    } else if (strcasecmp(cmd, "start") == 0) {
        cmdStart();
    } else if (strcasecmp(cmd, "stop") == 0) {
        cmdStop();
    } else if (strcasecmp(cmd, "autotune") == 0) {
        cmdAutotune();
    } else if (strcasecmp(cmd, "pause") == 0) {
        cmdPause();
    } else if (strcasecmp(cmd, "resume") == 0) {
        cmdResume();
    } else if (strcasecmp(cmd, "status") == 0) {
        cmdStatus();
    } else if (strncasecmp(cmd, "set ", 4) == 0) {
        cmdSet(cmd + 4);
    } else if (strcasecmp(cmd, "log") == 0) {
        cmdLog();
    } else if (strcasecmp(cmd, "reset") == 0) {
        cmdReset();
    } else if (strcasecmp(cmd, "silence") == 0) {
        cmdSilence();
    } else if (strcasecmp(cmd, "turn") == 0) {
        cmdTurn();
    } else if (strncasecmp(cmd, "test ", 5) == 0) {
        cmdTest(cmd + 5);
    } else if (strncasecmp(cmd, "override", 8) == 0) {
        cmdOverride(cmd + 8);
    } else if (strcasecmp(cmd, "sd") == 0) {
        cmdSD();
    } else if (strncasecmp(cmd, "custom ", 7) == 0) {
        cmdCustom(cmd + 7);
    } else if (strncasecmp(cmd, "cal ", 4) == 0) {
        cmdCal(cmd + 4);
    } else if (strncasecmp(cmd, "time", 4) == 0) {
        cmdTime(cmd + 4);
    } else {
        Serial.print(F("Unknown command: "));
        Serial.println(cmd);
        Serial.println(F("Type 'help' for available commands."));
    }
}

void Terminal::cmdHelp() {
    Serial.println(F(""));
    Serial.println(F("COMMANDS:"));
    Serial.println(F("  species              List available species presets"));
    Serial.println(F("  select <name|#>      Select species (e.g. select chicken, select 1)"));
    Serial.println(F("  start                Begin incubation cycle"));
    Serial.println(F("  stop                 Emergency stop"));
    Serial.println(F("  autotune             Run PID autotune"));
    Serial.println(F("  pause                Pause incubation"));
    Serial.println(F("  resume               Resume incubation / power recovery"));
    Serial.println(F("  status               Show detailed status"));
    Serial.println(F("  set temp <C>         Override temperature setpoint"));
    Serial.println(F("  set adc <value>      Override PID target to raw ADC value (0-1023)"));
    Serial.println(F("  set adc off          Disable ADC target mode, return to temp"));
    Serial.println(F("  set maxtemp <C>      Set safety max temp (35-50C)"));
    Serial.println(F("  set humidity <lo> <hi>  Override humidity range (%)"));
    Serial.println(F("  set pid <Kp> <Ki> <Kd>  Set PID tuning"));
    Serial.println(F("  set turns <N>        Set turns per day"));
    Serial.println(F("  set fan <min> <max>  Set fan speed range (0-255)"));
    Serial.println(F("  set preheat <pwm>    Max heater PWM during preheat (0-255)"));
    Serial.println(F("  turn                 Force an immediate egg turn"));
    Serial.println(F("  log                  Show event log"));
    Serial.println(F("  silence              Silence buzzer alarm"));
    Serial.println(F("  sd                   Show SD card status"));
    Serial.println(F("  reset                Factory reset (clear EEPROM)"));
    Serial.println(F("  help                 Show this help"));
    Serial.println(F(""));
    Serial.println(F("HARDWARE TEST:"));
    Serial.println(F("  test temp            Read thermistor ADC and temperature"));
    Serial.println(F("  test dht             Read DHT humidity and temperature"));
    Serial.println(F("  test heater <pwm>    Force heater PWM 0-255, -1 = auto"));
    Serial.println(F("  test fan <pwm>       Force fan PWM 0-255 (-1 = auto)"));
    Serial.println(F("  test motor           Trigger one egg turn immediately (no-count)"));
    Serial.println();
    Serial.println(F("SAFETY OVERRIDE:"));
    Serial.println(F("  override on          Disable safety shutdowns (for testing)"));
    Serial.println(F("  override off         Re-enable safety shutdowns"));
    Serial.println();
    Serial.println(F("CUSTOM SPECIES:"));
    Serial.println(F("  custom days <N>      Set total incubation days"));
    Serial.println(F("  custom stop <N>      Set turning stop day (lockdown)"));
    Serial.println(F("  custom temp <C>      Set temperature setpoint"));
    Serial.println(F("  custom humid <lo> <hi>  Set setter humidity range"));
    Serial.println(F("  custom lock <lo> <hi>   Set lockdown humidity range"));
    Serial.println(F("  custom turns <N>     Set turns per day"));
    Serial.println();
    Serial.println(F("CALIBRATION:"));
    Serial.println(F("  cal temp <offset>    Add offset to all temp readings (e.g. -14.2)"));
    Serial.println(F("  cal temp actual <C>  Set offset based on trusted thermometer"));
    Serial.println(F("  cal point <C>        Record ADC->actualTemp calibration point"));
    Serial.println(F("  cal points           List recorded calibration points"));
    Serial.println(F("  cal table            Output single-line table for webapp"));
    Serial.println(F("  cal generate         Print C code for hardcoded table"));
    Serial.println(F("  cal clear points     Erase all recorded points"));
    Serial.println(F("  cal reset            Clear temp offset, custom thermistor, and points"));
    Serial.println(F("  set thermistor <R25> <beta>  Set custom thermistor curve"));
    Serial.println();
    Serial.println(F("RTC (if DS3231 connected):"));
    Serial.println(F("  time                 Show current RTC time"));
    Serial.println(F("  time set <YYYY MM DD HH MM SS>  Set RTC"));
    Serial.println();
}

void Terminal::cmdSpecies() {
    printSpeciesList();
}

void Terminal::cmdSelect(const char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
        Serial.println(F("Usage: select <species name or number>"));
        return;
    }

    if (_sm->getState() != STATE_IDLE) {
        Serial.println(F("Cannot change species while incubation is active. Use 'stop' first."));
        return;
    }

    SpeciesID id;
    if (arg[0] >= '1' && arg[0] <= '9' && arg[1] == '\0') {
        uint8_t idx = arg[0] - '1';
        if (idx < SPECIES_COUNT) {
            id = (SpeciesID)idx;
        } else {
            id = SPECIES_COUNT; // invalid
        }
    } else {
        id = findSpeciesByName(arg);
    }

    if (id >= SPECIES_COUNT) {
        Serial.print(F("Unknown species: "));
        Serial.println(arg);
        Serial.println(F("Type 'species' to list available presets."));
        return;
    }

    _sm->setSpecies(id);
    Serial.print(F(">> Species set to "));
    Serial.println(getSpeciesName(id));
    printSpeciesDetails(id);
}

void Terminal::cmdStart() {
    if (_sm->getState() != STATE_IDLE) {
        Serial.println(F("Cannot start — already running. Use 'stop' first."));
        return;
    }

    SpeciesID species = _sm->getSpeciesID();
    SpeciesPreset p = getSpeciesPreset(species);

    if (p.totalDays == 0) {
        Serial.println(F("Cannot start — custom species has 0 days. Configure first."));
        return;
    }

    _heater->clearShutdown();
    _heater->setManualSpeed(-1);
    _sm->startPreheating(species);
    _clock->start();
    _turner->setTurnsPerDay(p.turnsPerDay);
    _turner->setEnabled(true);
    _pid->reset();
    _pid->setSetpoint(_sm->getTargetTemp());
    _fan->setManualSpeed(-1); // Ensure fan is in auto mode

    _storage->logEvent(EVENT_START, (uint16_t)species);

    Serial.println(F(">> PREHEATING... Stabilizing temperature."));
    Serial.print(F("   Target: "));
    Serial.print(_sm->getTargetTemp(), 1);
    Serial.print(F("C | Species: "));
    Serial.print(getSpeciesName(species));
    Serial.print(F(" ("));
    Serial.print(p.totalDays);
    Serial.println(F(" days)"));
    Serial.println(F("   Heater ON | Fan ON"));
    Serial.println(F("   Incubation begins when temp is stable for 30 min."));
}

void Terminal::cmdStop() {
    _sm->emergencyStop();
    _pid->autotuneCancel();
    _heater->shutdown();
    _turner->setEnabled(false);
    _fan->setManualSpeed(0);
    _clock->stop();
    _storage->logEvent(EVENT_USER_STOP, 0);

    Serial.println(F(">> STOPPED. Heater OFF, turner OFF, fan OFF."));
    Serial.println(F("   Use 'start' to begin a new cycle."));
}

void Terminal::cmdAutotune() {
    if (_sm->getState() != STATE_IDLE) {
        Serial.println(F("Autotune can only run from IDLE state. Use 'stop' first."));
        return;
    }

    _heater->clearShutdown();
    _heater->setManualSpeed(-1);
    _sm->startAutotune();
    _pid->autotuneStart(_sm->getTargetTemp());
    Serial.println(F(">> PID Autotune started. This will take 10-20 minutes."));
    Serial.println(F("   Type 'stop' to cancel."));
}

void Terminal::cmdPause() {
    if (_sm->pause()) {
        _clock->pause();
        _turner->setEnabled(false);
        _storage->logEvent(EVENT_PAUSE, 0);
        Serial.println(F(">> PAUSED. Heater maintains temp, timer stopped."));
    } else {
        Serial.println(F("Cannot pause in current state."));
    }
}

void Terminal::cmdResume() {
    if (_sm->getState() == STATE_PAUSED) {
        _heater->clearShutdown();
        _heater->setManualSpeed(-1);
        _sm->resume();
        _clock->resume();
        if (_sm->isTurningAllowed()) {
            _turner->setEnabled(true);
        }
        _fan->setManualSpeed(-1); // Return to auto
        _storage->logEvent(EVENT_RESUME, 0);
        Serial.println(F(">> RESUMED."));
    } else {
        Serial.println(F("Nothing to resume."));
    }
}

void Terminal::cmdStatus() {
    if (!_sm) return;

    float temp = _heater->readTemperature();

    Serial.println(F("==================================="));
    Serial.print(F("  State: "));
    Serial.print(_sm->getStateName());

    if (_sm->getState() != STATE_IDLE && _sm->getState() != STATE_AUTOTUNE) {
        Serial.print(F(" | Day "));
        Serial.print(_clock->getCurrentDay());
        Serial.print(F(" of "));
        Serial.print(_sm->getActivePreset().totalDays);
    }
    Serial.println();

    Serial.print(F("  Temp:  "));
    if (temp > -900.0f) {
        Serial.print(temp, 1);
    } else {
        Serial.print(F("FAIL"));
    }
    if (_sm->isAdcTargetMode()) {
        Serial.print(F("C (ADC target: "));
        Serial.print(_sm->getAdcTarget());
        Serial.print(F(" | raw ADC: "));
        Serial.print(_heater->readRawADC());
        Serial.println(F(")"));
    } else {
        Serial.print(F("C (target: "));
        Serial.print(_sm->getTargetTemp(), 1);
        Serial.print(F("C | max: "));
        Serial.print(_safety->getMaxTemp(), 1);
        Serial.println(F("C)"));
    }

    Serial.print(F("  Humid: "));
    Serial.print(_humid->getHumidity(), 0);
    Serial.print(F("% (target: "));
    Serial.print(_sm->getHumidityLo());
    Serial.print('-');
    Serial.print(_sm->getHumidityHi());
    Serial.println(F("%)"));

    Serial.print(F("  Heater: "));
    Serial.print((uint16_t)_heater->getOutput() * 100 / 255);
    Serial.print(F("% PWM | Fan: "));
    Serial.print(_fan->getSpeedPercent());
    Serial.println(F("% PWM"));

    if (_sm->isTurningAllowed()) {
        Serial.print(F("  Turner: Next turn in "));
        uint32_t secs = _turner->getSecondsUntilNextTurn(_clock->getDaySeconds());
        Serial.print(secs / 3600);
        Serial.print(F("h "));
        Serial.print((secs % 3600) / 60);
        Serial.println(F("m"));
    }

    char timeBuf[20];
    _clock->getFormattedTime(timeBuf, sizeof(timeBuf));
    Serial.print(F("  Uptime: "));
    Serial.println(timeBuf);

    Serial.print(F("  PID: Kp="));
    Serial.print(_pid->getKp(), 2);
    Serial.print(F(" Ki="));
    Serial.print(_pid->getKi(), 3);
    Serial.print(F(" Kd="));
    Serial.println(_pid->getKd(), 2);

    if (_safety->isAnyAlarm()) {
        Serial.println(F("  *** ALARMS ACTIVE ***"));
    }

    Serial.println(F("==================================="));
}

void Terminal::cmdSet(const char* args) {
    while (*args == ' ') args++;

    if (strncasecmp(args, "temp ", 5) == 0) {
        float temp = atof(args + 5);
        if (temp < 10.0f || temp > 60.0f) {
            Serial.println(F("Temperature must be 10-60C."));
            return;
        }
        _sm->setTempOverride(temp);
        _pid->setSetpoint(temp);
        Serial.print(F(">> Temperature setpoint: "));
        Serial.print(temp, 1);
        Serial.println(F("C"));
    }
    else if (strncasecmp(args, "maxtemp ", 8) == 0) {
        float temp = atof(args + 8);
        if (temp < 35.0f || temp > 50.0f) {
            Serial.println(F("Max temp must be 35-50C."));
            return;
        }
        _safety->setMaxTemp(temp);
        Serial.print(F(">> Safety max temperature: "));
        Serial.print(temp, 1);
        Serial.println(F("C"));
    }
    else if (strncasecmp(args, "humidity ", 8) == 0) {
        const char* p = args + 8;
        uint8_t lo = (uint8_t)atoi(p);
        // Find second number
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        uint8_t hi = (uint8_t)atoi(p);
        if (lo < 20 || hi > 90 || lo > hi) {
            Serial.println(F("Humidity range must be 20-90%."));
            return;
        }
        _sm->setHumidityOverride(lo, hi);
        Serial.print(F(">> Humidity target: "));
        Serial.print(lo);
        Serial.print('-');
        Serial.print(hi);
        Serial.println(F("%"));
    }
    else if (strncasecmp(args, "pid ", 4) == 0) {
        const char* p = args + 4;
        float kp = atof(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        float ki = atof(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        float kd = atof(p);
        _pid->setTunings(kp, ki, kd);
        Serial.print(F(">> PID: Kp="));
        Serial.print(kp, 2);
        Serial.print(F(" Ki="));
        Serial.print(ki, 3);
        Serial.print(F(" Kd="));
        Serial.println(kd, 2);
    }
    else if (strncasecmp(args, "turns ", 6) == 0) {
        uint8_t turns = (uint8_t)atoi(args + 6);
        if (turns < 1 || turns > 24) {
            Serial.println(F("Turns must be 1-24."));
            return;
        }
        _turner->setTurnsPerDay(turns);
        Serial.print(F(">> Turns per day: "));
        Serial.println(turns);
    }
    else if (strncasecmp(args, "turn deg ", 9) == 0) {
        uint16_t deg = (uint16_t)atoi(args + 9);
        if (deg < 15 || deg > 360) {
            Serial.println(F("Degrees per turn must be 15-360."));
            return;
        }
        _turner->setDegreesPerTurn(deg);
        uint32_t steps = (uint32_t)deg * TURNER_STEPS_PER_REV / 360UL;
        Serial.print(F(">> Degrees per turn: "));
        Serial.print(deg);
        Serial.print(F(" (~"));
        Serial.print(steps);
        Serial.println(F(" steps)"));
    }
    else if (strncasecmp(args, "turn rpm ", 9) == 0) {
        float rpm = atof(args + 9);
        if (rpm < 0.5f || rpm > 10.0f) {
            Serial.println(F("Turn RPM must be 0.5-10."));
            return;
        }
        _turner->setRPM(rpm);
        Serial.print(F(">> Turn RPM: "));
        Serial.println(rpm, 1);
    }
    else if (strncasecmp(args, "fan ", 4) == 0) {
        const char* p = args + 4;
        uint8_t minS = (uint8_t)atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        uint8_t maxS = (uint8_t)atoi(p);
        _fan->setSpeedRange(minS, maxS);
        Serial.print(F(">> Fan range: "));
        Serial.print(minS);
        Serial.print('-');
        Serial.println(maxS);
    }
    else if (strncasecmp(args, "preheat ", 8) == 0) {
        int pwm = atoi(args + 8);
        if (pwm < 0 || pwm > 255) {
            Serial.println(F("Usage: set preheat <0-255>"));
            return;
        }
        _sm->setPreheatMax((uint8_t)pwm);
        _storage->savePreheatMax((uint8_t)pwm);
        Serial.print(F(">> Preheat max PWM set to "));
        Serial.println(pwm);
    }
    else if (strncasecmp(args, "thermistor ", 11) == 0) {
        const char* p = args + 11;
        float nominalR = atof(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        float beta = atof(p);
        if (nominalR <= 0.0f || beta <= 0.0f) {
            Serial.println(F("Usage: set thermistor <R25> <beta> (values must be > 0)"));
            return;
        }
        _heater->setCustomThermistor(nominalR, beta);
        _storage->saveCalibration(_heater->getTempOffset(), nominalR, beta);
        Serial.print(F(">> Custom thermistor: R25="));
        Serial.print(nominalR, 1);
        Serial.print(F(" beta="));
        Serial.println(beta, 1);
    }
    else {
        Serial.println(F("Usage: set temp|humidity|pid|turns|turn deg|turn rpm|fan|preheat|thermistor <values>"));
    }
}

void Terminal::cmdLog() {
    _storage->printEventLog();
}

void Terminal::cmdReset() {
    Serial.println(F(">> Factory reset — clearing EEPROM..."));
    _storage->clear();
    _sm->reset();
    _heater->shutdown();
    _heater->clearShutdown();
    _heater->setManualSpeed(-1);
    _turner->setEnabled(false);
    _fan->setManualSpeed(-1);
    _clock->stop();
    _safety->clearAlarms();
    _pid->begin(PID_DEFAULT_KP, PID_DEFAULT_KI, PID_DEFAULT_KD);
    Serial.println(F(">> Reset complete. All settings restored to defaults."));
}

void Terminal::cmdSilence() {
    _safety->silenceAlarm();
    Serial.println(F(">> Buzzer silenced. Alarm flags remain active."));
}

void Terminal::cmdTurn() {
    if (_sm->isTurningAllowed()) {
        _turner->turnNow();
    } else {
        Serial.println(F("Turning not allowed in current state."));
    }
}

void Terminal::cmdOverride(const char* arg) {
    while (*arg == ' ') arg++;
    if (strcasecmp(arg, "on") == 0) {
        _safety->setOverride(true);
        Serial.println(F(">> Safety override ENABLED."));
        Serial.println(F("   Heater will NOT shut down on sensor/overtemp alarms."));
        Serial.println(F("   Type 'override off' to restore normal protection."));
    } else if (strcasecmp(arg, "off") == 0) {
        _safety->setOverride(false);
        Serial.println(F(">> Safety override DISABLED. Normal protection restored."));
    } else {
        Serial.print(F("Override: "));
        Serial.println(_safety->isOverridden() ? F("ON") : F("OFF"));
    }
}

void Terminal::cmdTest(const char* args) {
    while (*args == ' ') args++;

    if (strncasecmp(args, "temp", 4) == 0) {
        uint16_t raw = _heater->readRawADC();
        float temp = _heater->readTemperature();
        float tempEpcos = _heater->adcToTemperature(raw, 100000.0f, 3950.0f);
        float tempCarbon = _heater->adcToTemperature(raw, 155570.0f, 4092.0f);
        Serial.print(F("[TEST] Thermistor ADC = "));
        Serial.println(raw);
        if (_heater->isSensorFailed()) {
            Serial.println(F("[TEST] Sensor FAILED (open or short)"));
        } else {
            Serial.print(F("[TEST] Active profile: "));
            Serial.print(temp, 1);
            Serial.println(F("C"));
            Serial.print(F("[TEST] EPCOS 100K:     "));
            Serial.print(tempEpcos, 1);
            Serial.println(F("C"));
            Serial.print(F("[TEST] Carbonmini:     "));
            Serial.print(tempCarbon, 1);
            Serial.println(F("C"));
        }
    }
    else if (strncasecmp(args, "heater ", 7) == 0) {
        int pwm = atoi(args + 7);
        if (pwm < -1 || pwm > 255) {
            Serial.println(F("Usage: test heater <0-255> or test heater -1"));
            return;
        }
        _heater->clearShutdown();
        _heater->setManualSpeed((int16_t)pwm);
        if (pwm == -1) {
            Serial.println(F("[TEST] Heater returned to AUTO mode."));
        } else {
            Serial.print(F("[TEST] Heater PWM set to "));
            Serial.println(pwm);
            if (pwm > 0) {
                Serial.println(F("WARNING: Heater is ON. Type 'test heater 0' or 'stop' to turn off."));
            }
        }
    }
    else if (strncasecmp(args, "fan ", 4) == 0) {
        int pwm = atoi(args + 4);
        if (pwm == -1) {
            _fan->setManualSpeed(-1);
            Serial.println(F("[TEST] Fan returned to AUTO mode."));
        } else if (pwm < 0 || pwm > 255) {
            Serial.println(F("Usage: test fan <0-255> or test fan -1"));
        } else {
            _fan->setManualSpeed((int16_t)pwm);
            Serial.print(F("[TEST] Fan PWM set to "));
            Serial.println(pwm);
        }
    }
    else if (strncasecmp(args, "dht", 3) == 0) {
        Serial.print(F("[TEST] DHT present: "));
        Serial.println(_humid->isPresent() ? F("YES") : F("NO"));
        Serial.print(F("[TEST] DHT failures: "));
        Serial.println(_humid->getFailureCount());
        if (_humid->read()) {
            Serial.print(F("[TEST] DHT humidity: "));
            Serial.print(_humid->getHumidity(), 1);
            Serial.println(F("%"));
            Serial.print(F("[TEST] DHT temperature: "));
            Serial.print(_humid->getTemperature(), 1);
            Serial.println(F("C"));
        } else {
            Serial.println(F("[TEST] DHT read FAILED — check wiring and pull-up resistor"));
        }
    }
    else if (strncasecmp(args, "motor", 5) == 0) {
        if (_turner->isStepping()) {
            Serial.println(F("[TEST] Motor is already turning. Wait for it to finish."));
        } else {
            _turner->turnNow(false);
            Serial.println(F("[TEST] Motor turn triggered."));
        }
    }
    else {
        Serial.println(F("Usage: test temp|dht|heater <pwm>|fan <pwm>|motor"));
    }
}

void Terminal::cmdSD() {
    if (_sd) {
        _sd->printStatus();
    } else {
        Serial.println(F("[SD] Logger not available."));
    }
}

void Terminal::cmdCustom(const char* args) {
    while (*args == ' ') args++;

    if (_sm->getState() != STATE_IDLE) {
        Serial.println(F("Cannot edit custom species while incubation is active."));
        return;
    }

    SpeciesPreset cp = getCustomPreset();

    if (strncasecmp(args, "days ", 5) == 0) {
        uint8_t days = (uint8_t)atoi(args + 5);
        if (days < 1 || days > 60) {
            Serial.println(F("Days must be 1-60."));
            return;
        }
        cp.totalDays = days;
        setCustomPreset(cp);
        Serial.print(F(">> Custom total days: "));
        Serial.println(days);
    }
    else if (strncasecmp(args, "stop ", 5) == 0) {
        uint8_t day = (uint8_t)atoi(args + 5);
        if (day < 1 || day > cp.totalDays) {
            Serial.println(F("Stop day must be 1 to totalDays."));
            return;
        }
        cp.turningStopDay = day;
        setCustomPreset(cp);
        Serial.print(F(">> Custom turning stop day: "));
        Serial.println(day);
    }
    else if (strncasecmp(args, "temp ", 5) == 0) {
        float temp = atof(args + 5);
        if (temp < 30.0f || temp > 42.0f) {
            Serial.println(F("Temp must be 30-42C."));
            return;
        }
        cp.tempSetpoint = (uint16_t)(temp * 10.0f);
        setCustomPreset(cp);
        Serial.print(F(">> Custom temp: "));
        Serial.print(temp, 1);
        Serial.println(F("C"));
    }
    else if (strncasecmp(args, "humid ", 6) == 0) {
        const char* p = args + 6;
        uint8_t lo = (uint8_t)atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        uint8_t hi = (uint8_t)atoi(p);
        if (lo < 20 || hi > 90 || lo > hi) {
            Serial.println(F("Humidity range must be 20-90%."));
            return;
        }
        cp.humiditySetterLo = lo;
        cp.humiditySetterHi = hi;
        setCustomPreset(cp);
        Serial.print(F(">> Custom setter humidity: "));
        Serial.print(lo);
        Serial.print('-');
        Serial.print(hi);
        Serial.println(F("%"));
    }
    else if (strncasecmp(args, "lock ", 5) == 0) {
        const char* p = args + 5;
        uint8_t lo = (uint8_t)atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        uint8_t hi = (uint8_t)atoi(p);
        if (lo < 20 || hi > 90 || lo > hi) {
            Serial.println(F("Humidity range must be 20-90%."));
            return;
        }
        cp.humidityLockdownLo = lo;
        cp.humidityLockdownHi = hi;
        setCustomPreset(cp);
        Serial.print(F(">> Custom lockdown humidity: "));
        Serial.print(lo);
        Serial.print('-');
        Serial.print(hi);
        Serial.println(F("%"));
    }
    else if (strncasecmp(args, "turns ", 6) == 0) {
        uint8_t turns = (uint8_t)atoi(args + 6);
        if (turns < 1 || turns > 24) {
            Serial.println(F("Turns must be 1-24."));
            return;
        }
        cp.turnsPerDay = turns;
        setCustomPreset(cp);
        Serial.print(F(">> Custom turns/day: "));
        Serial.println(turns);
    }
    else {
        // Show current custom preset
        Serial.println(F("Custom species preset:"));
        printSpeciesDetails(SPECIES_CUSTOM);
        Serial.println(F("Use 'custom <param> <value>' to edit. See 'help'."));
    }
}

void Terminal::cmdCal(const char* args) {
    while (*args == ' ') args++;

    if (strncasecmp(args, "temp actual ", 12) == 0) {
        float actualTemp = atof(args + 12);
        float rawTemp = _heater->adcToTemperature(_heater->readRawADC()); // true sensor reading before offset
        float offset = actualTemp - rawTemp;
        _heater->setTempOffset(offset);
        _storage->saveCalibration(offset, _heater->getCustomNominalR(), _heater->getCustomBeta());
        Serial.print(F(">> Temp calibrated. Offset = "));
        Serial.print(offset, 1);
        Serial.println(F("C applied and saved."));
    }
    else if (strncasecmp(args, "temp ", 5) == 0) {
        float offset = atof(args + 5);
        _heater->setTempOffset(offset);
        _storage->saveCalibration(offset, _heater->getCustomNominalR(), _heater->getCustomBeta());
        Serial.print(F(">> Temp offset set to "));
        Serial.print(offset, 1);
        Serial.println(F("C and saved."));
    }
    else if (strncasecmp(args, "point ", 6) == 0) {
        float actualTemp = atof(args + 6);
        if (actualTemp < -10.0f || actualTemp > 250.0f) {
            Serial.println(F("Usage: cal point <actual temperature in C>"));
            return;
        }
        _heater->addCalibrationPoint(actualTemp);
    }
    else if (strcasecmp(args, "points") == 0) {
        _heater->printCalibrationPoints();
    }
    else if (strcasecmp(args, "table") == 0) {
        _heater->printCalibrationTable();
    }
    else if (strcasecmp(args, "generate") == 0) {
        _heater->generateTableCode();
    }
    else if (strcasecmp(args, "clear points") == 0) {
        _heater->clearCalibrationPoints();
    }
    else if (strcasecmp(args, "reset") == 0) {
        _heater->setTempOffset(0.0f);
        _heater->setCustomThermistor(0.0f, 0.0f);
        _heater->clearCalibrationPoints();
        _storage->saveCalibration(0.0f, 0.0f, 0.0f);
        Serial.println(F(">> Calibration reset. Using compile-time thermistor defaults."));
    }
    else {
        Serial.println(F("Usage: cal temp <offset> | cal temp actual <C> | cal point <C> | cal points | cal generate | cal clear points | cal reset"));
    }
}

void Terminal::cmdTime(const char* args) {
    while (*args == ' ') args++;

    if (!_rtc || !_rtc->isPresent()) {
        Serial.println(F("[RTC] DS3231 not detected."));
        return;
    }

    if (strncasecmp(args, "set ", 4) == 0) {
        const char* p = args + 4;
        uint16_t year = (uint16_t)atoi(p);
        p = nextToken(p);
        uint8_t month = (uint8_t)atoi(p);
        p = nextToken(p);
        uint8_t day = (uint8_t)atoi(p);
        p = nextToken(p);
        uint8_t hour = (uint8_t)atoi(p);
        p = nextToken(p);
        uint8_t minute = (uint8_t)atoi(p);
        p = nextToken(p);
        uint8_t second = (uint8_t)atoi(p);

        if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
            Serial.println(F("Usage: time set YYYY MM DD HH MM SS"));
            return;
        }

        if (_rtc->setDateTime(year, month, day, hour, minute, second)) {
            Serial.print(F(">> RTC set to: "));
            char buf[20];
            _rtc->getFormattedDateTime(buf, sizeof(buf));
            Serial.println(buf);
        } else {
            Serial.println(F("[RTC] Failed to set time."));
        }
    } else {
        // Show current time + RTC temperature
        char buf[20];
        _rtc->getFormattedDateTime(buf, sizeof(buf));
        Serial.print(F("[RTC] "));
        Serial.println(buf);
        float rtcTemp = _rtc->getTemperature();
        if (rtcTemp > -900.0f) {
            Serial.print(F("[RTC] Board temp: "));
            Serial.print(rtcTemp, 1);
            Serial.println(F("C"));
        }
    }
}

const char* Terminal::nextToken(const char* str) {
    while (*str && *str != ' ') str++;
    while (*str == ' ') str++;
    return str;
}
