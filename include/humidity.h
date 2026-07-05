#ifndef HUMIDITY_H
#define HUMIDITY_H

#include <Arduino.h>
#include "config.h"

class SHT31Sensor;

// =============================================================================
// Humidity source: prefers a shared SHT31 (±2%RH) when present, otherwise
// falls back to the built-in DHT11/DHT22 bit-bang driver (±5%RH). The DHT path
// is kept intact as the fallback.
// =============================================================================

class HumiditySensor {
public:
    HumiditySensor();

    // Initialize the sensor pin
    void begin();

    // Prefer this shared SHT31 for humidity/temperature when it is valid
    void setSht31(SHT31Sensor* sht) { _sht31 = sht; }

    // DHT digital pin (applied at boot from EEPROM). Re-arms detection.
    void setPin(uint8_t pin);
    uint8_t getPin() const { return _pin; }

    // Read sensor data. Returns true on success.
    // Call no more than once every 2 seconds (DHT22 limitation; DHT11 can be read every 1s).
    bool read();

    // Get last successful readings (SHT31 when valid, else DHT)
    float getHumidity() const;
    float getTemperature() const;

    // Get consecutive failure count
    uint8_t getFailureCount() const { return _failures; }

    // Is the humidity source failed? (no SHT31, and DHT absent/failing)
    bool isFailed() const;

    // Is a humidity source physically present? (SHT31 valid, or DHT present)
    bool isPresent() const;

    // Was a humidity source ever seen working? (for mid-run fault detection)
    bool everPresent() const { return _everPresent; }

    // Reset presence and failures so the sensor is re-detected
    void reset();

    // Get midpoint humidity target for current phase
    static float getHumidityMidpoint(uint8_t lo, uint8_t hi);

    // Which source produced the last reading? (for status display)
    bool usingSht31() const;

private:
    float _humidity;
    float _temperature;
    uint8_t _failures;
    bool _present;             // DHT presence
    bool _everPresent;         // Any source ever worked
    unsigned long _lastRedetect;  // Last re-detection attempt while absent
    uint8_t _pin;              // DHT digital pin
    SHT31Sensor* _sht31;       // Shared SHT31 (may be null)

    // Low-level bit-bang protocol
    bool readRawData(uint8_t data[5]);
};

#endif // HUMIDITY_H
