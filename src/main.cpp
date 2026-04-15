#include <Arduino.h>
#include "config.h"
#include "species.h"
#include "pid.h"
#include "heater.h"
#include "humidity.h"
#include "turner.h"
#include "fan.h"
#include "state.h"
#include "storage.h"
#include "clock.h"
#include "safety.h"
#include "terminal.h"

// =============================================================================
// Global objects
// =============================================================================
StateMachine    stateMachine;
PIDController   pid;
Heater          heater;
HumiditySensor  humiditySensor;
EggTurner       turner;
FanController   fan;
SoftClock       incubationClock;
Storage         storage;
SafetyMonitor   safety;
Terminal        terminal;

// =============================================================================
// Timing variables
// =============================================================================
unsigned long lastPIDUpdate = 0;
unsigned long lastDHTRead = 0;
unsigned long lastEEPROMSave = 0;
uint16_t lastDay = 0;

// Current sensor readings (global for sharing between modules)
float currentTemp = 0.0f;
float currentHumidity = 50.0f;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    // Initialize all subsystems
    terminal.begin();

    // Small delay for USB to enumerate
    delay(2000);

    terminal.printBanner();

    heater.begin();
    humiditySensor.begin();
    turner.begin();
    fan.begin();
    safety.begin();

    pid.begin(PID_DEFAULT_KP, PID_DEFAULT_KI, PID_DEFAULT_KD);
    pid.setOutputLimits(PID_OUTPUT_MIN, PID_OUTPUT_MAX);

    // Wire up terminal references
    terminal.setReferences(&stateMachine, &pid, &heater, &humiditySensor,
                           &turner, &fan, &incubationClock, &storage, &safety);

    // --- Check for power recovery ---
    SavedState saved;
    if (storage.load(saved)) {
        // Valid state found in EEPROM
        storage.logEvent(EVENT_POWER_RECOVER, saved.currentDay);

        Serial.println(F("========================================"));
        Serial.println(F("  POWER RECOVERY DETECTED"));
        Serial.println(F("========================================"));
        Serial.print(F("  Species: "));
        Serial.println(getSpeciesName((SpeciesID)saved.speciesID));
        Serial.print(F("  State: "));

        // Map saved state to name
        StateMachine tempSM;
        switch ((IncubatorState)saved.state) {
            case STATE_INCUBATING: Serial.println(F("INCUBATING")); break;
            case STATE_LOCKDOWN:   Serial.println(F("LOCKDOWN")); break;
            case STATE_HATCHING:   Serial.println(F("HATCHING")); break;
            default:               Serial.println(F("PAUSED")); break;
        }

        Serial.print(F("  Day: "));
        Serial.print(saved.currentDay);
        Serial.print(F(" of "));
        Serial.println(getSpeciesPreset((SpeciesID)saved.speciesID).totalDays);

        uint32_t elapsedHours = saved.elapsedSeconds / 3600;
        Serial.print(F("  Elapsed: "));
        Serial.print(elapsedHours);
        Serial.println(F(" hours"));

        Serial.println(F(""));
        Serial.println(F("  Type 'resume' to continue incubation."));
        Serial.println(F("  Type 'reset' to start fresh (or select a new species)."));
        Serial.println(F("========================================"));
        Serial.println();

        // Restore PID tuning
        pid.setTunings(saved.pidKp, saved.pidKi, saved.pidKd);
        pid.setSetpoint((float)saved.targetTemp / 10.0f);

        // Set species
        stateMachine.setSpecies((SpeciesID)saved.speciesID);

        // Prepare for resume — go to PAUSED state with previous state saved
        stateMachine.setPreviousState((IncubatorState)saved.state);

        // Pre-load the clock with saved elapsed time
        incubationClock.resumeFrom(saved.elapsedSeconds);
        incubationClock.pause(); // Don't start counting yet until user says 'resume'

        // Restore turner state
        turner.setTurnsPerDay(stateMachine.getActivePreset().turnsPerDay);

        // Go to paused state waiting for user to resume
        // We manually set the state since normal transitions require specific source states
        // The terminal 'resume' command will handle the actual resume
        // For now start heating to not lose temperature
        IncubatorState resumeState = (IncubatorState)saved.state;
        if (resumeState == STATE_INCUBATING || resumeState == STATE_LOCKDOWN || resumeState == STATE_HATCHING) {
            // Start heating immediately — don't wait for resume
            // Temperature is critical during recovery
            fan.setManualSpeed(-1); // Auto mode
        }

    } else {
        // No saved state — fresh start
        storage.logEvent(EVENT_BOOT, 0);
        Serial.println(F("No saved state found. Ready for new incubation."));
        Serial.println(F("Type 'species' to see options, then 'select <name|#>' and 'start'."));
        Serial.println();
    }

    terminal.printPrompt();

    // Initialize timers
    lastPIDUpdate = millis();
    lastDHTRead = millis();
    lastEEPROMSave = millis();
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    unsigned long now = millis();
    IncubatorState state = stateMachine.getState();

    // --- Update software clock ---
    incubationClock.update();

    // --- Poll terminal for commands ---
    terminal.poll();

    // --- Read DHT22 sensor (every 5 seconds) ---
    if (now - lastDHTRead >= DHT_READ_INTERVAL_MS) {
        lastDHTRead = now;
        if (humiditySensor.isPresent()) {
            if (humiditySensor.read()) {
                currentHumidity = humiditySensor.getHumidity();
            }
            // If read failed, keep last known value
        }
    }

    // --- PID Temperature Control (every 1 second) ---
    if (now - lastPIDUpdate >= PID_SAMPLE_MS) {
        lastPIDUpdate = now;

        currentTemp = heater.readTemperature();

        if (stateMachine.isHeatingAllowed()) {
            if (heater.isSensorFailed()) {
                // Sensor failure — shut down heater for safety
                heater.shutdown();
            } else if (pid.isAutotuning()) {
                // Autotune mode — use relay output
                bool done = pid.autotuneUpdate(currentTemp);
                heater.setOutput((uint8_t)pid.getAutotuneOutput());

                if (done) {
                    stateMachine.finishAutotune();
                    storage.logEvent(EVENT_AUTOTUNE_DONE, 0);
                    // Save new PID values
                    storage.save(
                        (uint8_t)stateMachine.getSpeciesID(),
                        (uint8_t)stateMachine.getState(),
                        incubationClock.getElapsedSeconds(),
                        incubationClock.getCurrentDay(),
                        turner.getTurnsCompleted(),
                        pid.getKp(), pid.getKi(), pid.getKd(),
                        (uint16_t)(stateMachine.getTargetTemp() * 10.0f),
                        (uint16_t)(stateMachine.getHumidityMidpoint() * 10.0f)
                    );
                }
            } else if (currentTemp > safety.getMaxTemp()) {
                // Over-temp — emergency shutdown
                heater.shutdown();
                fan.fullSpeed();
            } else {
                // Normal PID control
                int16_t output = pid.compute(currentTemp);
                heater.setOutput((uint8_t)output);
            }

            // Preheat stability check
            if (state == STATE_PREHEATING) {
                stateMachine.updatePreheatStability(currentTemp, stateMachine.getTargetTemp());
                if (stateMachine.isPreheatStable()) {
                    stateMachine.transitionToIncubating();
                    turner.setEnabled(true);
                    Serial.println(F(">> Temperature stable! INCUBATION STARTED."));
                    storage.logEvent(EVENT_START, incubationClock.getCurrentDay());
                }
            }
        } else {
            // Not in a heating state — ensure heater is off
            heater.setOutput(0);
        }

        // --- Fan control (based on temp and humidity) ---
        if (state != STATE_IDLE && state != STATE_DONE) {
            float tempError = stateMachine.getTargetTemp() - currentTemp;
            float humidMid = stateMachine.getHumidityMidpoint();
            float humidError = humidMid - currentHumidity;
            fan.update(tempError, humidError);
        }
    }

    // --- Egg Turner update ---
    if (stateMachine.isTurningAllowed()) {
        turner.update(incubationClock.getDaySeconds());
    }

    // --- Day change detection ---
    if (state == STATE_INCUBATING || state == STATE_LOCKDOWN || state == STATE_HATCHING) {
        uint16_t currentDay = incubationClock.getCurrentDay();
        if (currentDay != lastDay) {
            lastDay = currentDay;
            turner.resetDayCount();

            Serial.print(F(">> Day "));
            Serial.print(currentDay);
            Serial.print(F(" of "));
            Serial.println(stateMachine.getActivePreset().totalDays);

            // Check for state transitions
            if (stateMachine.checkDayTransitions(currentDay)) {
                // State changed — update turner
                if (!stateMachine.isTurningAllowed()) {
                    turner.setEnabled(false);
                    Serial.println(F(">> Egg turning STOPPED (lockdown)."));
                }

                // Log the transition
                if (stateMachine.getState() == STATE_LOCKDOWN) {
                    storage.logEvent(EVENT_LOCKDOWN, currentDay);
                } else if (stateMachine.getState() == STATE_HATCHING) {
                    storage.logEvent(EVENT_HATCHING, currentDay);
                } else if (stateMachine.getState() == STATE_DONE) {
                    storage.logEvent(EVENT_DONE, currentDay);
                    heater.setOutput(0);
                    turner.setEnabled(false);
                    fan.setManualSpeed(0);
                    Serial.println(F(""));
                    Serial.println(F("==================================="));
                    Serial.println(F("  INCUBATION COMPLETE!"));
                    Serial.println(F("  Check your eggs/chicks."));
                    Serial.println(F("  Type 'reset' when done."));
                    Serial.println(F("==================================="));
                }
            }
        }
    }

    // --- Safety checks ---
    if (state != STATE_IDLE && state != STATE_DONE) {
        safety.check(currentTemp, currentHumidity,
                     heater.isSensorFailed(), humiditySensor.isFailed());

        if (!safety.isOverridden() && (safety.isOverTemp() || safety.isSensorFailed())) {
            heater.shutdown();
            fan.fullSpeed();
            if (state != STATE_ERROR) {
                stateMachine.goToError();
                if (safety.isOverTemp()) {
                    storage.logEvent(EVENT_OVERTEMP, (uint16_t)(currentTemp * 10));
                }
                if (safety.isSensorFailed()) {
                    storage.logEvent(EVENT_SENSOR_FAIL, 0);
                }
            }
        }

        if (safety.isHumidityLow()) {
            static unsigned long lastHumidWarn = 0;
            if (now - lastHumidWarn > 300000UL) { // Every 5 minutes
                storage.logEvent(EVENT_HUMID_LOW, (uint16_t)(currentHumidity * 10));
                lastHumidWarn = now;
            }
        }
    }

    // --- LED heartbeat ---
    safety.updateLED();

    // --- Auto-report status ---
    if (terminal.shouldAutoReport() && state != STATE_IDLE) {
        terminal.printStatus();
    }

    // --- Periodic EEPROM save ---
    if (now - lastEEPROMSave >= EEPROM_SAVE_INTERVAL_MS) {
        lastEEPROMSave = now;

        if (state == STATE_INCUBATING || state == STATE_LOCKDOWN ||
            state == STATE_HATCHING || state == STATE_PAUSED) {

            storage.save(
                (uint8_t)stateMachine.getSpeciesID(),
                (uint8_t)(state == STATE_PAUSED ? stateMachine.getPreviousState() : state),
                incubationClock.getElapsedSeconds(),
                incubationClock.getCurrentDay(),
                turner.getTurnsCompleted(),
                pid.getKp(), pid.getKi(), pid.getKd(),
                (uint16_t)(stateMachine.getTargetTemp() * 10.0f),
                (uint16_t)(stateMachine.getHumidityMidpoint() * 10.0f)
            );
        }
    }
}
