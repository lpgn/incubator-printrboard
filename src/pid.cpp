#include "pid.h"
#include "config.h"

// =============================================================================
// PID Controller Implementation
// =============================================================================

PIDController::PIDController()
    : _kp(PID_DEFAULT_KP), _ki(PID_DEFAULT_KI), _kd(PID_DEFAULT_KD),
      _setpoint(37.5f), _integral(0.0f), _lastInput(0.0f),
      _output(0), _outMin(PID_OUTPUT_MIN), _outMax(PID_OUTPUT_MAX),
      _firstCompute(true), _autotuning(false) {}

void PIDController::begin(float kp, float ki, float kd) {
    setTunings(kp, ki, kd);
    reset();
}

void PIDController::setTunings(float kp, float ki, float kd) {
    _kp = kp;
    _ki = ki;
    _kd = kd;
}

void PIDController::setOutputLimits(int16_t min, int16_t max) {
    _outMin = min;
    _outMax = max;
    // Clamp existing output
    if (_output > _outMax) _output = _outMax;
    if (_output < _outMin) _output = _outMin;
}

int16_t PIDController::compute(float currentTemp) {
    float error = _setpoint - currentTemp;

    // Integral with anti-windup
    _integral += _ki * error;
    if (_integral > PID_WINDUP_LIMIT) _integral = PID_WINDUP_LIMIT;
    if (_integral < -PID_WINDUP_LIMIT) _integral = -PID_WINDUP_LIMIT;

    // Derivative on measurement (not on error, avoids derivative kick)
    float derivative = 0.0f;
    if (!_firstCompute) {
        derivative = currentTemp - _lastInput; // note: negative of d(error)
    }
    _firstCompute = false;
    _lastInput = currentTemp;

    // PID output
    float output = (_kp * error) + _integral - (_kd * derivative);

    // Clamp output
    if (output > (float)_outMax) output = (float)_outMax;
    if (output < (float)_outMin) output = (float)_outMin;

    _output = (int16_t)output;
    return _output;
}

void PIDController::reset() {
    _integral = 0.0f;
    _lastInput = 0.0f;
    _output = 0;
    _firstCompute = true;
}

// =============================================================================
// PID Autotune — Ziegler-Nichols Relay Method
// Same approach as Marlin's M303
// =============================================================================

void PIDController::autotuneStart(float setpoint) {
    _autotuning = true;
    _atSetpoint = setpoint;
    _atHysteresis = AUTOTUNE_HYSTERESIS;
    _atOutput = _outMax;
    _atHeating = true;
    _atCycleCount = 0;
    _atTargetCycles = AUTOTUNE_CYCLES;
    _atLastToggle = millis();
    _atLastLogTime = millis();
    _atMaxTemp = 0.0f;
    _atMinTemp = 999.0f;
    _atPeriodSum = 0.0f;
    _atAmplitudeSum = 0.0f;
    _atCompletedCycles = 0;

    Serial.println(F("[AUTOTUNE] Starting PID autotune..."));
    Serial.print(F("[AUTOTUNE] Setpoint: "));
    Serial.print(setpoint, 1);
    Serial.print(F("C, Cycles: "));
    Serial.println(_atTargetCycles);
    Serial.println(F("[AUTOTUNE] Heating to setpoint..."));
}

bool PIDController::autotuneUpdate(float currentTemp) {
    if (!_autotuning) return false;

    // Track min/max temperature in current half-cycle
    if (currentTemp > _atMaxTemp) _atMaxTemp = currentTemp;
    if (currentTemp < _atMinTemp) _atMinTemp = currentTemp;

    bool shouldToggle = false;

    if (_atHeating && currentTemp > _atSetpoint + _atHysteresis) {
        // Crossed above setpoint — switch to cooling
        shouldToggle = true;
    } else if (!_atHeating && currentTemp < _atSetpoint - _atHysteresis) {
        // Crossed below setpoint — switch to heating
        shouldToggle = true;
    }

    if (shouldToggle) {
        unsigned long now = millis();
        unsigned long halfPeriod = now - _atLastToggle;
        _atLastToggle = now;

        if (_atHeating) {
            // Was heating, now cooling
            _atOutput = _outMin;
            _atHeating = false;
            Serial.print(F("[AUTOTUNE] >> Switched to COOLING at "));
            Serial.print(currentTemp, 1);
            Serial.println(F("C"));
        } else {
            // Was cooling, now heating — completes one full cycle
            _atOutput = _outMax;
            _atHeating = true;
            Serial.print(F("[AUTOTUNE] >> Switched to HEATING at "));
            Serial.print(currentTemp, 1);
            Serial.println(F("C"));

            if (_atCycleCount > 0) {
                // Record this cycle's data (skip first cycle as it warms from cold)
                float amplitude = (_atMaxTemp - _atMinTemp) / 2.0f;
                _atAmplitudeSum += amplitude;

                // Period is time for full cycle — we approximate from 2x the last half
                // But more accurately we track full cycles
                _atPeriodSum += (float)halfPeriod * 2.0f;  // rough estimate
                _atCompletedCycles++;

                Serial.print(F("[AUTOTUNE] Cycle "));
                Serial.print(_atCompletedCycles);
                Serial.print(F("/"));
                Serial.print(_atTargetCycles);
                Serial.print(F("  Amplitude: "));
                Serial.print(amplitude, 2);
                Serial.print(F("C  Max: "));
                Serial.print(_atMaxTemp, 1);
                Serial.print(F("  Min: "));
                Serial.println(_atMinTemp, 1);

                // Print running estimates so user can abort gracefully
                float avgAmplitude = _atAmplitudeSum / (float)_atCompletedCycles;
                float avgPeriod = _atPeriodSum / (float)_atCompletedCycles / 1000.0f;
                float Ku = (4.0f * (float)(_outMax - _outMin)) / (3.14159f * (2.0f * avgAmplitude));
                float Tu = avgPeriod;
                float newKp = 0.6f * Ku;
                float newKi = 1.2f * Ku / Tu;
                float newKd = 0.075f * Ku * Tu;

                Serial.print(F("[AUTOTUNE] Intermediate PID Projection -> Kp: "));
                Serial.print(newKp, 2);
                Serial.print(F(" | Ki: "));
                Serial.print(newKi, 3);
                Serial.print(F(" | Kd: "));
                Serial.println(newKd, 2);
            }

            _atCycleCount++;
            _atMaxTemp = 0.0f;
            _atMinTemp = 999.0f;

            // Check if we've completed enough cycles
            if (_atCompletedCycles >= _atTargetCycles) {
                // Calculate Ziegler-Nichols parameters
                float avgAmplitude = _atAmplitudeSum / (float)_atCompletedCycles;
                float avgPeriod = _atPeriodSum / (float)_atCompletedCycles / 1000.0f; // to seconds

                // Ultimate gain: Ku = (4 * d) / (π * a)
                // where d = relay output amplitude, a = process oscillation amplitude
                float d = (float)(_outMax - _outMin);
                float Ku = (4.0f * d) / (3.14159f * (2.0f * avgAmplitude));
                float Tu = avgPeriod; // Ultimate period in seconds

                // Ziegler-Nichols PID tuning rules
                float newKp = 0.6f * Ku;
                float newKi = 1.2f * Ku / Tu;
                float newKd = 0.075f * Ku * Tu;

                Serial.println(F(""));
                Serial.println(F("[AUTOTUNE] ========== COMPLETE =========="));
                Serial.print(F("[AUTOTUNE] Ku="));
                Serial.print(Ku, 3);
                Serial.print(F("  Tu="));
                Serial.print(Tu, 2);
                Serial.println(F("s"));
                Serial.print(F("[AUTOTUNE] New PID: Kp="));
                Serial.print(newKp, 2);
                Serial.print(F("  Ki="));
                Serial.print(newKi, 3);
                Serial.print(F("  Kd="));
                Serial.println(newKd, 2);
                Serial.println(F("[AUTOTUNE] Values applied. Use 'save' or they persist at next EEPROM save."));

                // Apply new tunings
                setTunings(newKp, newKi, newKd);
                reset();
                _autotuning = false;
                _atOutput = 0;
                return true; // autotune complete
            }
        }
    }

    unsigned long nowMillis = millis();
    if (nowMillis - _atLastLogTime >= 5000) {
        _atLastLogTime = nowMillis;
        Serial.print(F("[AUTOTUNE] "));
        if (_atHeating) {
            Serial.print(F("HEATING -> Waiting to cross "));
            Serial.print(_atSetpoint + _atHysteresis, 1);
        } else {
            Serial.print(F("COOLING -> Waiting to cross "));
            Serial.print(_atSetpoint - _atHysteresis, 1);
        }
        Serial.print(F("C | Current: "));
        Serial.print(currentTemp, 1);
        Serial.print(F("C | Highwater: "));
        Serial.print(_atMaxTemp, 1);
        Serial.println(F("C"));
    }

    return false; // still running
}

void PIDController::autotuneCancel() {
    if (_autotuning) {
        _autotuning = false;
        _atOutput = 0;
        Serial.println(F("[AUTOTUNE] Cancelled."));
    }
}
