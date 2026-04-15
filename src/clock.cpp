#include "clock.h"
#include "config.h"

SoftClock::SoftClock()
    : _running(false), _elapsedSeconds(0), _lastMillis(0), _millisAccum(0) {}

void SoftClock::start() {
    _elapsedSeconds = 0;
    _millisAccum = 0;
    _lastMillis = millis();
    _running = true;
}

void SoftClock::resumeFrom(uint32_t elapsedSeconds) {
    _elapsedSeconds = elapsedSeconds;
    _millisAccum = 0;
    _lastMillis = millis();
    _running = true;
}

void SoftClock::pause() {
    if (_running) {
        update(); // Capture remaining time
        _running = false;
    }
}

void SoftClock::resume() {
    _lastMillis = millis();
    _running = true;
}

void SoftClock::update() {
    if (!_running) return;

    unsigned long now = millis();
    unsigned long delta = now - _lastMillis;
    _lastMillis = now;

    _millisAccum += (uint32_t)delta;

    // Convert accumulated milliseconds to seconds
    while (_millisAccum >= MS_PER_SECOND) {
        _millisAccum -= MS_PER_SECOND;
        _elapsedSeconds++;
    }
}

uint32_t SoftClock::getElapsedSeconds() const {
    return _elapsedSeconds;
}

uint16_t SoftClock::getCurrentDay() const {
    return (uint16_t)(_elapsedSeconds / SECONDS_PER_DAY) + 1;
}

uint32_t SoftClock::getDaySeconds() const {
    return _elapsedSeconds % SECONDS_PER_DAY;
}

void SoftClock::getFormattedTime(char* buf, uint8_t bufSize) const {
    uint32_t total = _elapsedSeconds;
    uint16_t days = total / SECONDS_PER_DAY;
    total %= SECONDS_PER_DAY;
    uint8_t hours = total / 3600;
    total %= 3600;
    uint8_t minutes = total / 60;

    snprintf(buf, bufSize, "%ud %uh %um", days, hours, minutes);
}

void SoftClock::stop() {
    update(); // Capture final time
    _running = false;
}
