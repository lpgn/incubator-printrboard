#include "fan.h"
#include "config.h"

// =============================================================================
// Fan Controller Implementation
// Purpose: Reduce humidity ONLY by exhausting moist air out of the incubator.
//
// The fan CANNOT increase humidity (it vents air out, replacing it with
// drier ambient air). Therefore:
//   - Humidity above midpoint → fan ON at proportional PWM speed
//   - Humidity at or below midpoint → fan OFF
//   - Temperature below setpoint → throttle or stop fan to conserve heat
//
// PWM speed control minimises temperature loss compared to binary ON/OFF.
// A deadband prevents the fan from cycling on tiny humidity fluctuations.
// =============================================================================

FanController::FanController()
    : _currentSpeed(0), _minSpeed(FAN_MIN_SPEED),
      _maxSpeed(FAN_MAX_SPEED), _manualMode(false), _manualSpeed(-1) {}

void FanController::begin() {
    pinMode(FAN_PIN, OUTPUT);
    analogWrite(FAN_PIN, 0);
    // Override Timer3 prescaler: /1 instead of default /64
    // Default analogWrite runs at ~490Hz (audible MOSFET whine).
    // Prescaler=1 gives ~31kHz (well above human hearing).
    // Only affects OC3A (fan). Heater on OC3B uses software slow PWM.
    TCCR3B = (TCCR3B & 0b11111000) | 0b00000001;
}

void FanController::update(float tempError, float humidError) {
    if (_manualMode) {
        _currentSpeed = (_manualSpeed > 0) ? (uint8_t)_manualSpeed : 0;
        analogWrite(FAN_PIN, _currentSpeed);
        return;
    }

    // humidError = midpoint - currentHumidity
    //   positive → too dry (humidity below midpoint)
    //   negative → too wet (humidity above midpoint)
    // Fan can ONLY reduce humidity, so we only act when too wet.

    float humidExcess = -humidError;  // How many % above midpoint

    uint8_t speed = 0;

    if (humidExcess > FAN_HUMID_DEADBAND) {
        // --- Compute proportional fan speed ---
        // Map humidity excess from deadband..full_range to MIN_ACTIVE..MAX
        float range = FAN_HUMID_FULL_RANGE - FAN_HUMID_DEADBAND;
        float fraction = (humidExcess - FAN_HUMID_DEADBAND) / range;
        if (fraction > 1.0f) fraction = 1.0f;

        speed = FAN_MIN_ACTIVE +
                (uint8_t)(fraction * (float)(FAN_MAX_SPEED - FAN_MIN_ACTIVE));

        // --- Temperature protection ---
        // tempError > 0 means current temp is BELOW setpoint (too cold).
        // Venting air cools the incubator, so throttle the fan when cold.
        if (tempError > FAN_TEMP_PROTECT_HI) {
            // Way too cold — stop fan entirely, let heater catch up
            speed = 0;
        } else if (tempError > FAN_TEMP_PROTECT_LO) {
            // Linearly reduce fan as temp drops below setpoint
            // At LO: keep full calculated speed
            // At HI: reduce to zero
            float scale = 1.0f - (tempError - FAN_TEMP_PROTECT_LO)
                                / (FAN_TEMP_PROTECT_HI - FAN_TEMP_PROTECT_LO);
            speed = (uint8_t)((float)speed * scale);
            // Below minimum active PWM the motor stalls — just turn off
            if (speed > 0 && speed < FAN_MIN_ACTIVE) speed = 0;
        }
    }

    _currentSpeed = speed;
    analogWrite(FAN_PIN, speed);
}

void FanController::setManualSpeed(int16_t speed) {
    if (speed < 0) {
        _manualMode = false;
        _manualSpeed = -1;
    } else {
        _manualMode = true;
        _manualSpeed = (speed > 0) ? (int16_t)_maxSpeed : 0;
        _currentSpeed = (uint8_t)_manualSpeed;
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
    analogWrite(FAN_PIN, _maxSpeed);
}
