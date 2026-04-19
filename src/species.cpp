#include "species.h"
#include "config.h"
#include <EEPROM.h>

// =============================================================================
// Species presets — default values in PROGMEM, editable copies in RAM
// =============================================================================

#define PRESET_EEPROM_MAGIC     0xAB
#define PRESET_EEPROM_ADDR      0x200
#define PRESET_SIZE             sizeof(SpeciesPreset)

static const SpeciesPreset PROGMEM defaultPresets[SPECIES_COUNT] = {
    // name         days  stop  temp  sL  sH  lL  lH  turns  deg
    {"Chicken",      21,   18,  375,  45, 55, 65, 70,   5,  55},
    {"Pigeon",       18,   15,  375,  50, 60, 65, 75,   3,  38},
    {"Quail",        18,   15,  375,  45, 50, 65, 70,   3,  35},
    {"Duck",         28,   25,  375,  55, 60, 65, 75,   3,  56},
    {"Turkey",       28,   25,  375,  50, 60, 65, 70,   5,  56},
    {"Goose",        30,   27,  375,  50, 55, 70, 75,   5,  70},
    {"Guinea",       28,   25,  375,  45, 50, 65, 70,   3,  47},
    {"Custom",        0,    0,  375,  45, 55, 65, 70,   5,  55},
};

// Editable RAM copies of all presets
static SpeciesPreset ramPresets[SPECIES_COUNT];

static uint8_t calcPresetChecksum() {
    uint8_t cs = 0;
    const uint8_t* p = (const uint8_t*)ramPresets;
    for (uint16_t i = 0; i < sizeof(ramPresets); i++) {
        cs ^= p[i];
    }
    return cs;
}

void initSpeciesPresets() {
    uint8_t magic = EEPROM.read(PRESET_EEPROM_ADDR);
    uint8_t checksum = EEPROM.read(PRESET_EEPROM_ADDR + 1);

    if (magic == PRESET_EEPROM_MAGIC) {
        // Try loading from EEPROM
        uint8_t* p = (uint8_t*)ramPresets;
        for (uint16_t i = 0; i < sizeof(ramPresets); i++) {
            p[i] = EEPROM.read(PRESET_EEPROM_ADDR + 2 + i);
        }
        if (calcPresetChecksum() == checksum) {
            Serial.println(F("[SPECIES] Loaded presets from EEPROM."));
            return;
        }
    }

    // Load defaults from PROGMEM
    for (uint8_t i = 0; i < SPECIES_COUNT; i++) {
        memcpy_P(&ramPresets[i], &defaultPresets[i], sizeof(SpeciesPreset));
    }
    Serial.println(F("[SPECIES] Loaded factory defaults."));
}

void saveSpeciesPresets() {
    uint8_t* p = (uint8_t*)ramPresets;
    for (uint16_t i = 0; i < sizeof(ramPresets); i++) {
        EEPROM.update(PRESET_EEPROM_ADDR + 2 + i, p[i]);
    }
    EEPROM.update(PRESET_EEPROM_ADDR + 1, calcPresetChecksum());
    EEPROM.update(PRESET_EEPROM_ADDR, PRESET_EEPROM_MAGIC);
    Serial.println(F("[SPECIES] Presets saved to EEPROM."));
}

void resetSpeciesPresets() {
    for (uint8_t i = 0; i < SPECIES_COUNT; i++) {
        memcpy_P(&ramPresets[i], &defaultPresets[i], sizeof(SpeciesPreset));
    }
    saveSpeciesPresets();
    Serial.println(F("[SPECIES] Factory defaults restored."));
}

SpeciesPreset getSpeciesPreset(SpeciesID id) {
    if (id >= SPECIES_COUNT) {
        id = SPECIES_CHICKEN;
    }
    return ramPresets[id];
}

SpeciesPreset& getSpeciesPresetRef(SpeciesID id) {
    if (id >= SPECIES_COUNT) {
        id = SPECIES_CHICKEN;
    }
    return ramPresets[id];
}

const char* getSpeciesName(SpeciesID id) {
    static char buf[12];
    if (id < SPECIES_COUNT) {
        strcpy(buf, ramPresets[id].name);
    } else {
        strcpy(buf, "Unknown");
    }
    return buf;
}

SpeciesID findSpeciesByName(const char* name) {
    for (uint8_t i = 0; i < SPECIES_COUNT; i++) {
        if (strcasecmp(name, ramPresets[i].name) == 0) {
            return (SpeciesID)i;
        }
    }
    return SPECIES_COUNT;
}

void setCustomPreset(const SpeciesPreset& preset) {
    ramPresets[SPECIES_CUSTOM] = preset;
}

void printSpeciesList() {
    Serial.println(F("Available presets:"));
    for (uint8_t i = 0; i < SPECIES_COUNT; i++) {
        SpeciesPreset p = ramPresets[i];
        Serial.print(F("  "));
        Serial.print(i + 1);
        Serial.print(F(". "));
        Serial.print(p.name);
        uint8_t len = strlen(p.name);
        for (uint8_t j = len; j < 10; j++) Serial.print(' ');
        Serial.print(F("- "));
        Serial.print(p.totalDays);
        Serial.print(F(" days, "));
        Serial.print(p.tempSetpoint / 10);
        Serial.print('.');
        Serial.print(p.tempSetpoint % 10);
        Serial.print(F("C, "));
        Serial.print(p.humiditySetterLo);
        Serial.print('-');
        Serial.print(p.humiditySetterHi);
        Serial.println(F("% RH"));
    }
}

void printSpeciesDetails(SpeciesID id) {
    if (id >= SPECIES_COUNT) return;
    SpeciesPreset p = ramPresets[id];
    Serial.print(F("  Species: "));
    Serial.print(p.name);
    Serial.print(F(" ("));
    Serial.print(p.totalDays);
    Serial.println(F(" days)"));
    Serial.print(F("  Temp: "));
    Serial.print(p.tempSetpoint / 10);
    Serial.print('.');
    Serial.print(p.tempSetpoint % 10);
    Serial.print(F("C | Humidity: "));
    Serial.print(p.humiditySetterLo);
    Serial.print('-');
    Serial.print(p.humiditySetterHi);
    Serial.print(F("% | Turns/day: "));
    Serial.println(p.turnsPerDay);
    Serial.print(F("  Angle: "));
    Serial.print(p.turnDegrees);
    Serial.print(F("deg | Lockdown day "));
    Serial.print(p.turningStopDay);
    Serial.print(F(" (humidity -> "));
    Serial.print(p.humidityLockdownLo);
    Serial.print('-');
    Serial.print(p.humidityLockdownHi);
    Serial.println(F("%)"));
}
