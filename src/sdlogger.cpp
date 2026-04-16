#include "sdlogger.h"
#include "config.h"

#define LOG_FILE    "LOGFILE.TXT"
#define STATE_FILE  "STATE.TXT"

SDLogger::SDLogger() : _ready(false), _csPin(255) {}

bool SDLogger::begin(uint8_t csPin) {
    _csPin = csPin;
    pinMode(csPin, OUTPUT);

    if (!SD.begin(csPin)) {
        _ready = false;
        return false;
    }
    _ready = true;

    // Ensure log file has a header
    ensureHeader(LOG_FILE);
    return true;
}

bool SDLogger::isReady() const {
    return _ready;
}

bool SDLogger::ensureHeader(const char* filename) {
    if (!_ready) return false;

    // Only write header if file does not exist or is empty
    if (SD.exists(filename)) {
        File f = SD.open(filename, FILE_READ);
        if (f && f.size() > 0) {
            f.close();
            return true;
        }
        if (f) f.close();
    }

    File f = SD.open(filename, FILE_WRITE);
    if (!f) return false;
    f.println(F("ms,temp_c,humidity_pct,heater_pct,fan_pct,state,day,turns"));
    f.close();
    return true;
}

bool SDLogger::writeLog(uint32_t millisVal, float tempC, float humidityPct,
                        uint8_t heaterPct, uint8_t fanPct,
                        const char* stateName, uint16_t day, uint8_t turnsToday) {
    if (!_ready) return false;

    File f = SD.open(LOG_FILE, FILE_WRITE);
    if (!f) return false;

    f.print(millisVal);
    f.print(',');
    f.print(tempC, 2);
    f.print(',');
    f.print(humidityPct, 1);
    f.print(',');
    f.print(heaterPct);
    f.print(',');
    f.print(fanPct);
    f.print(',');
    f.print(stateName);
    f.print(',');
    f.print(day);
    f.print(',');
    f.println(turnsToday);

    f.close();
    return true;
}

bool SDLogger::writeState(uint8_t speciesID, uint8_t state, uint32_t elapsedSeconds,
                          uint16_t currentDay, uint8_t turnsToday,
                          float targetTemp, float humidityTarget) {
    if (!_ready) return false;

    // Remove old state file first
    if (SD.exists(STATE_FILE)) {
        SD.remove(STATE_FILE);
    }

    File f = SD.open(STATE_FILE, FILE_WRITE);
    if (!f) return false;

    f.print(F("species="));      f.println(speciesID);
    f.print(F("state="));        f.println(state);
    f.print(F("elapsed="));      f.println(elapsedSeconds);
    f.print(F("day="));          f.println(currentDay);
    f.print(F("turns="));        f.println(turnsToday);
    f.print(F("targetTemp="));   f.println(targetTemp, 1);
    f.print(F("targetHumid="));  f.println(humidityTarget, 1);

    f.close();
    return true;
}

void SDLogger::printStatus() {
    Serial.print(F("[SD] Status: "));
    if (_ready) {
        Serial.println(F("READY"));
    } else {
        Serial.println(F("NOT READY (no card or init failed)"));
    }
}
