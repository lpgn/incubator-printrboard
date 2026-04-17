#include "storage.h"
#include "config.h"
#include <EEPROM.h>

Storage::Storage() {}

void Storage::save(uint8_t speciesID, uint8_t state, uint32_t elapsedSeconds,
                   uint16_t currentDay, uint8_t turnsToday,
                   float kp, float ki, float kd,
                   uint16_t targetTemp, uint16_t humidity,
                   uint32_t epoch) {

    SavedState s;
    s.magic = EEPROM_MAGIC_BYTE;
    s.speciesID = speciesID;
    s.state = state;
    s.elapsedSeconds = elapsedSeconds;
    s.currentDay = currentDay;
    s.turnsToday = turnsToday;
    s.epoch = epoch;
    s.pidKp = kp;
    s.pidKi = ki;
    s.pidKd = kd;
    s.targetTemp = targetTemp;
    s.humidityTarget = humidity;
    s.checksum = calcChecksum(s);

    // Write using EEPROM.update() to minimize wear
    EEPROM.update(EEPROM_ADDR_MAGIC, s.magic);
    EEPROM.update(EEPROM_ADDR_SPECIES, s.speciesID);
    EEPROM.update(EEPROM_ADDR_STATE, s.state);
    writeUint32(EEPROM_ADDR_START_TIME, s.epoch);
    writeUint32(EEPROM_ADDR_ELAPSED, s.elapsedSeconds);
    writeUint16(EEPROM_ADDR_DAY, s.currentDay);
    EEPROM.update(EEPROM_ADDR_TURNS, s.turnsToday);
    writeFloat(EEPROM_ADDR_KP, s.pidKp);
    writeFloat(EEPROM_ADDR_KI, s.pidKi);
    writeFloat(EEPROM_ADDR_KD, s.pidKd);
    writeUint16(EEPROM_ADDR_TARGET_TEMP, s.targetTemp);
    writeUint16(EEPROM_ADDR_HUMIDITY, s.humidityTarget);
    EEPROM.update(EEPROM_ADDR_CHECKSUM, s.checksum);
}

bool Storage::load(SavedState& outState) {
    outState.magic = EEPROM.read(EEPROM_ADDR_MAGIC);
    if (outState.magic != EEPROM_MAGIC_BYTE) return false;

    outState.speciesID = EEPROM.read(EEPROM_ADDR_SPECIES);
    outState.state = EEPROM.read(EEPROM_ADDR_STATE);
    outState.epoch = readUint32(EEPROM_ADDR_START_TIME);
    outState.elapsedSeconds = readUint32(EEPROM_ADDR_ELAPSED);
    outState.currentDay = readUint16(EEPROM_ADDR_DAY);
    outState.turnsToday = EEPROM.read(EEPROM_ADDR_TURNS);
    outState.pidKp = readFloat(EEPROM_ADDR_KP);
    outState.pidKi = readFloat(EEPROM_ADDR_KI);
    outState.pidKd = readFloat(EEPROM_ADDR_KD);
    outState.targetTemp = readUint16(EEPROM_ADDR_TARGET_TEMP);
    outState.humidityTarget = readUint16(EEPROM_ADDR_HUMIDITY);
    outState.checksum = EEPROM.read(EEPROM_ADDR_CHECKSUM);

    // Verify checksum
    uint8_t expected = calcChecksum(outState);
    if (outState.checksum != expected) return false;

    // Sanity checks
    if (outState.speciesID >= SPECIES_COUNT) return false;
    if (outState.targetTemp < 300 || outState.targetTemp > 450) return false; // 30-45°C

    return true;
}

void Storage::clear() {
    // Only clear our data area, not the entire EEPROM
    for (uint16_t i = 0; i < 256; i++) {
        EEPROM.update(i, 0xFF);
    }
    Serial.println(F("[STORAGE] EEPROM cleared."));
}

bool Storage::hasValidState() {
    if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC_BYTE) return false;
    SavedState s;
    return load(s);
}

void Storage::logEvent(uint8_t eventCode, uint16_t eventData) {
    // Simple circular log: 16 entries × 8 bytes each = 128 bytes
    // Each entry: [code, data_hi, data_lo, day_hi, day_lo, hour, min, sec]

    // Find the next empty slot or overwrite oldest
    static uint8_t logIndex = 0;
    uint16_t addr = EEPROM_ADDR_LOG + (logIndex * 8);

    EEPROM.update(addr, eventCode);
    EEPROM.update(addr + 1, (uint8_t)(eventData >> 8));
    EEPROM.update(addr + 2, (uint8_t)(eventData & 0xFF));
    // Remaining bytes could store timestamp, but for simplicity just store zeros
    EEPROM.update(addr + 3, 0);
    EEPROM.update(addr + 4, 0);
    EEPROM.update(addr + 5, 0);
    EEPROM.update(addr + 6, 0);
    EEPROM.update(addr + 7, 0);

    logIndex = (logIndex + 1) % 16;
}

void Storage::printEventLog() {
    Serial.println(F("Event Log:"));
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t addr = EEPROM_ADDR_LOG + (i * 8);
        uint8_t code = EEPROM.read(addr);
        if (code == 0xFF) continue; // Empty slot

        uint16_t data = ((uint16_t)EEPROM.read(addr + 1) << 8) | EEPROM.read(addr + 2);

        Serial.print(F("  ["));
        Serial.print(i);
        Serial.print(F("] "));

        switch (code) {
            case EVENT_BOOT:          Serial.print(F("BOOT")); break;
            case EVENT_START:         Serial.print(F("START")); break;
            case EVENT_LOCKDOWN:      Serial.print(F("LOCKDOWN")); break;
            case EVENT_HATCHING:      Serial.print(F("HATCHING")); break;
            case EVENT_DONE:          Serial.print(F("DONE")); break;
            case EVENT_PAUSE:         Serial.print(F("PAUSE")); break;
            case EVENT_RESUME:        Serial.print(F("RESUME")); break;
            case EVENT_POWER_RECOVER: Serial.print(F("POWER RECOVERY")); break;
            case EVENT_OVERTEMP:      Serial.print(F("OVER-TEMP")); break;
            case EVENT_UNDERTEMP:     Serial.print(F("UNDER-TEMP")); break;
            case EVENT_SENSOR_FAIL:   Serial.print(F("SENSOR FAIL")); break;
            case EVENT_HUMID_HIGH:    Serial.print(F("HUMID HIGH")); break;
            case EVENT_HUMID_LOW:     Serial.print(F("HUMID LOW")); break;
            case EVENT_AUTOTUNE_DONE: Serial.print(F("AUTOTUNE DONE")); break;
            case EVENT_USER_STOP:     Serial.print(F("USER STOP")); break;
            default:                  Serial.print(F("UNKNOWN")); break;
        }

        if (data > 0) {
            Serial.print(F(" data="));
            Serial.print(data);
        }
        Serial.println();
    }
}

uint8_t Storage::calcChecksum(const SavedState& s) {
    uint8_t cs = 0;
    cs ^= s.magic;
    cs ^= s.speciesID;
    cs ^= s.state;
    cs ^= (uint8_t)(s.elapsedSeconds & 0xFF);
    cs ^= (uint8_t)((s.elapsedSeconds >> 8) & 0xFF);
    cs ^= (uint8_t)(s.currentDay & 0xFF);
    cs ^= s.turnsToday;
    cs ^= (uint8_t)(s.epoch & 0xFF);
    cs ^= (uint8_t)((s.epoch >> 8) & 0xFF);
    cs ^= (uint8_t)((s.epoch >> 16) & 0xFF);
    cs ^= (uint8_t)((s.epoch >> 24) & 0xFF);
    // XOR the float bytes
    const uint8_t* fp;
    fp = (const uint8_t*)&s.pidKp;
    for (uint8_t i = 0; i < 4; i++) cs ^= fp[i];
    fp = (const uint8_t*)&s.pidKi;
    for (uint8_t i = 0; i < 4; i++) cs ^= fp[i];
    fp = (const uint8_t*)&s.pidKd;
    for (uint8_t i = 0; i < 4; i++) cs ^= fp[i];
    cs ^= (uint8_t)(s.targetTemp & 0xFF);
    cs ^= (uint8_t)(s.humidityTarget & 0xFF);
    return cs;
}

void Storage::writeFloat(uint16_t addr, float value) {
    const uint8_t* p = (const uint8_t*)&value;
    for (uint8_t i = 0; i < 4; i++) {
        EEPROM.update(addr + i, p[i]);
    }
}

float Storage::readFloat(uint16_t addr) {
    float value;
    uint8_t* p = (uint8_t*)&value;
    for (uint8_t i = 0; i < 4; i++) {
        p[i] = EEPROM.read(addr + i);
    }
    return value;
}

void Storage::writeUint32(uint16_t addr, uint32_t value) {
    EEPROM.update(addr, (uint8_t)(value & 0xFF));
    EEPROM.update(addr + 1, (uint8_t)((value >> 8) & 0xFF));
    EEPROM.update(addr + 2, (uint8_t)((value >> 16) & 0xFF));
    EEPROM.update(addr + 3, (uint8_t)((value >> 24) & 0xFF));
}

uint32_t Storage::readUint32(uint16_t addr) {
    return (uint32_t)EEPROM.read(addr)
         | ((uint32_t)EEPROM.read(addr + 1) << 8)
         | ((uint32_t)EEPROM.read(addr + 2) << 16)
         | ((uint32_t)EEPROM.read(addr + 3) << 24);
}

void Storage::writeUint16(uint16_t addr, uint16_t value) {
    EEPROM.update(addr, (uint8_t)(value & 0xFF));
    EEPROM.update(addr + 1, (uint8_t)((value >> 8) & 0xFF));
}

uint16_t Storage::readUint16(uint16_t addr) {
    return (uint16_t)EEPROM.read(addr) | ((uint16_t)EEPROM.read(addr + 1) << 8);
}

void Storage::saveCalibration(float tempOffset, float nominalR, float beta) {
    writeFloat(EEPROM_ADDR_TEMP_OFFSET, tempOffset);
    writeFloat(EEPROM_ADDR_THERM_R25, nominalR);
    writeFloat(EEPROM_ADDR_THERM_BETA, beta);
}

void Storage::loadCalibration(float& tempOffset, float& nominalR, float& beta) {
    tempOffset = readFloat(EEPROM_ADDR_TEMP_OFFSET);
    nominalR = readFloat(EEPROM_ADDR_THERM_R25);
    beta = readFloat(EEPROM_ADDR_THERM_BETA);
}

void Storage::savePreheatMax(uint8_t pwm) {
    EEPROM.update(EEPROM_ADDR_PREHEAT_MAX, pwm);
}

uint8_t Storage::loadPreheatMax() {
    uint8_t val = EEPROM.read(EEPROM_ADDR_PREHEAT_MAX);
    if (val == 0xFF) return 0; // Uninitialized = use compile-time default
    return val;
}
