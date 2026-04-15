#ifndef HUMIDITY_H
#define HUMIDITY_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// DHT22 Temperature & Humidity Sensor Driver
// Bit-bang 1-wire protocol — no external library needed
// =============================================================================

class HumiditySensor {
public:
    HumiditySensor();

    // Initialize the sensor pin
    void begin();

    // Read sensor data. Returns true on success.
    // Call no more than once every 2 seconds (DHT22 limitation).
    bool read();

    // Get last successful readings
    float getHumidity() const;
    float getTemperature() const { return _temperature; }

    // Get consecutive failure count
    uint8_t getFailureCount() const { return _failures; }

    // Is sensor considered failed? (3+ consecutive failures while present)
    bool isFailed() const;

    // Is sensor physically present? Goes false after many consecutive failures.
    bool isPresent() const { return _present; }

    // Get midpoint humidity target for current phase
    static float getHumidityMidpoint(uint8_t lo, uint8_t hi);

private:
    float _humidity;
    float _temperature;
    uint8_t _failures;
    bool _present;

    // Low-level bit-bang protocol
    bool readRawData(uint8_t data[5]);
};

#endif // HUMIDITY_H
