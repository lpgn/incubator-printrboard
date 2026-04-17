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
    analogWrite(FAN_PIN, _currentSpeed);
}

void FanController::update(float tempError, float humidError) {
    if (_manualMode) {
        _currentSpeed = (uint8_t)_manualSpeed;
        analogWrite(FAN_PIN, _currentSpeed);
        return;
    }

    // Start from base speed
    float speed = (float)FAN_BASE_SPEED;

    // --- Temperature component (PRIORITY) ---
    // tempError > 0 means too cold, < 0 means too hot
    if (tempError < -0.3f) {
        // Too hot — ramp up fan proportionally
        // At -1°C error → +100 speed, at -2°C → +200 (capped at max)
        float tempBoost = (-tempError - 0.3f) * 100.0f;
        if (tempBoost > 200.0f) tempBoost = 200.0f;
        speed += tempBoost;
    }

    // --- Humidity component (secondary) ---
    // humidError > 0 means too dry, < 0 means too wet
    // Only apply if temperature is not in a critical state
    if (tempError > -1.0f && tempError < 1.0f) {
        // Temperature is roughly OK — adjust for humidity
        if (humidError > 3.0f) {
            // Too dry — more fan = more evaporation from water tray
            float humBoost = (humidError - 3.0f) * 5.0f;
            if (humBoost > 80.0f) humBoost = 80.0f;
            speed += humBoost;
        } else if (humidError < -5.0f) {
            // Too wet — less fan = less evaporation
            float humReduce = (-humidError - 5.0f) * 5.0f;
            if (humReduce > 40.0f) humReduce = 40.0f;
            speed -= humReduce;
        }
    }

    // Clamp to range
    if (speed < (float)_minSpeed) speed = (float)_minSpeed;
    if (speed > (float)_maxSpeed) speed = (float)_maxSpeed;

    _currentSpeed = (uint8_t)speed;
    analogWrite(FAN_PIN, _currentSpeed);
}

void FanController::setManualSpeed(int16_t speed) {
    if (speed < 0) {
        _manualMode = false;
        _manualSpeed = -1;
    } else {
        _manualMode = true;
        if (speed > 255) speed = 255;
        _manualSpeed = speed;
        _currentSpeed = (uint8_t)speed;
        analogWrite(FAN_PIN, _currentSpeed);
    }
}

void FanController::setSpeedRange(uint8_t min, uint8_t max) {
    _minSpeed = min;
    _maxSpeed = max;
    if (_minSpeed > _maxSpeed) _minSpeed = _maxSpeed;
}

uint8_t FanController::getSpeedPercent() const {
    return (uint8_t)((uint16_t)_currentSpeed * 100 / 255);
}

void FanController::fullSpeed() {
    _currentSpeed = _maxSpeed;
    analogWrite(FAN_PIN, _currentSpeed);
}
