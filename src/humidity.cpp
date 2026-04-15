#include "humidity.h"
#include "config.h"

// =============================================================================
// DHT22 Bit-Bang Driver Implementation
// Protocol: single-wire, custom timing (not I2C, not 1-Wire Dallas)
// =============================================================================

HumiditySensor::HumiditySensor()
    : _humidity(50.0f), _temperature(25.0f), _failures(0), _present(true) {}

void HumiditySensor::begin() {
    pinMode(DHT22_PIN, INPUT_PULLUP);
}

bool HumiditySensor::read() {
    if (!_present) {
        return false;
    }

    uint8_t data[5] = {0};

    if (!readRawData(data)) {
        _failures++;
        if (_present && _failures >= 20) {
            _present = false;
            _humidity = 50.0f;
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

    // Sanity check
    if (_humidity < 0.0f || _humidity > 100.0f || _temperature < -40.0f || _temperature > 80.0f) {
        _failures++;
        return false;
    }

    _failures = 0;
    return true;
}

bool HumiditySensor::readRawData(uint8_t data[5]) {
    // === START SIGNAL ===
    // Pull data line LOW for at least 1ms (we use 2ms)
    pinMode(DHT22_PIN, OUTPUT);
    digitalWrite(DHT22_PIN, LOW);
    delay(2);

    // Release line (pull HIGH via pull-up)
    digitalWrite(DHT22_PIN, HIGH);
    delayMicroseconds(40);

    // Switch to input to read response
    pinMode(DHT22_PIN, INPUT_PULLUP);

    // === RESPONSE SIGNAL ===
    // DHT22 pulls LOW for ~80µs, then HIGH for ~80µs

    // Wait for LOW (response start) — timeout after 100µs
    uint8_t timeout = 100;
    while (digitalRead(DHT22_PIN) == HIGH) {
        if (--timeout == 0) return false;
        delayMicroseconds(1);
    }

    // Wait for HIGH (response acknowledge)
    timeout = 100;
    while (digitalRead(DHT22_PIN) == LOW) {
        if (--timeout == 0) return false;
        delayMicroseconds(1);
    }

    // Wait for LOW (data transmission start)
    timeout = 100;
    while (digitalRead(DHT22_PIN) == HIGH) {
        if (--timeout == 0) return false;
        delayMicroseconds(1);
    }

    // === READ 40 BITS (5 bytes) ===
    // Each bit: 50µs LOW + 26-28µs HIGH (=0) or 70µs HIGH (=1)
    for (uint8_t i = 0; i < 40; i++) {
        // Wait for HIGH (start of bit)
        timeout = 100;
        while (digitalRead(DHT22_PIN) == LOW) {
            if (--timeout == 0) return false;
            delayMicroseconds(1);
        }

        // Measure HIGH duration to determine bit value
        unsigned long tStart = micros();
        timeout = 100;
        while (digitalRead(DHT22_PIN) == HIGH) {
            if (--timeout == 0) return false;
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

    return true;
}

float HumiditySensor::getHumidity() const {
    return _present ? _humidity : 50.0f;
}

bool HumiditySensor::isFailed() const {
    return _present && _failures >= DHT_MAX_FAILURES;
}

float HumiditySensor::getHumidityMidpoint(uint8_t lo, uint8_t hi) {
    return ((float)lo + (float)hi) / 2.0f;
}
