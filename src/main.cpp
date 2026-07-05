#include <Arduino.h>
#include <avr/wdt.h>
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
#include "sht31.h"

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
SHT31Sensor     sht31;

// =============================================================================
// Timing variables
// =============================================================================
unsigned long lastPIDUpdate = 0;
unsigned long lastDHTRead = 0;
unsigned long lastEEPROMSave = 0;
unsigned long lastRTCSync = 0;
uint16_t lastDay = 0;

// Current sensor readings (global for sharing between modules)
float currentTemp = 0.0f;
float currentHumidity = 50.0f;

// Transient over-temp hold: heater forced off (non-latching) until temp
// falls 0.5C below the limit. The latching shutdown stays with the ERROR path.
bool overTempHold = false;

// RTC re-sync anchors (software clock drift correction)
uint32_t rtcSyncEpoch = 0;
uint32_t rtcSyncElapsed = 0;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    // SAFETY FIRST: after a watchdog reset the WDT restarts at its minimum
    // 16ms timeout — disable it immediately or setup()'s delays boot-loop.
    // And drive the heater pin LOW before anything else so a reset can never
    // leave the heater stuck ON.
    MCUSR = 0;
    wdt_disable();
    pinMode(HEATER_PIN, OUTPUT);
    digitalWrite(HEATER_PIN, LOW);

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
    initSpeciesPresets();

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

    // --- Temperature/humidity source configuration (persisted per-source) ---
    // Wire the shared SHT31 into both consumers.
    heater.setDigitalSources(&sht31, &humiditySensor);
    humiditySensor.setSht31(&sht31);

    // Apply stored per-source pin/address (fall back to config.h defaults).
    uint8_t thermCh = storage.loadThermChannel(THERMISTOR_PIN);
    heater.setThermChannel(thermCh);

    uint8_t dhtPin = storage.loadDhtPin(DHT22_PIN);
    humiditySensor.setPin(dhtPin);
    humiditySensor.begin();

    // SHT31 shares the I2C bus already brought up by rtc.begin() above — do
    // NOT call Wire.begin() again here.
    uint8_t shtAddr = storage.loadShtAddr(SHT31_I2C_ADDR_DEFAULT);
    bool shtOk = sht31.begin(shtAddr);
    if (shtOk) {
        Serial.print(F("[SHT31] Detected at 0x"));
        Serial.println(shtAddr, HEX);
    }

    // Select the control source, validating it can actually run — never leave
    // control templess: fall back to the thermistor if the chosen chip is absent.
    TempSource src = (TempSource)storage.loadTempSource();
    heater.setTempSource(src);
    if (src == TEMP_SOURCE_SHT31 && !shtOk) {
        Serial.print(F("[TEMP] SHT31 selected but none answered at 0x"));
        Serial.print(shtAddr, HEX);
        Serial.println(F(" — falling back to THERMISTOR for control."));
        heater.setTempSource(TEMP_SOURCE_THERMISTOR);
    } else if (src == TEMP_SOURCE_DHT) {
        bool dhtOk = false;
        for (uint8_t i = 0; i < 3 && !dhtOk; i++) dhtOk = humiditySensor.read();
        if (!dhtOk) {
            Serial.println(F("[TEMP] DHT selected but not responding — falling back to THERMISTOR for control."));
            heater.setTempSource(TEMP_SOURCE_THERMISTOR);
        }
    }

    Serial.print(F("[TEMP] Control source: "));
    switch (heater.getTempSource()) {
        case TEMP_SOURCE_SHT31: Serial.print(F("SHT31 @0x")); Serial.println(shtAddr, HEX); break;
        case TEMP_SOURCE_DHT:   Serial.print(F("DHT pin ")); Serial.println(dhtPin); break;
        default:                Serial.print(F("thermistor ADC ch")); Serial.println(thermCh); break;
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
        Serial.println(F("  AUTO-RESUMING incubation..."));
        Serial.println(F("  Type 'stop' if you wish to abort."));
        Serial.println(F("========================================"));
        Serial.println();

        // Restore PID tuning
        pid.setTunings(saved.pidKp, saved.pidKi, saved.pidKd);
        pid.setSetpoint((float)saved.targetTemp / 10.0f);

        // Set species
        stateMachine.setSpecies((SpeciesID)saved.speciesID);

        // Restore the saved state directly — do not wait for user
        stateMachine.forceState((IncubatorState)saved.state);

        // Re-apply saved setpoint overrides. The control loop re-reads the
        // stateMachine targets every cycle, so pid.setSetpoint alone would be
        // overwritten on the first PID pass.
        float savedTemp = (float)saved.targetTemp / 10.0f;
        float presetTemp = stateMachine.getTargetTemp();
        if (savedTemp < presetTemp - 0.05f || savedTemp > presetTemp + 0.05f) {
            stateMachine.setTempOverride(savedTemp);
            Serial.print(F("  Restored temp override: "));
            Serial.print(savedTemp, 1);
            Serial.println(F("C"));
        }
        float savedHumid = (float)saved.humidityTarget / 10.0f;
        float presetHumid = stateMachine.getHumidityMidpoint();
        if (savedHumid >= 25.0f && savedHumid <= 85.0f &&
            (savedHumid < presetHumid - 0.05f || savedHumid > presetHumid + 0.05f)) {
            // Only the midpoint was persisted — restore as a symmetric ±5% band
            stateMachine.setHumidityOverride((uint8_t)(savedHumid - 5.0f),
                                             (uint8_t)(savedHumid + 5.0f));
            Serial.print(F("  Restored humidity override midpoint: "));
            Serial.print(savedHumid, 0);
            Serial.println(F("%"));
        }

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
        // Clock is now running — no pause

        // Restore turner state and enable if appropriate
        turner.setTurnsPerDay(stateMachine.getActivePreset().turnsPerDay);
        turner.setDegreesPerTurn(stateMachine.getActivePreset().turnDegrees);
        turner.setTurnsCompleted(saved.turnsToday);
        if (stateMachine.isTurningAllowed()) {
            turner.setEnabled(true);
        }

        // Start heating and fan immediately for all active states
        IncubatorState resumeState = (IncubatorState)saved.state;
        if (resumeState == STATE_INCUBATING || resumeState == STATE_LOCKDOWN || resumeState == STATE_HATCHING) {
            heater.clearShutdown();
            heater.setManualSpeed(-1);
            pid.reset();
            pid.setSetpoint(stateMachine.getTargetTemp());
            fan.setManualSpeed(-1); // Auto mode
        }

        storage.logEvent(EVENT_RESUME, incubationClock.getCurrentDay());

        // Anchor day-change detection to the day we SAVED in, not 0: within
        // the same day this stops the first loop from false-detecting a day
        // change and wiping the restored turn count; after a multi-day
        // blackout the loop's day-change block then legitimately fires and
        // its transition loop catches up every missed phase.
        lastDay = saved.currentDay;

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
    lastRTCSync = millis();

    // Watchdog: all peripherals are up — from here loop() must run and call
    // wdt_reset() at least every 4 seconds or the MCU resets (heater-safe:
    // early setup() forces the heater pin LOW on every boot).
    wdt_enable(WDTO_4S);
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    wdt_reset();

    unsigned long now = millis();
    IncubatorState state = stateMachine.getState();

    // --- Update software clock ---
    incubationClock.update();

    // --- Poll the shared SHT31 (self rate-limited to SHT31_READ_INTERVAL_MS) ---
    // Feeds both the control temperature and the humidity module from one read.
    sht31.update();

    // --- Poll terminal for commands ---
    terminal.poll();

    // --- Read DHT22 sensor (every 5 seconds) ---
    if (now - lastDHTRead >= DHT_READ_INTERVAL_MS) {
        lastDHTRead = now;
        // read() also handles periodic re-detection when the sensor is absent
        if (humiditySensor.read()) {
            currentHumidity = humiditySensor.getHumidity();
        }
        // If read failed, keep last known value
    }

    // --- PID Temperature Control (every 1 second) ---
    if (now - lastPIDUpdate >= PID_SAMPLE_MS) {
        lastPIDUpdate = now;

        currentTemp = heater.readControlTemperature();
        uint16_t currentADC = heater.readRawADC();

        if (stateMachine.isHeatingAllowed()) {
            // Release the transient over-temp hold with 0.5C hysteresis
            if (overTempHold && !heater.isSensorFailed() &&
                currentTemp < safety.getMaxTemp() - 0.5f) {
                overTempHold = false;
                fan.setManualSpeed(-1); // Fan back to auto
            }

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
                    // Do NOT write a full state record here — state is IDLE
                    // now and a saved IDLE record would make every later boot
                    // look like a power recovery. The new gains live in the
                    // pid object and persist at the next in-run EEPROM save.
                }
            } else if (currentTemp > safety.getMaxTemp() || overTempHold) {
                // Over-temp — hold heater OFF. NEVER conditional (previously
                // skipped in ADC-target mode). Non-latching: PID resumes once
                // temp drops 0.5C below the limit; the true latching shutdown
                // stays with the safety/ERROR path below.
                overTempHold = true;
                heater.setOutput(0);
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

    // --- Heater slow PWM update ---
    // MUST run every loop() iteration, not just each PID pass: the 2s
    // software PWM needs sub-second pin sampling or the duty degenerates
    // to ~0/50/100% steps.
    heater.update();

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

            // Check for state transitions — loop so a multi-day catch-up
            // (e.g. RTC blackout recovery) fires EVERY missed phase change,
            // not just the first one
            while (stateMachine.checkDayTransitions(currentDay)) {
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
                    storage.invalidateState();
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
        // Digital control-sensor fault (SHT31/DHT lost mid-run, now running on
        // the thermistor): raise the alarm but do NOT shut the heater down.
        safety.setDigitalFault(heater.isDigitalFault());
        static bool digFaultLogged = false;
        if (heater.isDigitalFault()) {
            if (!digFaultLogged) {
                storage.logEvent(EVENT_SENSOR_FAIL, 0);
                digFaultLogged = true;
            }
        } else {
            digFaultLogged = false;
        }

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

        // Log under-temp once per episode (rising edge)
        static bool underTempLogged = false;
        if (safety.isUnderTemp()) {
            if (!underTempLogged) {
                storage.logEvent(EVENT_UNDERTEMP, (uint16_t)(currentTemp * 10));
                underTempLogged = true;
            }
        } else {
            underTempLogged = false;
        }
    }

    // --- Periodic re-sync of software clock to RTC ---
    // Corrects MCU crystal drift over multi-week incubations. Only small
    // deltas are applied so an intentional 'set day'/'set elapsed' jump or a
    // pause is just re-anchored, never "corrected" away.
    if (now - lastRTCSync >= RTC_CHECK_INTERVAL) {
        lastRTCSync = now;
        if (rtc.isPresent() && incubationClock.isRunning()) {
            uint32_t epoch = rtc.getEpoch2000(); // 0 when RTC time is invalid
            if (epoch > 0) {
                if (rtcSyncEpoch > 0 && epoch > rtcSyncEpoch) {
                    uint32_t rtcDelta = epoch - rtcSyncEpoch;
                    uint32_t softDelta = incubationClock.getElapsedSeconds() - rtcSyncElapsed;
                    int32_t drift = (int32_t)rtcDelta - (int32_t)softDelta;
                    if (drift != 0 && drift >= -5 && drift <= 5) {
                        incubationClock.resumeFrom(rtcSyncElapsed + rtcDelta);
                    }
                }
                rtcSyncEpoch = epoch;
                rtcSyncElapsed = incubationClock.getElapsedSeconds();
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
                   state == STATE_HATCHING || state == STATE_PAUSED ||
                   state == STATE_PREHEATING) {

            IncubatorState saveState =
                (state == STATE_PAUSED) ? stateMachine.getPreviousState() : state;
            // PREHEATING is recorded as INCUBATING so resume works (matches cmdSave)
            if (saveState == STATE_PREHEATING) saveState = STATE_INCUBATING;

            storage.save(
                (uint8_t)stateMachine.getSpeciesID(),
                (uint8_t)saveState,
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
                    (uint8_t)saveState,
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
