#ifndef HEATER_H
#define HEATER_H

#include <Arduino.h>
#include "config.h"

// Set to 1 and paste your hardcoded table in heater.cpp to use a compile-time table
#define USE_HARDCODED_CAL_TABLE 1

struct CalibrationPoint {
    uint16_t adc;
    float actualTemp;
};

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

    // Temperature calibration offset (°C)
    void setTempOffset(float offset) { _tempOffset = offset; }
    float getTempOffset() const { return _tempOffset; }

    // Custom thermistor curve (0 = use compile-time defaults)
    void setCustomThermistor(float nominalR, float beta);
    float getCustomNominalR() const { return _customNominalR; }
    float getCustomBeta() const { return _customBeta; }

    // --- Multi-point calibration table ---
    bool addCalibrationPoint(float actualTemp);
    void clearCalibrationPoints();
    uint8_t getCalibrationPointCount() const { return _calCount; }
    void printCalibrationPoints();
    void printCalibrationTable();  // single-line machine-readable
    void generateTableCode();
    bool isUsingCalibrationTable() const { return _calCount >= 2; }
    void loadCalibrationPoints();

private:
    CalibrationPoint _calTable[CALIB_MAX_POINTS];
    uint8_t _calCount;
    uint8_t _currentPWM;
    bool _sensorFailed;
    bool _isShutdown;
    bool _manualMode;
    uint8_t _manualPWM;
    float _tempOffset;
    float _customNominalR;
    float _customBeta;

    float interpolateFromTable(uint16_t adcValue);
    void saveCalibrationPoints();

public:
    // Convert raw ADC to temperature using active thermistor profile
    float adcToTemperature(uint16_t adcValue);

    // Convert raw ADC using arbitrary thermistor constants (for comparison/testing)
    float adcToTemperature(uint16_t adcValue, float nominalR, float beta);
};

#endif // HEATER_H
