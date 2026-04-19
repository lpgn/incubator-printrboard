#include "species.h"

// =============================================================================
// Species presets stored in PROGMEM to save SRAM
// =============================================================================
//
// Column layout for each preset row:
//   name  = species name (max 11 chars)
//   days  = total incubation period in days
//   stop  = lockdown day — last day eggs are turned. turning stops here.
//           eggs sit still from this day until hatch.
//   temp  = temperature setpoint in Celsius x10 (e.g. 375 = 37.5C)
//   sL    = setter humidity LOW bound (%) — during turning period
//   sH    = setter humidity HIGH bound (%) — during turning period
//   lL    = lockdown humidity LOW bound (%) — after turning stops
//   lH    = lockdown humidity HIGH bound (%) — after turning stops
//   turns = egg turns per day (MUST be odd number — even values auto-corrected
//           down by firmware so eggs don't end up back where they started)
//   deg   = degrees to rotate per turn (mechanical — depends on egg holder geometry)
//
static const SpeciesPreset PROGMEM presets[SPECIES_COUNT] = {
    // name         days  stop  temp  sL  sH  lL  lH  turns  deg
    {"Chicken",      21,   18,  375,  45, 55, 65, 70,   5,  55},
    {"Pigeon",       18,   15,  375,  50, 60, 65, 75,   3,  55},
    {"Quail",        18,   15,  375,  45, 50, 65, 70,   3,  55},
    {"Duck",         28,   25,  375,  55, 60, 65, 75,   3,  55},
    {"Turkey",       28,   25,  375,  50, 60, 65, 70,   5,  55},
    {"Goose",        30,   27,  375,  50, 55, 70, 75,   5,  55},
    {"Guinea",       28,   25,  375,  45, 50, 65, 70,   3,  55},
    {"Custom",        0,    0,  375,  45, 55, 65, 70,   5,  55},
};

// Custom preset in RAM (user-modifiable)
static SpeciesPreset customPreset = {"Custom", 21, 18, 375, 45, 55, 65, 70, 5, 55};

SpeciesPreset getSpeciesPreset(SpeciesID id) {
    if (id >= SPECIES_COUNT) {
        id = SPECIES_CHICKEN; // fallback
    }
    if (id == SPECIES_CUSTOM) {
        return customPreset;
    }
    SpeciesPreset p;
    memcpy_P(&p, &presets[id], sizeof(SpeciesPreset));
    return p;
}

const char* getSpeciesName(SpeciesID id) {
    static char buf[12];
    if (id == SPECIES_CUSTOM) {
        strcpy(buf, customPreset.name);
    } else if (id < SPECIES_COUNT) {
        SpeciesPreset p;
        memcpy_P(&p, &presets[id], sizeof(SpeciesPreset));
        strcpy(buf, p.name);
    } else {
        strcpy(buf, "Unknown");
    }
    return buf;
}

SpeciesID findSpeciesByName(const char* name) {
    for (uint8_t i = 0; i < SPECIES_COUNT; i++) {
        SpeciesPreset p = getSpeciesPreset((SpeciesID)i);
        // Case-insensitive compare
        if (strcasecmp(name, p.name) == 0) {
            return (SpeciesID)i;
        }
    }
    return SPECIES_COUNT; // not found
}

SpeciesPreset& getCustomPreset() {
    return customPreset;
}

void setCustomPreset(const SpeciesPreset& preset) {
    customPreset = preset;
    strcpy(customPreset.name, "Custom"); // always keep the name
}

void printSpeciesList() {
    Serial.println(F("Available presets:"));
    for (uint8_t i = 0; i < SPECIES_COUNT; i++) {
        SpeciesPreset p = getSpeciesPreset((SpeciesID)i);
        Serial.print(F("  "));
        Serial.print(i + 1);
        Serial.print(F(". "));
        Serial.print(p.name);
        // Pad name to 10 chars
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
    SpeciesPreset p = getSpeciesPreset(id);
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
    Serial.print(F("  Lockdown day "));
    Serial.print(p.turningStopDay);
    Serial.print(F(" (humidity -> "));
    Serial.print(p.humidityLockdownLo);
    Serial.print('-');
    Serial.print(p.humidityLockdownHi);
    Serial.println(F("%)"));
}
