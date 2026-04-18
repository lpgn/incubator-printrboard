#include "safety.h"
#include "config.h"

SafetyMonitor::SafetyMonitor()
    : _overTemp(false), _underTemp(false), _sensorFail(false),
      _humidHigh(false), _humidLow(false), _silenced(false), _overridden(false),
      _maxTemp(40.0f),
      _overTempStart(0), _overTempTiming(false),
      _underTempStart(0), _underTempTiming(false),
      _lastBuzzToggle(0), _buzzState(false),
      _lastLEDBlink(0), _ledState(false) {}

void SafetyMonitor::begin() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
}

bool SafetyMonitor::check(float temperature, float humidity,
                          bool thermSensorFailed, bool dhtFailed) {
    bool anyAlarm = false;

    // --- Thermistor sensor failure ---
    if (thermSensorFailed) {
        if (!_sensorFail) {
            _sensorFail = true;
            Serial.println(F("!!! ALARM: Thermistor sensor FAILED !!!"));
            Serial.println(F("!!! Heater shut down for safety   !!!"));
        }
        anyAlarm = true;
    } else {
        if (_sensorFail) {
            Serial.println(F("RECOVERED: Thermistor sensor OK"));
        }
        _sensorFail = false;
    }

    // --- Over-temperature (sustained, 2 seconds to avoid preheat spikes) ---
    if (!thermSensorFailed && temperature > _maxTemp) {
        if (!_overTempTiming) {
            _overTempTiming = true;
            _overTempStart = millis();
        } else if (millis() - _overTempStart > 2000UL) {
            if (!_overTemp) {
                _overTemp = true;
                Serial.print(F("!!! ALARM: OVER-TEMP "));
                Serial.print(temperature, 1);
                Serial.print(F("C > "));
                Serial.print(_maxTemp, 1);
                Serial.println(F("C !!!"));
            }
            anyAlarm = true;
        }
    } else {
        if (_overTemp) {
            Serial.print(F("RECOVERED: Over-temp cleared. Temp "));
            Serial.print(temperature, 1);
            Serial.println(F("C"));
        }
        _overTempTiming = false;
        _overTemp = false;
    }

    // --- Under-temperature (sustained) ---
    if (!thermSensorFailed && temperature < TEMP_MIN_WARNING && temperature > 0.0f) {
        if (!_underTempTiming) {
            _underTempTiming = true;
            _underTempStart = millis();
        } else if (millis() - _underTempStart > TEMP_MIN_WARN_MS) {
            if (!_underTemp) {
                _underTemp = true;
                Serial.print(F("!!! WARNING: Under-temp "));
                Serial.print(temperature, 1);
                Serial.print(F("C for >10 min !!!"));
                Serial.println();
            }
            anyAlarm = true;
        }
    } else {
        if (_underTemp) {
            Serial.print(F("RECOVERED: Under-temp cleared. Temp "));
            Serial.print(temperature, 1);
            Serial.println(F("C"));
        }
        _underTempTiming = false;
        _underTemp = false;
    }

    // --- Humidity high ---
    if (!dhtFailed && humidity > HUMIDITY_MAX_ALARM) {
        if (!_humidHigh) {
            _humidHigh = true;
            Serial.print(F("!!! WARNING: Humidity HIGH "));
            Serial.print(humidity, 1);
            Serial.println(F("% !!!"));
        }
        anyAlarm = true;
    } else {
        if (_humidHigh) {
            Serial.print(F("RECOVERED: Humidity HIGH cleared. Humidity "));
            Serial.print(humidity, 1);
            Serial.println(F("%"));
        }
        _humidHigh = false;
    }

    // --- Humidity low ---
    if (!dhtFailed && humidity < HUMIDITY_MIN_ALARM && humidity > 0.0f) {
        if (!_humidLow) {
            _humidLow = true;
            Serial.print(F("!!! WARNING: Humidity LOW "));
            Serial.print(humidity, 1);
            Serial.println(F("% — Refill water tray! !!!"));
        }
        anyAlarm = true;
    } else {
        if (_humidLow) {
            Serial.print(F("RECOVERED: Humidity LOW cleared. Humidity "));
            Serial.print(humidity, 1);
            Serial.println(F("%"));
        }
        _humidLow = false;
    }

    // --- DHT sensor failure (informational, not critical) ---
    if (dhtFailed) {
        // Don't set a hard alarm, just report periodically
        static unsigned long lastDHTWarn = 0;
        if (millis() - lastDHTWarn > 30000) {
            Serial.println(F("WARNING: DHT sensor read failures — using last known humidity."));
            lastDHTWarn = millis();
        }
    }

    // --- Buzzer control ---
    if (anyAlarm && !_silenced && !_overridden) {
        unsigned long now = millis();
        // Beep pattern: 500ms on, 500ms off for critical, 200ms on/1800ms off for warnings
        uint16_t onTime = (_overTemp || _sensorFail) ? 500 : 200;
        uint16_t offTime = (_overTemp || _sensorFail) ? 500 : 1800;
        uint16_t period = _buzzState ? onTime : offTime;

        if (now - _lastBuzzToggle >= period) {
            _buzzState = !_buzzState;
            digitalWrite(BUZZER_PIN, _buzzState ? HIGH : LOW);
            _lastBuzzToggle = now;
        }
    } else {
        digitalWrite(BUZZER_PIN, LOW);
        _buzzState = false;
    }

    return anyAlarm;
}

void SafetyMonitor::silenceAlarm() {
    _silenced = true;
    digitalWrite(BUZZER_PIN, LOW);
}

void SafetyMonitor::setMaxTemp(float temp) {
    if (temp < 35.0f) temp = 35.0f;
    if (temp > 50.0f) temp = 50.0f;
    _maxTemp = temp;
}

void SafetyMonitor::setOverride(bool overridden) {
    _overridden = overridden;
    if (_overridden) {
        digitalWrite(BUZZER_PIN, LOW);
        _buzzState = false;
    }
}

void SafetyMonitor::clearAlarms() {
    _overTemp = false;
    _underTemp = false;
    _sensorFail = false;
    _humidHigh = false;
    _humidLow = false;
    _silenced = false;
    _overTempTiming = false;
    _underTempTiming = false;
    digitalWrite(BUZZER_PIN, LOW);
}

void SafetyMonitor::buzz(bool on) {
    digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
}

void SafetyMonitor::updateLED() {
    unsigned long now = millis();
    uint16_t interval = isAnyAlarm() ? 200 : 1000; // Fast blink if alarm, slow if OK

    if (now - _lastLEDBlink >= interval) {
        _ledState = !_ledState;
        digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
        _lastLEDBlink = now;
    }
}
