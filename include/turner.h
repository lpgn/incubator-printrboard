#ifndef TURNER_H
#define TURNER_H

#include <Arduino.h>

// =============================================================================
// Egg Turner — Stepper motor control via A4982 driver
// Direct-drive, no gearing needed (1/16 microstepping gives fine control)
// =============================================================================

class EggTurner {
public:
    EggTurner();

    // Initialize stepper pins
    void begin();

    // Configure turning parameters
    void setDegreesPerTurn(uint16_t degrees);
    void setTurnsPerDay(uint8_t turns);

    // Update — call from main loop. Handles scheduling and motion.
    // elapsedDaySeconds: seconds elapsed in the current day (0-86399)
    void update(uint32_t elapsedDaySeconds);

    // Force an immediate turn (for testing)
    void turnNow();

    // Enable/disable turning (disabled during lockdown)
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    // Is a turn currently in progress?
    bool isTurning() const { return _stepping; }
    bool isStepping() const { return _stepping; }

    // Get status
    uint8_t getTurnsCompleted() const { return _turnsCompleted; }
    uint8_t getTurnsPerDay() const { return _turnsPerDay; }

    // Reset turn counter (called at start of new day)
    void resetDayCount();

    // Get time until next scheduled turn (seconds)
    uint32_t getSecondsUntilNextTurn(uint32_t elapsedDaySeconds) const;

private:
    bool _enabled;
    bool _stepping;
    bool _direction;        // Alternates each turn
    uint16_t _degreesPerTurn;
    uint8_t _turnsPerDay;
    uint8_t _turnsCompleted;
    uint32_t _stepsRemaining;
    uint32_t _stepsTotal;
    uint16_t _stepDelayUs;  // Microseconds between steps (controls speed)
    unsigned long _lastStepTime;
    uint32_t _accelStep;    // Current step in acceleration phase

    // Calculate steps needed for the configured degrees
    uint32_t degreesToSteps(uint16_t degrees);

    // Calculate interval between turns (seconds)
    uint32_t getTurnInterval() const;

    // Execute one step pulse
    void doStep();
};

#endif // TURNER_H
