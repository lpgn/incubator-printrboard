#ifndef PID_H
#define PID_H

#include <Arduino.h>

// =============================================================================
// PID Controller with anti-windup and autotune
// =============================================================================

class PIDController {
public:
    PIDController();

    // Initialize with tuning parameters
    void begin(float kp, float ki, float kd);

    // Set tuning parameters
    void setTunings(float kp, float ki, float kd);

    // Get current tuning parameters
    float getKp() const { return _kp; }
    float getKi() const { return _ki; }
    float getKd() const { return _kd; }

    // Set output limits
    void setOutputLimits(int16_t min, int16_t max);

    // Set setpoint
    void setSetpoint(float sp) { _setpoint = sp; }
    float getSetpoint() const { return _setpoint; }

    // Compute PID output — call at regular intervals (PID_SAMPLE_MS)
    // Returns output value (0-255 for PWM)
    int16_t compute(float currentTemp);

    // Reset integral and derivative state
    void reset();

    // Get last computed output
    int16_t getOutput() const { return _output; }

    // --- Autotune (Ziegler-Nichols relay method) ---

    // Start autotune at the given setpoint temperature
    void autotuneStart(float setpoint);

    // Call this each PID cycle during autotune
    // Returns true when autotune is complete
    bool autotuneUpdate(float currentTemp);

    // Cancel autotune
    void autotuneCancel();

    // Is autotune currently running?
    bool isAutotuning() const { return _autotuning; }

    // Get autotune heater output (0 or max)
    int16_t getAutotuneOutput() const { return _atOutput; }

private:
    // PID parameters
    float _kp, _ki, _kd;
    float _setpoint;

    // PID state
    float _integral;
    float _lastInput;
    int16_t _output;
    int16_t _outMin, _outMax;
    bool _firstCompute;

    // Autotune state
    bool _autotuning;
    float _atSetpoint;
    float _atHysteresis;
    int16_t _atOutput;
    bool _atHeating;            // Currently in heating phase
    uint8_t _atCycleCount;
    uint8_t _atTargetCycles;
    unsigned long _atLastToggle;
    float _atMaxTemp;
    float _atMinTemp;
    float _atPeriodSum;
    float _atAmplitudeSum;
    uint8_t _atCompletedCycles;
    unsigned long _atLastLogTime;
};

#endif // PID_H
