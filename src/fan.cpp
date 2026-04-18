#include "fan.h"
#include "config.h"

// =============================================================================
// Fan Controller Implementation
// Dual purpose: temperature cooling + humidity regulation via evaporation
//
// Logic:
//   - Base speed ~30% for forced-air circulation
//   - Temp too high → fan UP (active cooling)
//   - Humidity too low → fan UP (more airflow over water = more evaporation)
//   - Humidity too high → fan DOWN (less evaporation)
//   - Temp too low → fan stays at base (let heater work)
//   - Temperature safety ALWAYS takes priority over humidity
// =============================================================================

FanController::FanController()
    : _currentSpeed(0), _minSpeed(FAN_MIN_SPEED),
      _maxSpeed(FAN_MAX_SPEED), _manualMode(false), _manualSpeed(-1) {}

void FanController::begin() {
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);
}

void FanController::update(float tempError, float humidError) {
    if (_manualMode) {
        _currentSpeed = (_manualSpeed > 0) ? _maxSpeed : 0;
        digitalWrite(FAN_PIN, _currentSpeed > 0 ? HIGH : LOW);
        return;
    }

    // Binary fan control: ON or OFF
    bool shouldBeOn = false;

    // --- Temperature component (PRIORITY) ---
    // tempError < 0 means too hot
    if (tempError < -0.5f) {
        shouldBeOn = true;
    }

    // --- Humidity component (secondary) ---
    // Only apply if temperature is not in a critical state
    if (tempError > -1.0f && tempError < 1.0f) {
        if (humidError > 3.0f) {
            // Too dry — more airflow over water tray
            shouldBeOn = true;
        }
    }

    _currentSpeed = shouldBeOn ? _maxSpeed : 0;
    digitalWrite(FAN_PIN, shouldBeOn ? HIGH : LOW);
}

void FanController::setManualSpeed(int16_t speed) {
    if (speed < 0) {
        _manualMode = false;
        _manualSpeed = -1;
    } else {
        _manualMode = true;
        _manualSpeed = (speed > 0) ? (int16_t)_maxSpeed : 0;
        _currentSpeed = (uint8_t)_manualSpeed;
        digitalWrite(FAN_PIN, _currentSpeed > 0 ? HIGH : LOW);
    }
}

void FanController::setSpeedRange(uint8_t min, uint8_t max) {
    _minSpeed = min;
    _maxSpeed = max;
    if (_minSpeed > _maxSpeed) _minSpeed = _maxSpeed;
}

uint8_t FanController::getSpeedPercent() const {
    return _currentSpeed > 0 ? 100 : 0;
}

void FanController::fullSpeed() {
    _currentSpeed = _maxSpeed;
    digitalWrite(FAN_PIN, HIGH);
}
