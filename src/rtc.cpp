#include "rtc.h"

// =============================================================================
// DS3231 I2C RTC Driver
// Registers: 0x00-0x06 = time, 0x11-0x12 = temperature
// =============================================================================

DS3231::DS3231() : _present(false) {}

void DS3231::begin() {
    Wire.begin();

    // Probe for DS3231 at address 0x68
    Wire.beginTransmission(RTC_I2C_ADDR);
    uint8_t error = Wire.endTransmission();
    _present = (error == 0);

    if (_present) {
        // Disable SQW output, enable battery-backed oscillator
        // Control register (0x0E): disable alarms/SQW, enable oscillator
        writeRegister(0x0E, 0x00);
    }
}

bool DS3231::readDateTime(RTCDateTime& dt) {
    if (!_present) return false;

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(0x00); // Start at register 0
    if (Wire.endTransmission() != 0) return false;

    Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)7);
    if (Wire.available() < 7) return false;

    dt.second    = bcdToDec(Wire.read() & 0x7F);
    dt.minute    = bcdToDec(Wire.read() & 0x7F);
    dt.hour      = bcdToDec(Wire.read() & 0x3F); // 24-hour mode
    dt.dayOfWeek = bcdToDec(Wire.read() & 0x07);
    dt.day       = bcdToDec(Wire.read() & 0x3F);
    dt.month     = bcdToDec(Wire.read() & 0x1F);
    dt.year      = 2000 + bcdToDec(Wire.read());

    return true;
}

bool DS3231::setDateTime(uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute, uint8_t second) {
    if (!_present) return false;

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(0x00); // Start at register 0
    Wire.write(decToBcd(second));
    Wire.write(decToBcd(minute));
    Wire.write(decToBcd(hour));
    Wire.write(decToBcd(0)); // Day of week (not used)
    Wire.write(decToBcd(day));
    Wire.write(decToBcd(month));
    Wire.write(decToBcd(year - 2000));
    return (Wire.endTransmission() == 0);
}

uint32_t DS3231::getEpoch2000() {
    RTCDateTime dt;
    if (!readDateTime(dt)) return 0;

    // Days since 2000-01-01
    uint32_t days = 0;
    for (uint16_t y = 2000; y < dt.year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }

    // Days in months for current year
    static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint8_t m = 1; m < dt.month; m++) {
        days += daysInMonth[m - 1];
        if (m == 2 && (dt.year % 4 == 0 && (dt.year % 100 != 0 || dt.year % 400 == 0))) {
            days++; // Leap year February
        }
    }
    days += dt.day - 1;

    return days * 86400UL + (uint32_t)dt.hour * 3600UL +
           (uint32_t)dt.minute * 60UL + dt.second;
}

float DS3231::getTemperature() {
    if (!_present) return -999.0f;

    // Temperature registers: 0x11 (MSB, signed int) and 0x12 (fractional, upper 2 bits)
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(0x11);
    if (Wire.endTransmission() != 0) return -999.0f;

    Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)2);
    if (Wire.available() < 2) return -999.0f;

    int8_t msb = (int8_t)Wire.read();
    uint8_t lsb = Wire.read();

    return (float)msb + (float)(lsb >> 6) * 0.25f;
}

void DS3231::getFormattedDateTime(char* buf, uint8_t bufSize) {
    RTCDateTime dt;
    if (!readDateTime(dt)) {
        strncpy(buf, "NO RTC", bufSize);
        return;
    }
    snprintf(buf, bufSize, "%04u-%02u-%02u %02u:%02u:%02u",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

void DS3231::getFormattedTime(char* buf, uint8_t bufSize) {
    RTCDateTime dt;
    if (!readDateTime(dt)) {
        strncpy(buf, "--:--:--", bufSize);
        return;
    }
    snprintf(buf, bufSize, "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
}

uint8_t DS3231::bcdToDec(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

uint8_t DS3231::decToBcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

uint8_t DS3231::readRegister(uint8_t reg) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)1);
    return Wire.read();
}

void DS3231::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}
