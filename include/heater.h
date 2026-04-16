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

    // Read raw averaged ADC value (0-1023) for diagnostics
    uint16_t readRawADC();

    // Set heater PWM output (0-255)
    void setOutput(uint8_t pwm);

    // Manual override (0-255), set to -1 to return to auto
    void setManualSpeed(int16_t speed);

    // Is heater in manual mode?
    bool isManual() const { return _manualMode; }

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
    bool _manualMode;
    uint8_t _manualPWM;

    // Convert raw ADC to temperature using Steinhart-Hart
    float adcToTemperature(uint16_t adcValue);

public:
    // Convert raw ADC using arbitrary thermistor constants (for comparison/testing)
    float adcToTemperature(uint16_t adcValue, float nominalR, float beta);
};

#endif // HEATER_H
