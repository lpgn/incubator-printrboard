#ifndef HEATER_H
#define HEATER_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// Heater control — PWM output + NTC thermistor reading
// =============================================================================

// Control-temperature source. Whatever is selected, readControlTemperature()
// falls back to the thermistor if the chosen sensor isn't answering, so
// control is never left templess. Values are EEPROM-persisted — the legacy
// "digital" == 1 maps onto SHT31 (== 1) so old saves migrate transparently.
enum TempSource : uint8_t {
    TEMP_SOURCE_THERMISTOR = 0,
    TEMP_SOURCE_SHT31      = 1,
    TEMP_SOURCE_DHT        = 2
};

// Shared sensors this module can pull a control temperature from
class SHT31Sensor;
class HumiditySensor;

class Heater {
public:
    Heater();

    // Initialize pins
    void begin();

    // Read temperature from thermistor (°C), with oversampling
    float readTemperature();

    // Temperature used for PID control — routes to the selected source
    float readControlTemperature();

    // Control-temperature source selection
    void setTempSource(TempSource src) { _tempSource = src; }
    TempSource getTempSource() const { return _tempSource; }

    // Wire in the shared digital sensors (SHT31 for temp+humidity, the DHT
    // module for DHT-as-control-source). Either may be null.
    void setDigitalSources(SHT31Sensor* sht, HumiditySensor* dht) { _sht31 = sht; _dht = dht; }

    // Is a digital control sensor present and usable right now?
    bool hasDigitalSensor() const;

    // I2C address the SHT31 was probed at (0 if none wired). For status display.
    uint8_t getSht31Address() const;

    // Did the ACTIVE digital source fault mid-run (was working, now lost)?
    // True → control has fallen back to the thermistor and the caller should
    // raise the sensor-fault alarm. Computed in readControlTemperature().
    bool isDigitalFault() const { return _digitalFault; }

    // Thermistor ADC channel (0 or 1). Applied at boot from EEPROM.
    void setThermChannel(uint8_t ch) { _thermChannel = (ch <= 1) ? ch : _thermChannel; }
    uint8_t getThermChannel() const { return _thermChannel; }

    // Read raw summed ADC value (0-THERM_ADC_SUM_MAX) for diagnostics
    uint16_t readRawADC();

    // Set heater target duty (0-255). Actual pin is updated by update() for slow PWM.
    void setOutput(uint8_t pwm);

    // Call frequently in main loop to handle slow PWM pin toggling
    void update();

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

public:
    // Convert raw ADC to temperature using active thermistor profile
    float adcToTemperature(uint16_t adcValue);

    // Convert raw ADC using arbitrary thermistor constants (for comparison/testing)
    float adcToTemperature(uint16_t adcValue, float nominalR, float beta);

private:
    uint8_t _currentPWM;
    bool _sensorFailed;
    bool _isShutdown;
    bool _manualMode;
    uint8_t _manualPWM;
    float _tempOffset;
    float _customNominalR;
    float _customBeta;
    TempSource _tempSource;
    uint8_t _thermChannel;      // ADC channel for the thermistor
    bool _digitalFault;         // Active digital source lost mid-run
    SHT31Sensor* _sht31;        // Shared SHT31 (may be null)
    HumiditySensor* _dht;       // Shared DHT module (may be null)
};

#endif // HEATER_H
