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
#include "rtc.h"
#include "sdlogger.h"

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
DS3231          rtc;
SDLogger        sdLogger;

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
    rtc.begin();

    pid.begin(PID_DEFAULT_KP, PID_DEFAULT_KI, PID_DEFAULT_KD);
    pid.setOutputLimits(PID_OUTPUT_MIN, PID_OUTPUT_MAX);

    // Initialize SD card (optional — continues even if no card inserted)
    if (sdLogger.begin(SD_CS_PIN)) {
        Serial.println(F("[SD] Card initialized. Logging enabled."));
    } else {
        Serial.println(F("[SD] Card not found or init failed. Logging disabled."));
    }

    // Wire up terminal references
    terminal.setReferences(&stateMachine, &pid, &heater, &humiditySensor,
                           &turner, &fan, &incubationClock, &storage, &safety, &rtc, &sdLogger);

    // Load temperature calibration from EEPROM
    float calOffset, calR25, calBeta;
    storage.loadCalibration(calOffset, calR25, calBeta);
    // Guard against NaN from uninitialized EEPROM
    if (calOffset != calOffset) calOffset = 0.0f;
    if (calR25 != calR25) calR25 = 0.0f;
    if (calBeta != calBeta) calBeta = 0.0f;
    if (calOffset != 0.0f || calR25 > 0.0f || calBeta > 0.0f) {
        heater.setTempOffset(calOffset);
        if (calR25 > 0.0f && calBeta > 0.0f) {
            heater.setCustomThermistor(calR25, calBeta);
        }
        Serial.print(F("[CAL] Loaded: offset="));
        Serial.print(calOffset, 1);
        Serial.print(F("C R25="));
        Serial.print(calR25, 1);
        Serial.print(F(" beta="));
        Serial.println(calBeta, 1);
    }

    // Load preheat max PWM from EEPROM
    uint8_t savedPreheat = storage.loadPreheatMax();
    if (savedPreheat > 0) {
        stateMachine.setPreheatMax(savedPreheat);
        Serial.print(F("[CAL] Preheat max PWM: "));
        Serial.println(savedPreheat);
    }

    // --- DS3231 RTC detection ---
    if (rtc.isPresent()) {
        Serial.println(F("DS3231 RTC detected."));
        char timeBuf[20];
        rtc.getFormattedDateTime(timeBuf, sizeof(timeBuf));
        Serial.print(F("  RTC time: "));
        Serial.println(timeBuf);
    } else {
        Serial.println(F("No RTC detected — using software clock only."));
    }

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
        stateMachine.forcePaused((IncubatorState)saved.state);

        // Pre-load the clock with saved elapsed time
        uint32_t resumedElapsed = saved.elapsedSeconds;

        // RTC catch-up: account for blackout time
        if (rtc.isPresent() && saved.epoch > 0) {
            uint32_t currentEpoch = rtc.getEpoch2000();
            if (currentEpoch > saved.epoch) {
                uint32_t blackoutSeconds = currentEpoch - saved.epoch;
                // Sanity check: don't add more than 30 days
                if (blackoutSeconds < SECONDS_PER_DAY * 30UL) {
                    resumedElapsed += blackoutSeconds;
                    Serial.print(F("  Blackout recovered: "));
                    Serial.print(blackoutSeconds / 3600);
                    Serial.println(F(" hours"));
                }
            }
        }

        incubationClock.resumeFrom(resumedElapsed);
        incubationClock.pause(); // Don't start counting yet until user says 'resume'

        // Restore turner state
        turner.setTurnsPerDay(stateMachine.getActivePreset().turnsPerDay);
        turner.setTurnsCompleted(saved.turnsToday);

        // Go to paused state waiting for user to resume
        // We manually set the state since normal transitions require specific source states
        // The terminal 'resume' command will handle the actual resume
        // For now start heating to not lose temperature
        IncubatorState resumeState = (IncubatorState)saved.state;
        if (resumeState == STATE_INCUBATING || resumeState == STATE_LOCKDOWN || resumeState == STATE_HATCHING) {
            // Start heating immediately — don't wait for resume
            // Temperature is critical during recovery
            heater.clearShutdown();
            heater.setManualSpeed(-1);
            pid.reset();
            pid.setSetpoint(stateMachine.getTargetTemp());
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
        uint16_t currentADC = heater.readRawADC();

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
                        (uint16_t)(stateMachine.getHumidityMidpoint() * 10.0f),
                        rtc.isPresent() ? rtc.getEpoch2000() : 0
                    );
                }
            } else if (currentTemp > safety.getMaxTemp() && !stateMachine.isAdcTargetMode()) {
                // Over-temp — emergency shutdown (only when NOT in ADC target mode)
                heater.shutdown();
                fan.fullSpeed();
            } else {
                // Normal PID control
                int16_t output;
                if (stateMachine.isAdcTargetMode()) {
                    pid.setSetpoint((float)stateMachine.getAdcTarget());
                    output = pid.compute((float)currentADC);
                } else {
                    pid.setSetpoint(stateMachine.getTargetTemp());
                    output = pid.compute(currentTemp);
                }
                // Clamp output during large temperature deviations to prevent scorching
                // (e.g., when door is opened and cold air causes PID to spike)
                float tempError = stateMachine.getTargetTemp() - currentTemp;
                if (tempError < 0.0f) tempError = -tempError;
                if (tempError > 2.0f && output > stateMachine.getPreheatMax()) {
                    output = stateMachine.getPreheatMax();
                }
                heater.setOutput((uint8_t)output);
            }

            // Preheat stability check (only in normal temp mode)
            if (state == STATE_PREHEATING && !stateMachine.isAdcTargetMode()) {
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

        // --- Heater slow PWM update (call every loop for accurate timing) ---
        heater.update();

        // --- Fan control (proportional PWM for humidity reduction) ---
        // Fan runs in ALL active states including PREHEATING.
        // Temperature protection inside fan.update() throttles speed
        // when temp is below setpoint, preventing excessive heat loss.
        if (state != STATE_IDLE && state != STATE_DONE) {
            float tempError = stateMachine.getTargetTemp() - currentTemp;
            float humidMid = stateMachine.getHumidityMidpoint();
            float humidError = humidMid - currentHumidity;
            fan.update(tempError, humidError);
        } else {
            if (!fan.isManual()) {
                fan.setManualSpeed(0); // Keep fan off while inactive
            }
        }
    }

    // --- Egg Turner update ---
    if (stateMachine.isTurningAllowed() || turner.isStepping()) {
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
                if (safety.isOverTemp()) {
                    stateMachine.goToError("OVER-TEMP");
                    storage.logEvent(EVENT_OVERTEMP, (uint16_t)(currentTemp * 10));
                } else if (safety.isSensorFailed()) {
                    stateMachine.goToError("SENSOR FAIL");
                    storage.logEvent(EVENT_SENSOR_FAIL, 0);
                }
            }
        }

        // Auto-recover from ERROR when alarms clear
        if (state == STATE_ERROR && !safety.isAnyAlarm()) {
            stateMachine.recoverFromError();
            storage.logEvent(EVENT_RESUME, incubationClock.getCurrentDay());
            if (stateMachine.isHeatingAllowed()) {
                heater.clearShutdown();
                fan.setManualSpeed(-1); // Return to auto
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
    if (terminal.shouldAutoReport()) {
        terminal.printStatus();

        // Also log to SD card if available
        if (sdLogger.isReady()) {
            sdLogger.writeLog(
                now,
                currentTemp,
                currentHumidity,
                (uint8_t)(heater.getOutput() * 100 / 255),
                fan.getSpeedPercent(),
                stateMachine.getStateName(),
                incubationClock.getCurrentDay(),
                turner.getTurnsCompleted()
            );
        }
    }

    // --- Periodic EEPROM save ---
    if (now - lastEEPROMSave >= EEPROM_SAVE_INTERVAL_MS) {
        lastEEPROMSave = now;

        // Never save AUTOTUNE state — prevents half-tuned recovery on brownout
        if (state == STATE_AUTOTUNE) {
            // Skip save during autotune
        } else if (state == STATE_INCUBATING || state == STATE_LOCKDOWN ||
                   state == STATE_HATCHING || state == STATE_PAUSED) {

            storage.save(
                (uint8_t)stateMachine.getSpeciesID(),
                (uint8_t)(state == STATE_PAUSED ? stateMachine.getPreviousState() : state),
                incubationClock.getElapsedSeconds(),
                incubationClock.getCurrentDay(),
                turner.getTurnsCompleted(),
                pid.getKp(), pid.getKi(), pid.getKd(),
                (uint16_t)(stateMachine.getTargetTemp() * 10.0f),
                (uint16_t)(stateMachine.getHumidityMidpoint() * 10.0f),
                rtc.isPresent() ? rtc.getEpoch2000() : 0
            );

            // Also snapshot state to SD card
            if (sdLogger.isReady()) {
                sdLogger.writeState(
                    (uint8_t)stateMachine.getSpeciesID(),
                    (uint8_t)(state == STATE_PAUSED ? stateMachine.getPreviousState() : state),
                    incubationClock.getElapsedSeconds(),
                    incubationClock.getCurrentDay(),
                    turner.getTurnsCompleted(),
                    stateMachine.getTargetTemp(),
                    stateMachine.getHumidityMidpoint()
                );
            }
        }
    }
}
