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
    uint16_t tempSetpoint;      // Temperature × 10 (e.g., 375 = 37.5°C)
    uint8_t humiditySetterLo;   // Setter period humidity low bound (%)
    uint8_t humiditySetterHi;   // Setter period humidity high bound (%)
    uint8_t humidityLockdownLo; // Lockdown humidity low bound (%)
    uint8_t humidityLockdownHi; // Lockdown humidity high bound (%)
    uint8_t turnsPerDay;        // Number of egg turns per day
    uint8_t turnDegrees;        // Degrees to rotate per turn
};

// Get a species preset by ID (reads from PROGMEM)
SpeciesPreset getSpeciesPreset(SpeciesID id);

// Get species name string (for display)
const char* getSpeciesName(SpeciesID id);

// Find species by name (case-insensitive, returns SPECIES_COUNT if not found)
SpeciesID findSpeciesByName(const char* name);

// Get the custom species preset (stored in RAM, user-modifiable)
SpeciesPreset& getCustomPreset();

// Set custom preset values
void setCustomPreset(const SpeciesPreset& preset);

// Print species list to serial
void printSpeciesList();

// Print details of a specific species
void printSpeciesDetails(SpeciesID id);

#endif // SPECIES_H
