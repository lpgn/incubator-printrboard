#include "heater.h"
#include "config.h"
#include <math.h>

// =============================================================================
// Heater Implementation
// =============================================================================

Heater::Heater()
    : _currentPWM(0), _sensorFailed(false), _isShutdown(false) {}

void Heater::begin() {
    pinMode(HEATER_PIN, OUTPUT);
    analogWrite(HEATER_PIN, 0); // Start with heater OFF
}

float Heater::readTemperature() {
    // Oversample ADC for noise reduction
    uint32_t sum = 0;
    for (uint8_t i = 0; i < THERM_OVERSAMPLE; i++) {
        sum += analogRead(THERMISTOR_PIN);
    }
    uint16_t adcValue = sum / THERM_OVERSAMPLE;

    // Check for sensor failure (open circuit or short)
    if (adcValue <= TEMP_SENSOR_FAIL_LO || adcValue >= TEMP_SENSOR_FAIL_HI) {
        _sensorFailed = true;
        return -999.0f; // Invalid reading
    }

    _sensorFailed = false;
    return adcToTemperature(adcValue);
}

void Heater::setOutput(uint8_t pwm) {
    if (_isShutdown) {
        pwm = 0; // Enforce shutdown
    }
    _currentPWM = pwm;
    analogWrite(HEATER_PIN, _currentPWM);
}

void Heater::shutdown() {
    _isShutdown = true;
    _currentPWM = 0;
    analogWrite(HEATER_PIN, 0);
}

float Heater::adcToTemperature(uint16_t adcValue) {
    // Convert ADC reading to resistance
    // Voltage divider: Vout = Vcc * R_therm / (R_series + R_therm)
    // ADC = Vout / Vcc * ADC_MAX
    // So: R_therm = R_series * ADC / (ADC_MAX - ADC)
    float resistance = THERM_SERIES_R * ((float)adcValue / (float)(THERM_ADC_MAX - adcValue));

    // Simplified Steinhart-Hart using Beta equation:
    // 1/T = 1/T0 + (1/B) * ln(R/R0)
    float steinhart;
    steinhart = resistance / THERM_NOMINAL_R;         // R/R0
    steinhart = log(steinhart);                         // ln(R/R0)
    steinhart /= THERM_BETA;                           // (1/B) * ln(R/R0)
    steinhart += 1.0f / (THERM_NOMINAL_T + 273.15f);  // + 1/T0
    steinhart = 1.0f / steinhart;                       // Invert
    steinhart -= 273.15f;                               // Convert to °C

    return steinhart;
}
