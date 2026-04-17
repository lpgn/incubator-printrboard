#include "turner.h"
#include "config.h"

// =============================================================================
// Egg Turner Implementation
// =============================================================================

EggTurner::EggTurner()
    : _enabled(false), _stepping(false), _direction(false),
      _degreesPerTurn(TURNER_DEFAULT_DEGREES), _turnsPerDay(5),
      _turnsCompleted(0), _stepsRemaining(0), _stepsTotal(0),
      _stepDelayUs(500), _lastStepTime(0), _accelStep(0), _testTurn(false) {}

void EggTurner::begin() {
    pinMode(TURNER_STEP_PIN, OUTPUT);
    pinMode(TURNER_DIR_PIN, OUTPUT);
    pinMode(TURNER_ENABLE_PIN, OUTPUT);

    // Disable stepper (active LOW on A4982)
    digitalWrite(TURNER_ENABLE_PIN, HIGH);
    digitalWrite(TURNER_STEP_PIN, LOW);
    digitalWrite(TURNER_DIR_PIN, LOW);

    // Calculate base step delay from RPM
    // steps_per_second = (RPM * STEPS_PER_REV) / 60
    // delay_us = 1000000 / steps_per_second
    float stepsPerSec = (float)TURNER_DEFAULT_RPM * (float)TURNER_STEPS_PER_REV / 60.0f;
    _stepDelayUs = (uint16_t)(1000000.0f / stepsPerSec);
}

void EggTurner::setDegreesPerTurn(uint16_t degrees) {
    _degreesPerTurn = degrees;
    if (_degreesPerTurn > 360) _degreesPerTurn = 360;
}

void EggTurner::setTurnsPerDay(uint8_t turns) {
    _turnsPerDay = turns;
    if (_turnsPerDay < 1) _turnsPerDay = 1;
    if (_turnsPerDay > 24) _turnsPerDay = 24;
}

void EggTurner::setRPM(float rpm) {
    if (rpm < 0.5f) rpm = 0.5f;
    if (rpm > 10.0f) rpm = 10.0f;
    float stepsPerSec = rpm * (float)TURNER_STEPS_PER_REV / 60.0f;
    _stepDelayUs = (uint16_t)(1000000.0f / stepsPerSec);
}

void EggTurner::update(uint32_t elapsedDaySeconds) {
    // If currently executing a turn, continue stepping even if not enabled
    if (_stepping) {
        unsigned long now = micros();
        if (now - _lastStepTime >= _stepDelayUs) {
            doStep();
            _lastStepTime = now;
            _stepsRemaining--;
            _accelStep++;

            if (_stepsRemaining == 0) {
                // Turn complete
                _stepping = false;
                _direction = !_direction; // Alternate direction for next turn

                if (!_testTurn) {
                    _turnsCompleted++;
                }
                _testTurn = false;

                // Disable stepper to save power and reduce heat
                digitalWrite(TURNER_ENABLE_PIN, HIGH);

                Serial.print(F("[TURNER] Turn "));
                Serial.print(_turnsCompleted);
                Serial.print(F("/"));
                Serial.print(_turnsPerDay);
                Serial.println(F(" complete."));
            }
        }
        return;
    }

    if (!_enabled) return;

    // Check if it's time for the next scheduled turn
    if (_turnsCompleted >= _turnsPerDay) return; // All turns done for today

    uint32_t interval = getTurnInterval();
    uint32_t nextTurnTime = (uint32_t)(_turnsCompleted + 1) * interval;

    if (elapsedDaySeconds >= nextTurnTime) {
        turnNow();
    }
}

void EggTurner::turnNow(bool countAsTurn) {
    if (_stepping) return; // Already turning

    _stepsTotal = degreesToSteps(_degreesPerTurn);
    _stepsRemaining = _stepsTotal;
    _accelStep = 0;
    _testTurn = !countAsTurn;

    // Set direction
    digitalWrite(TURNER_DIR_PIN, _direction ? HIGH : LOW);

    // Enable stepper driver (active LOW)
    digitalWrite(TURNER_ENABLE_PIN, LOW);

    // Small delay for driver to wake up
    delayMicroseconds(100);

    _stepping = true;
    _lastStepTime = micros();

    Serial.print(F("[TURNER] Starting turn "));
    Serial.print(_turnsCompleted + 1);
    Serial.print(F("/"));
    Serial.print(_turnsPerDay);
    Serial.print(F(" - "));
    Serial.print(_degreesPerTurn);
    Serial.print(F("deg "));
    Serial.println(_direction ? F("CW") : F("CCW"));
}

void EggTurner::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) {
        _stepping = false;
        _stepsRemaining = 0;
        digitalWrite(TURNER_ENABLE_PIN, HIGH); // Disable driver
    }
}

void EggTurner::resetDayCount() {
    _turnsCompleted = 0;
}

void EggTurner::setTurnsCompleted(uint8_t turns) {
    _turnsCompleted = turns;
}

uint32_t EggTurner::getSecondsUntilNextTurn(uint32_t elapsedDaySeconds) const {
    if (!_enabled || _turnsCompleted >= _turnsPerDay) return 0;
    uint32_t interval = getTurnInterval();
    uint32_t nextTurnTime = (uint32_t)(_turnsCompleted + 1) * interval;
    if (elapsedDaySeconds >= nextTurnTime) return 0;
    return nextTurnTime - elapsedDaySeconds;
}

uint32_t EggTurner::degreesToSteps(uint16_t degrees) {
    // steps = (degrees / 360) * steps_per_rev
    return (uint32_t)degrees * TURNER_STEPS_PER_REV / 360UL;
}

uint32_t EggTurner::getTurnInterval() const {
    // Spread turns evenly over 24 hours
    // But leave the first and last hour free
    // Effective window: 22 hours = 79200 seconds
    if (_turnsPerDay == 0) return SECONDS_PER_DAY;
    return 79200UL / (uint32_t)_turnsPerDay;
}

void EggTurner::doStep() {
    // Simple trapezoidal acceleration profile
    // Accelerate for TURNER_ACCEL_STEPS, cruise, decelerate for TURNER_ACCEL_STEPS
    uint32_t stepsFromEnd = _stepsRemaining;
    uint32_t phase = min(_accelStep, stepsFromEnd);

    if (phase < TURNER_ACCEL_STEPS) {
        // Acceleration/deceleration phase — slower steps
        uint16_t accelDelay = _stepDelayUs + (uint16_t)((_stepDelayUs * (TURNER_ACCEL_STEPS - phase)) / TURNER_ACCEL_STEPS);
        // Apply the longer delay
        if (accelDelay > _stepDelayUs) {
            delayMicroseconds(accelDelay - _stepDelayUs);
        }
    }

    // Pulse STEP pin (A4982 triggers on rising edge)
    digitalWrite(TURNER_STEP_PIN, HIGH);
    delayMicroseconds(2);
    digitalWrite(TURNER_STEP_PIN, LOW);
}
