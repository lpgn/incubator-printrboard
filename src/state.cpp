#include "state.h"

StateMachine::StateMachine()
    : _state(STATE_IDLE), _prevState(STATE_IDLE),
      _speciesID(SPECIES_CHICKEN), _tempOverride(false),
      _overrideTemp(37.5f), _humidOverride(false),
      _overrideHumLo(45), _overrideHumHi(55),
      _preheatStableMs(0), _lastPreheatCheck(0) {
    _preset = getSpeciesPreset(SPECIES_CHICKEN);
}

const char* StateMachine::getStateName() const {
    switch (_state) {
        case STATE_IDLE:        return "IDLE";
        case STATE_PREHEATING:  return "PREHEATING";
        case STATE_INCUBATING:  return "INCUBATING";
        case STATE_LOCKDOWN:    return "LOCKDOWN";
        case STATE_HATCHING:    return "HATCHING";
        case STATE_DONE:        return "DONE";
        case STATE_PAUSED:      return "PAUSED";
        case STATE_ERROR:       return "ERROR";
        case STATE_AUTOTUNE:    return "AUTOTUNE";
        default:                return "UNKNOWN";
    }
}

bool StateMachine::startPreheating(SpeciesID species) {
    if (_state != STATE_IDLE) return false;
    setSpecies(species);
    _state = STATE_PREHEATING;
    _preheatStableMs = 0;
    _lastPreheatCheck = millis();
    return true;
}

bool StateMachine::transitionToIncubating() {
    if (_state != STATE_PREHEATING) return false;
    _state = STATE_INCUBATING;
    Serial.println(F("[STATE] -> INCUBATING (eggs turning active)"));
    return true;
}

bool StateMachine::transitionToLockdown() {
    if (_state != STATE_INCUBATING) return false;
    _state = STATE_LOCKDOWN;
    Serial.println(F("[STATE] -> LOCKDOWN (turning stopped, humidity increased)"));
    return true;
}

bool StateMachine::transitionToHatching() {
    if (_state != STATE_LOCKDOWN) return false;
    _state = STATE_HATCHING;
    Serial.println(F("[STATE] -> HATCHING (watch for chicks!)"));
    return true;
}

bool StateMachine::transitionToDone() {
    if (_state != STATE_HATCHING) return false;
    _state = STATE_DONE;
    Serial.println(F("[STATE] -> DONE (incubation complete)"));
    return true;
}

bool StateMachine::pause() {
    if (_state == STATE_INCUBATING || _state == STATE_LOCKDOWN || _state == STATE_HATCHING) {
        _prevState = _state;
        _state = STATE_PAUSED;
        Serial.println(F("[STATE] -> PAUSED"));
        return true;
    }
    return false;
}

bool StateMachine::resume() {
    if (_state != STATE_PAUSED) return false;
    _state = _prevState;
    Serial.print(F("[STATE] -> RESUMED to "));
    Serial.println(getStateName());
    return true;
}

bool StateMachine::emergencyStop() {
    _prevState = _state;
    _state = STATE_IDLE;
    Serial.println(F("[STATE] !!! EMERGENCY STOP !!!"));
    return true;
}

bool StateMachine::startAutotune() {
    if (_state != STATE_IDLE) return false;
    _state = STATE_AUTOTUNE;
    return true;
}

bool StateMachine::finishAutotune() {
    if (_state != STATE_AUTOTUNE) return false;
    _state = STATE_IDLE;
    return true;
}

bool StateMachine::goToError(const char* reason) {
    _prevState = _state;
    _state = STATE_ERROR;
    Serial.print(F("[STATE] !!! ERROR STATE"));
    if (reason) {
        Serial.print(F(": "));
        Serial.print(reason);
    }
    Serial.println(F(" !!!"));
    return true;
}

bool StateMachine::recoverFromError() {
    if (_state != STATE_ERROR) return false;
    _state = _prevState;
    Serial.print(F(">> RECOVERED from error. Resuming "));
    Serial.println(getStateName());
    return true;
}

bool StateMachine::reset() {
    _state = STATE_IDLE;
    _prevState = STATE_IDLE;
    _preheatStableMs = 0;
    clearOverrides();
    Serial.println(F("[STATE] -> IDLE (reset)"));
    return true;
}

void StateMachine::setSpecies(SpeciesID species) {
    _speciesID = species;
    _preset = getSpeciesPreset(species);
}

void StateMachine::setTempOverride(float temp) {
    _tempOverride = true;
    _overrideTemp = temp;
}

void StateMachine::setHumidityOverride(uint8_t lo, uint8_t hi) {
    _humidOverride = true;
    _overrideHumLo = lo;
    _overrideHumHi = hi;
}

void StateMachine::clearOverrides() {
    _tempOverride = false;
    _humidOverride = false;
}

float StateMachine::getTargetTemp() const {
    if (_tempOverride) return _overrideTemp;
    return (float)_preset.tempSetpoint / 10.0f;
}

uint8_t StateMachine::getHumidityLo() const {
    if (_humidOverride) return _overrideHumLo;
    if (_state == STATE_LOCKDOWN || _state == STATE_HATCHING) {
        return _preset.humidityLockdownLo;
    }
    return _preset.humiditySetterLo;
}

uint8_t StateMachine::getHumidityHi() const {
    if (_humidOverride) return _overrideHumHi;
    if (_state == STATE_LOCKDOWN || _state == STATE_HATCHING) {
        return _preset.humidityLockdownHi;
    }
    return _preset.humiditySetterHi;
}

float StateMachine::getHumidityMidpoint() const {
    return ((float)getHumidityLo() + (float)getHumidityHi()) / 2.0f;
}

bool StateMachine::checkDayTransitions(uint16_t currentDay) {
    if (_state == STATE_INCUBATING && currentDay >= _preset.turningStopDay) {
        return transitionToLockdown();
    }
    if (_state == STATE_LOCKDOWN && currentDay >= _preset.totalDays - 1) {
        return transitionToHatching();
    }
    if (_state == STATE_HATCHING && currentDay >= _preset.totalDays + 2) {
        return transitionToDone();
    }
    return false;
}

bool StateMachine::isTurningAllowed() const {
    return _state == STATE_INCUBATING;
}

bool StateMachine::isHeatingAllowed() const {
    return _state == STATE_PREHEATING || _state == STATE_INCUBATING ||
           _state == STATE_LOCKDOWN || _state == STATE_HATCHING ||
           _state == STATE_PAUSED || _state == STATE_AUTOTUNE;
}

void StateMachine::updatePreheatStability(float currentTemp, float targetTemp) {
    if (_state != STATE_PREHEATING) return;

    unsigned long now = millis();
    unsigned long delta = now - _lastPreheatCheck;
    _lastPreheatCheck = now;

    float error = currentTemp - targetTemp;
    if (error < -0.5f || error > 0.5f) {
        // Not stable — reset counter
        _preheatStableMs = 0;
    } else {
        _preheatStableMs += delta;
    }
}
