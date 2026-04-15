#include "terminal.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

Terminal::Terminal()
    : _bufPos(0), _lastAutoReport(0),
      _sm(nullptr), _pid(nullptr), _heater(nullptr), _humid(nullptr),
      _turner(nullptr), _fan(nullptr), _clock(nullptr), _storage(nullptr),
      _safety(nullptr) {}

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
                             SoftClock* clock, Storage* storage, SafetyMonitor* safety) {
    _sm = sm;
    _pid = pid;
    _heater = heater;
    _humid = humid;
    _turner = turner;
    _fan = fan;
    _clock = clock;
    _storage = storage;
    _safety = safety;
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

    if (state == STATE_IDLE) {
        Serial.print(F("[IDLE] T="));
        Serial.print(temp, 1);
        Serial.print(F("C H="));
        Serial.print(humidity, 0);
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
    Serial.print(F("C H="));
    Serial.print(humidity, 0);
    Serial.print(F("% HTR="));
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
    Serial.println(F("  set humidity <lo> <hi>  Override humidity range (%)"));
    Serial.println(F("  set pid <Kp> <Ki> <Kd>  Set PID tuning"));
    Serial.println(F("  set turns <N>        Set turns per day"));
    Serial.println(F("  set fan <min> <max>  Set fan speed range (0-255)"));
    Serial.println(F("  turn                 Force an immediate egg turn"));
    Serial.println(F("  log                  Show event log"));
    Serial.println(F("  silence              Silence buzzer alarm"));
    Serial.println(F("  reset                Factory reset (clear EEPROM)"));
    Serial.println(F("  help                 Show this help"));
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

    _sm->startPreheating(species);
    _clock->start();
    _turner->setTurnsPerDay(p.turnsPerDay);
    _pid->setSetpoint(_sm->getTargetTemp());

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
    Serial.print(F("C (target: "));
    Serial.print(_sm->getTargetTemp(), 1);
    Serial.println(F("C)"));

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
        if (temp < 30.0f || temp > 42.0f) {
            Serial.println(F("Temperature must be 30-42C."));
            return;
        }
        _sm->setTempOverride(temp);
        _pid->setSetpoint(temp);
        Serial.print(F(">> Temperature setpoint: "));
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
    else {
        Serial.println(F("Usage: set temp|humidity|pid|turns|fan <values>"));
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

const char* Terminal::nextToken(const char* str) {
    while (*str && *str != ' ') str++;
    while (*str == ' ') str++;
    return str;
}
