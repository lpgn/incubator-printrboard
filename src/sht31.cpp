#include "sht31.h"
#include "config.h"
#include <math.h>

SHT31Sensor::SHT31Sensor()
    : _addr(SHT31_I2C_ADDR_DEFAULT), _present(false), _valid(false),
      _everPresent(false), _failures(0), _lastRead(0) {}

bool SHT31Sensor::begin(uint8_t addr) {
    _addr = addr;
    // Adafruit_SHT31::begin() talks over the shared Wire bus (already
    // initialised by the RTC driver). It reads the status register, so a
    // true return means the chip is really there.
    if (_dev.begin(addr)) {
        _present = true;
        _valid = false;      // no measurement yet
        _everPresent = true;
        _failures = 0;
        _lastRead = millis() - SHT31_READ_INTERVAL_MS; // read on next update()
        return true;
    }
    _present = false;
    return false;
}

void SHT31Sensor::update() {
    if (!_present) return; // absent — nothing to poll (re-detect via 'set temppin')

    unsigned long now = millis();
    if (now - _lastRead < SHT31_READ_INTERVAL_MS) return;
    _lastRead = now;

    // Single-shot high-repeatability read (~15ms inside the library) — well
    // within the 4s watchdog at this 1Hz cadence.
    float t = _dev.readTemperature();
    float h = _dev.readHumidity();

    if (isnan(t) || isnan(h) || t < -40.0f || t > 125.0f || h < 0.0f || h > 100.0f) {
        if (++_failures >= 3) {
            _valid = false;
            _present = false; // mark lost; everPresent stays true → mid-run fault
            Serial.println(F("[SHT31] Sensor stopped responding."));
        }
        return;
    }

    _temperature = t;
    _humidity = h;
    _valid = true;
    _failures = 0;
}
