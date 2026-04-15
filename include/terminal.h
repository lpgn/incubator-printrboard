#ifndef TERMINAL_H
#define TERMINAL_H

#include <Arduino.h>
#include "config.h"
#include "state.h"
#include "pid.h"
#include "heater.h"
#include "humidity.h"
#include "turner.h"
#include "fan.h"
#include "clock.h"
#include "storage.h"
#include "safety.h"

// =============================================================================
// USB Terminal — Serial command interface
// =============================================================================

class Terminal {
public:
    Terminal();

    // Initialize serial
    void begin();

    // Print boot banner
    void printBanner();

    // Poll for incoming commands — call from main loop
    // Returns true if a command was processed
    bool poll();

    // Print status report
    void printStatus();

    // Auto-report timer
    bool shouldAutoReport();

    // Set references to all subsystems (call after constructing all objects)
    void setReferences(StateMachine* sm, PIDController* pid, Heater* heater,
                       HumiditySensor* humid, EggTurner* turner, FanController* fan,
                       SoftClock* clock, Storage* storage, SafetyMonitor* safety);

private:
    char _buf[TERMINAL_BUF_SIZE];
    uint8_t _bufPos;
    unsigned long _lastAutoReport;

    // Subsystem references
    StateMachine* _sm;
    PIDController* _pid;
    Heater* _heater;
    HumiditySensor* _humid;
    EggTurner* _turner;
    FanController* _fan;
    SoftClock* _clock;
    Storage* _storage;
    SafetyMonitor* _safety;

    // Process a complete command line
    void processCommand(const char* cmd);

    // Command handlers
    void cmdHelp();
    void cmdSpecies();
    void cmdSelect(const char* arg);
    void cmdStart();
    void cmdStop();
    void cmdAutotune();
    void cmdPause();
    void cmdResume();
    void cmdStatus();
    void cmdSet(const char* args);
    void cmdLog();
    void cmdReset();
    void cmdSilence();
    void cmdTurn();

    // Helper: skip whitespace and return pointer to next token
    const char* nextToken(const char* str);
};

#endif // TERMINAL_H
