#ifndef SDLOGGER_H
#define SDLOGGER_H

#include <Arduino.h>
#include <SD.h>

// =============================================================================
// SD Card Logger — writes CSV logs and state snapshots to microSD
// =============================================================================

class SDLogger {
public:
    SDLogger();

    // Initialize SD card on the given chip-select pin
    bool begin(uint8_t csPin);

    // Returns true if SD card is ready
    bool isReady() const;

    // Write a CSV log line. Automatically creates header if file is new.
    bool writeLog(uint32_t millisVal, float tempC, float humidityPct,
                  uint8_t heaterPct, uint8_t fanPct,
                  const char* stateName, uint16_t day, uint8_t turnsToday);

    // Write a state snapshot file (overwritten each time)
    bool writeState(uint8_t speciesID, uint8_t state, uint32_t elapsedSeconds,
                    uint16_t currentDay, uint8_t turnsToday,
                    float targetTemp, float humidityTarget);

    // Print SD card info to Serial
    void printStatus();

private:
    bool _ready;
    uint8_t _csPin;

    bool ensureHeader(const char* filename);
};

#endif // SDLOGGER_H
