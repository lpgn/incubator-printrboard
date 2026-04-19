#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include "config.h"
#include "species.h"

// =============================================================================
// Incubator State Machine
// =============================================================================

enum IncubatorState : uint8_t {
    STATE_IDLE = 0,       // Waiting for user to select species and start
    STATE_PREHEATING,     // Warming up to setpoint
    STATE_INCUBATING,     // Active incubation with egg turning
    STATE_LOCKDOWN,       // Last days — no turning, high humidity
    STATE_HATCHING,       // Final phase — watching for hatches
    STATE_DONE,           // Incubation complete
    STATE_PAUSED,         // User-paused
    STATE_ERROR,          // Critical error (sensor failure, over-temp)
    STATE_AUTOTUNE        // PID autotune in progress
};

class StateMachine {
public:
    StateMachine();

    // Get current state
    IncubatorState getState() const { return _state; }

    // Get state name string
    const char* getStateName() const;

    // State transitions
    bool startPreheating(SpeciesID species);
    bool transitionToIncubating();
    bool transitionToLockdown();
    bool transitionToHatching();
    bool transitionToDone();
    bool pause();
    bool resume();
    bool emergencyStop();
    bool startAutotune();
    bool finishAutotune();
    bool goToError(const char* reason = nullptr);
    bool recoverFromError();
    bool reset(); // Back to IDLE

    // Configuration
    void setSpecies(SpeciesID species);
    SpeciesID getSpeciesID() const { return _speciesID; }
    SpeciesPreset getActivePreset() const { return _preset; }

    // Setpoint overrides
    void setTempOverride(float temp);
    void setHumidityOverride(uint8_t lo, uint8_t hi);
    void clearOverrides();

    // ADC target mode (for calibration precision)
    void setAdcTarget(uint16_t adc);
    void clearAdcTarget();
    bool isAdcTargetMode() const { return _adcTargetMode; }
    uint16_t getAdcTarget() const { return _adcTarget; }

    // Get effective targets for current phase
    float getTargetTemp() const;
    uint8_t getHumidityLo() const;
    uint8_t getHumidityHi() const;
    float getHumidityMidpoint() const;

    // Check day-based transitions
    // Returns true if a transition occurred
    bool checkDayTransitions(uint16_t currentDay);

    // Was there a state saved before pause/power loss?
    IncubatorState getPreviousState() const { return _prevState; }
    void setPreviousState(IncubatorState s) { _prevState = s; }

    // Force PAUSED state for EEPROM recovery (bypasses normal transition guards)
    void forcePaused(IncubatorState prevState) { _prevState = prevState; _state = STATE_PAUSED; }

    // Is turning allowed in current state?
    bool isTurningAllowed() const;

    // Is heating allowed in current state?
    bool isHeatingAllowed() const;

    // Preheat check: has temp been stable long enough?
    void updatePreheatStability(float currentTemp, float targetTemp);
    bool isPreheatStable() const { return _preheatStableMs >= PREHEAT_STABLE_MS; }

    // Preheat max PWM (user-configurable)
    uint8_t getPreheatMax() const { return _preheatMax; }
    void setPreheatMax(uint8_t pwm) { _preheatMax = pwm; }

private:
    IncubatorState _state;
    IncubatorState _prevState;  // State before pause/error
    SpeciesID _speciesID;
    SpeciesPreset _preset;

    // Overrides
    bool _tempOverride;
    float _overrideTemp;
    bool _humidOverride;
    uint8_t _overrideHumLo;
    uint8_t _overrideHumHi;

    // ADC target mode
    bool _adcTargetMode;
    uint16_t _adcTarget;

    // Preheat tracking
    uint32_t _preheatStableMs;
    unsigned long _lastPreheatCheck;
    uint8_t _preheatMax;
};

#endif // STATE_H
