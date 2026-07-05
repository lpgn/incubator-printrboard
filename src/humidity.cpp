#include "humidity.h"
#include "config.h"
#include "sht31.h"

// =============================================================================
// DHT11/DHT22 Bit-Bang Driver Implementation
// Protocol: single-wire, custom timing (not I2C, not 1-Wire Dallas)
// =============================================================================

HumiditySensor::HumiditySensor()
    : _humidity(50.0f), _temperature(25.0f), _failures(0), _present(true),
      _everPresent(false), _lastRedetect(0), _pin(DHT22_PIN), _sht31(nullptr) {}

void HumiditySensor::begin() {
    pinMode(_pin, INPUT_PULLUP);
}

void HumiditySensor::setPin(uint8_t pin) {
    _pin = pin;
    _present = true;   // re-arm detection on the new pin
    _failures = 0;
    pinMode(_pin, INPUT_PULLUP);
}

void HumiditySensor::reset() {
    _present = true;
    _failures = 0;
    pinMode(_pin, INPUT_PULLUP);
}

bool HumiditySensor::usingSht31() const {
    return _sht31 && _sht31->isValid();
}

bool HumiditySensor::read() {
    // Prefer the SHT31 when it has a valid reading (polled in the main loop);
    // this reads its cached value, no extra I2C traffic here.
    if (_sht31 && _sht31->isValid()) {
        _humidity = _sht31->getHumidity();
        _temperature = _sht31->getTemperature();
        _everPresent = true;
        return true;
    }

    if (!_present) {
        // Periodic re-detection: retry every ~10 min so a boot-time glitch
        // doesn't lock the sensor out for the whole run
        unsigned long now = millis();
        if (now - _lastRedetect < 600000UL) {
            return false;
        }
        _lastRedetect = now;
        // Fall through and attempt a read; success re-marks the sensor present
    }

    uint8_t data[5] = {0};

    if (!readRawData(data)) {
        _failures++;
        if (_present && _failures >= 20) {
            _present = false;
            Serial.println(F("[DHT] Sensor not detected — operating without humidity sensor."));
        }
        return false;
    }

    // Verify checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        _failures++;
        return false;
    }

#if DHT_TYPE == DHT_TYPE_DHT22
    // Parse humidity (data[0] MSB, data[1] LSB) — value × 10
    uint16_t rawHumidity = ((uint16_t)data[0] << 8) | data[1];
    _humidity = (float)rawHumidity / 10.0f;

    // Parse temperature (data[2] MSB, data[3] LSB) — value × 10
    // Bit 15 of temperature indicates negative
    uint16_t rawTemp = ((uint16_t)data[2] << 8) | data[3];
    if (rawTemp & 0x8000) {
        rawTemp &= 0x7FFF;
        _temperature = -(float)rawTemp / 10.0f;
    } else {
        _temperature = (float)rawTemp / 10.0f;
    }
#elif DHT_TYPE == DHT_TYPE_DHT11
    // DHT11: 8-bit integer humidity and temperature
    _humidity = (float)data[0];
    _temperature = (float)data[2];
#else
    #error "Unknown DHT_TYPE"
#endif

    // Sanity check
    if (_humidity < 0.0f || _humidity > 100.0f || _temperature < -40.0f || _temperature > 80.0f) {
        _failures++;
        return false;
    }

    if (!_present) {
        _present = true;
        Serial.println(F("[DHT] Sensor detected — humidity readings restored."));
    }
    _everPresent = true;
    _failures = 0;
    return true;
}

bool HumiditySensor::readRawData(uint8_t data[5]) {
    // === START SIGNAL ===
    // DHT11 needs ~18ms+, DHT22 needs ~1ms
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
#if DHT_TYPE == DHT_TYPE_DHT11
    delay(20);
#else
    delay(2);
#endif

    // Release line (pull HIGH via pull-up)
    digitalWrite(_pin, HIGH);
    delayMicroseconds(40);

    // Switch to input to read response
    pinMode(_pin, INPUT_PULLUP);

    // === RESPONSE SIGNAL ===
    // Disable interrupts during timing-critical section — USB SOF on AT90USB1286
    // can fire every 1ms and skew microsecond timing.
    noInterrupts();

    // Wait for LOW (response start) — timeout after 100µs
    uint8_t timeout = 100;
    while (digitalRead(_pin) == HIGH) {
        if (--timeout == 0) { interrupts(); return false; }
        delayMicroseconds(1);
    }

    // Wait for HIGH (response acknowledge)
    timeout = 100;
    while (digitalRead(_pin) == LOW) {
        if (--timeout == 0) { interrupts(); return false; }
        delayMicroseconds(1);
    }

    // Wait for LOW (data transmission start)
    timeout = 100;
    while (digitalRead(_pin) == HIGH) {
        if (--timeout == 0) { interrupts(); return false; }
        delayMicroseconds(1);
    }

    // === READ 40 BITS (5 bytes) ===
    // Each bit: 50µs LOW + 26-28µs HIGH (=0) or 70µs HIGH (=1)
    for (uint8_t i = 0; i < 40; i++) {
        // Wait for HIGH (start of bit)
        timeout = 100;
        while (digitalRead(_pin) == LOW) {
            if (--timeout == 0) { interrupts(); return false; }
            delayMicroseconds(1);
        }

        // Measure HIGH duration to determine bit value
        unsigned long tStart = micros();
        timeout = 100;
        while (digitalRead(_pin) == HIGH) {
            if (--timeout == 0) { interrupts(); return false; }
            delayMicroseconds(1);
        }
        unsigned long tHigh = micros() - tStart;

        // >40µs HIGH = bit 1, otherwise bit 0
        uint8_t byteIndex = i / 8;
        data[byteIndex] <<= 1;
        if (tHigh > 40) {
            data[byteIndex] |= 1;
        }
    }

    interrupts();
    return true;
}

float HumiditySensor::getHumidity() const {
    if (_sht31 && _sht31->isValid()) return _sht31->getHumidity();
    // Surface an invalid reading instead of a fabricated 50%
    return _present ? _humidity : -1.0f;
}

float HumiditySensor::getTemperature() const {
    if (_sht31 && _sht31->isValid()) return _sht31->getTemperature();
    return _temperature;
}

bool HumiditySensor::isPresent() const {
    return (_sht31 && _sht31->isValid()) || _present;
}

bool HumiditySensor::isFailed() const {
    // A valid SHT31 covers humidity even if the DHT is gone
    if (_sht31 && _sht31->isValid()) return false;
    // Otherwise a missing/failing DHT IS a failed source — raise the warning
    return !_present || _failures >= DHT_MAX_FAILURES;
}

float HumiditySensor::getHumidityMidpoint(uint8_t lo, uint8_t hi) {
    return ((float)lo + (float)hi) / 2.0f;
}
