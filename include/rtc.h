#ifndef RTC_H
#define RTC_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// =============================================================================
// DS3231 RTC Driver — Optional, auto-detected via I2C
// Uses hardware I2C (PD0=SCL, PD1=SDA on AT90USB1286)
// =============================================================================

struct RTCDateTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t dayOfWeek;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

class DS3231 {
public:
    DS3231();

    // Initialize I2C and probe for DS3231
    void begin();

    // Is a DS3231 present on the I2C bus?
    bool isPresent() const { return _present; }

    // Read current date/time from RTC
    bool readDateTime(RTCDateTime& dt);

    // Set RTC date/time
    bool setDateTime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t minute, uint8_t second);

    // Get UNIX-like timestamp (seconds since 2000-01-01)
    uint32_t getEpoch2000();

    // Get RTC temperature (DS3231 has a built-in temp sensor, ±0.25°C)
    float getTemperature();

    // Get formatted date/time string "YYYY-MM-DD HH:MM:SS"
    void getFormattedDateTime(char* buf, uint8_t bufSize);

    // Get formatted time string "HH:MM:SS"
    void getFormattedTime(char* buf, uint8_t bufSize);

private:
    bool _present;

    // BCD conversion helpers
    static uint8_t bcdToDec(uint8_t bcd);
    static uint8_t decToBcd(uint8_t dec);

    // Read a single register
    uint8_t readRegister(uint8_t reg);

    // Write a single register
    void writeRegister(uint8_t reg, uint8_t value);
};

#endif // RTC_H
