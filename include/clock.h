#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>

// =============================================================================
// Software Clock — Elapsed time tracking with EEPROM persistence
// No RTC hardware needed. Tracks total incubation elapsed seconds.
// =============================================================================

class SoftClock {
public:
    SoftClock();

    // Start counting from zero
    void start();

    // Resume from a saved elapsed time (after power recovery)
    void resumeFrom(uint32_t elapsedSeconds);

    // Pause the clock
    void pause();

    // Resume after pause
    void resume();

    // Update — call from main loop
    void update();

    // Get total elapsed seconds since incubation start
    uint32_t getElapsedSeconds() const;

    // Get current incubation day (1-based)
    uint16_t getCurrentDay() const;

    // Get seconds elapsed within the current day (0-86399)
    uint32_t getDaySeconds() const;

    // Get formatted time string "DDd HHh MMm"
    void getFormattedTime(char* buf, uint8_t bufSize) const;

    // Is clock running?
    bool isRunning() const { return _running; }

    // Stop the clock entirely
    void stop();

private:
    bool _running;
    uint32_t _elapsedSeconds;       // Total seconds of incubation
    unsigned long _lastMillis;      // Last millis() reading
    uint32_t _millisAccum;          // Accumulated milliseconds (sub-second)
};

#endif // CLOCK_H
