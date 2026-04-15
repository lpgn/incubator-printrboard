#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "species.h"

// =============================================================================
// EEPROM Persistent Storage — State save/restore for power recovery
// =============================================================================

// Forward declarations
enum IncubatorState : uint8_t;

struct SavedState {
    uint8_t magic;
    uint8_t speciesID;
    uint8_t state;
    uint32_t elapsedSeconds;
    uint16_t currentDay;
    uint8_t turnsToday;
    float pidKp;
    float pidKi;
    float pidKd;
    uint16_t targetTemp;      // °C × 10
    uint16_t humidityTarget;  // % × 10
    uint8_t checksum;
};

class Storage {
public:
    Storage();

    // Save current state to EEPROM
    void save(uint8_t speciesID, uint8_t state, uint32_t elapsedSeconds,
              uint16_t currentDay, uint8_t turnsToday,
              float kp, float ki, float kd,
              uint16_t targetTemp, uint16_t humidity);

    // Load saved state from EEPROM. Returns true if valid data found.
    bool load(SavedState& outState);

    // Clear EEPROM (factory reset)
    void clear();

    // Check if valid saved state exists
    bool hasValidState();

    // Log an event to the event log area
    void logEvent(uint8_t eventCode, uint16_t eventData);

    // Print event log to serial
    void printEventLog();

private:
    uint8_t calcChecksum(const SavedState& s);
    void writeFloat(uint16_t addr, float value);
    float readFloat(uint16_t addr);
    void writeUint32(uint16_t addr, uint32_t value);
    uint32_t readUint32(uint16_t addr);
    void writeUint16(uint16_t addr, uint16_t value);
    uint16_t readUint16(uint16_t addr);
};

// Event codes for logging
#define EVENT_BOOT          0x01
#define EVENT_START         0x02
#define EVENT_LOCKDOWN      0x03
#define EVENT_HATCHING      0x04
#define EVENT_DONE          0x05
#define EVENT_PAUSE         0x06
#define EVENT_RESUME        0x07
#define EVENT_POWER_RECOVER 0x08
#define EVENT_OVERTEMP      0x10
#define EVENT_UNDERTEMP     0x11
#define EVENT_SENSOR_FAIL   0x12
#define EVENT_HUMID_HIGH    0x13
#define EVENT_HUMID_LOW     0x14
#define EVENT_AUTOTUNE_DONE 0x20
#define EVENT_USER_STOP     0x30

#endif // STORAGE_H
