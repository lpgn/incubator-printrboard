#ifndef HEATER_H
#define HEATER_H

#include <Arduino.h>

// =============================================================================
// Heater control — PWM output + NTC thermistor reading
// =============================================================================

class Heater {
public:
    Heater();

    // Initialize pins
    void begin();

    // Read temperature from thermistor (°C), with oversampling
    float readTemperature();

    // Set heater PWM output (0-255)
    void setOutput(uint8_t pwm);

    // Emergency shutdown — heater OFF
    void shutdown();

    // Get current PWM output level
    uint8_t getOutput() const { return _currentPWM; }

    // Check if thermistor reading indicates sensor failure
    bool isSensorFailed() const { return _sensorFailed; }

    // Is heater currently shut down?
    bool isShutdown() const { return _isShutdown; }

    // Allow heater to operate again after shutdown
    void clearShutdown() { _isShutdown = false; }

private:
    uint8_t _currentPWM;
    bool _sensorFailed;
    bool _isShutdown;

    // Convert raw ADC to temperature using Steinhart-Hart
    float adcToTemperature(uint16_t adcValue);
};

#endif // HEATER_H
