#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>

// =============================================================================
// Safety Monitor — Over-temp, under-temp, sensor failure, humidity alarms
// =============================================================================

class SafetyMonitor {
public:
    SafetyMonitor();

    // Initialize buzzer/LED pins
    void begin();

    // Check all safety conditions. Returns true if an alarm is active.
    // Sets alarm flags and triggers buzzer/LED as needed.
    bool check(float temperature, float humidity, bool thermSensorFailed, bool dhtFailed);

    // Individual alarm queries
    bool isOverTemp() const { return _overTemp; }
    bool isUnderTemp() const { return _underTemp; }
    bool isSensorFailed() const { return _sensorFail; }
    bool isHumidityHigh() const { return _humidHigh; }
    bool isHumidityLow() const { return _humidLow; }
    bool isAnyAlarm() const { return _overTemp || _underTemp || _sensorFail || _humidHigh || _humidLow; }

    // Override safety shutdowns — allows testing even with alarms active
    void setOverride(bool overridden);
    bool isOverridden() const { return _overridden; }

    // Adjustable max safety temperature (default 40°C, adjustable 35-50°C)
    void setMaxTemp(float temp);
    float getMaxTemp() const { return _maxTemp; }

    // Silence the buzzer (alarm flags remain, but buzzer stops)
    void silenceAlarm();

    // Clear all alarms (after issue is resolved)
    void clearAlarms();

    // Set the buzzer state manually
    void buzz(bool on);

    // Blink the LED
    void updateLED();

private:
    bool _overTemp;
    bool _underTemp;
    bool _sensorFail;
    bool _humidHigh;
    bool _humidLow;
    bool _silenced;
    bool _overridden;
    float _maxTemp;

    // Sustained alarm timing
    unsigned long _overTempStart;
    bool _overTempTiming;
    unsigned long _underTempStart;
    bool _underTempTiming;

    // Buzzer pattern
    unsigned long _lastBuzzToggle;
    bool _buzzState;

    // LED blink
    unsigned long _lastLEDBlink;
    bool _ledState;
};

#endif // SAFETY_H
