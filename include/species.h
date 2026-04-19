#ifndef SPECIES_H
#define SPECIES_H

#include <Arduino.h>
#include <avr/pgmspace.h>

// =============================================================================
// Bird species incubation presets
// =============================================================================

enum SpeciesID : uint8_t {
    SPECIES_CHICKEN = 0,
    SPECIES_PIGEON,
    SPECIES_QUAIL,
    SPECIES_DUCK,
    SPECIES_TURKEY,
    SPECIES_GOOSE,
    SPECIES_GUINEA,
    SPECIES_CUSTOM,
    SPECIES_COUNT
};

struct SpeciesPreset {
    char name[12];              // Short name (max 11 chars + null)
    uint8_t totalDays;          // Total incubation period
    uint8_t turningStopDay;     // Day to stop turning (lockdown begins)
    uint16_t tempSetpoint;      // Temperature × 10 (e.g. 375 = 37.5°C)
    uint8_t humiditySetterLo;   // Setter period humidity low bound (%)
    uint8_t humiditySetterHi;   // Setter period humidity high bound (%)
    uint8_t humidityLockdownLo; // Lockdown humidity low bound (%)
    uint8_t humidityLockdownHi; // Lockdown humidity high bound (%)
    uint8_t turnsPerDay;        // Number of egg turns per day
    uint8_t turnDegrees;        // Degrees to rotate per turn
};

// Initialize RAM presets from EEPROM or PROGMEM defaults (call once in setup)
void initSpeciesPresets();

// Save all RAM presets to EEPROM
void saveSpeciesPresets();

// Reset all presets to factory defaults
void resetSpeciesPresets();

// Get a species preset by ID (reads from editable RAM copy)
SpeciesPreset getSpeciesPreset(SpeciesID id);

// Get a reference to a species preset for direct editing
SpeciesPreset& getSpeciesPresetRef(SpeciesID id);

// Get species name string (for display)
const char* getSpeciesName(SpeciesID id);

// Find species by name (case-insensitive, returns SPECIES_COUNT if not found)
SpeciesID findSpeciesByName(const char* name);

// Set custom preset values (legacy — now all presets are editable directly)
void setCustomPreset(const SpeciesPreset& preset);

// Print species list to serial
void printSpeciesList();

// Print details of a specific species
void printSpeciesDetails(SpeciesID id);

#endif // SPECIES_H
