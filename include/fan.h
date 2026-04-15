#ifndef FAN_H
#define FAN_H

#include <Arduino.h>

// =============================================================================
// Fan Control — Dual purpose: temperature regulation + humidity regulation
// =============================================================================

class FanController {
public:
    FanController();

    // Initialize fan pin
    void begin();

    // Update fan speed based on temperature and humidity conditions
    // tempError: setpoint - current (positive = too cold, negative = too hot)
    // humidError: midpoint - current (positive = too dry, negative = too wet)
    void update(float tempError, float humidError);

    // Manual override (0-255), set to -1 to return to auto
    void setManualSpeed(int16_t speed);

    // Set speed range
    void setSpeedRange(uint8_t min, uint8_t max);

    // Get current fan speed (0-255 PWM)
    uint8_t getSpeed() const { return _currentSpeed; }

    // Get speed as percentage
    uint8_t getSpeedPercent() const;

    // Force full speed (for safety override)
    void fullSpeed();

    // Is fan in manual mode?
    bool isManual() const { return _manualMode; }

private:
    uint8_t _currentSpeed;
    uint8_t _minSpeed;
    uint8_t _maxSpeed;
    bool _manualMode;
    int16_t _manualSpeed;
};

#endif // FAN_H
