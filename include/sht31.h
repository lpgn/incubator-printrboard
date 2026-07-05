#ifndef SHT31_SENSOR_H
#define SHT31_SENSOR_H

#include <Arduino.h>
#include <Adafruit_SHT31.h>

// =============================================================================
// SHT31 Temperature + Humidity Sensor (I2C, shared Wire bus with the DS3231)
//
// Single shared instance feeds BOTH the heater (control temperature) and the
// humidity module (humidity). It is polled once per cadence in the main loop;
// consumers read the cached values — no double I2C traffic, no blocking in the
// consumers. ±0.3°C / ±2%RH — better than the thermistor and far better than
// the DHT11 (integer 1°C / ±5%RH).
// =============================================================================

class SHT31Sensor {
public:
    SHT31Sensor();

    // Probe at the given I2C address (0x44 or 0x45). Wire must ALREADY be
    // initialised elsewhere (the DS3231 driver owns Wire.begin()). Returns
    // true if the chip answers.
    bool begin(uint8_t addr);

    // Poll the sensor, rate-limited internally. Call every loop() iteration.
    void update();

    // Currently present AND last reading valid?
    bool isValid() const { return _present && _valid; }

    // Was the sensor ever seen working? (distinguishes "absent at boot" from
    // "was working, then lost" — only the latter is a mid-run fault)
    bool everPresent() const { return _everPresent; }

    bool isPresent() const { return _present; }

    float getTemperature() const { return _temperature; } // °C, last good
    float getHumidity() const { return _humidity; }       // %RH, last good

    uint8_t getAddress() const { return _addr; }

private:
    Adafruit_SHT31 _dev;
    uint8_t _addr;
    bool _present;
    bool _valid;
    bool _everPresent;
    uint8_t _failures;
    unsigned long _lastRead;
    float _temperature;   // °C, last good reading
    float _humidity;      // %RH, last good reading
};

#endif // SHT31_SENSOR_H
