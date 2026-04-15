#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// PRINTRBOARD REV D — PIN DEFINITIONS
// MCU: AT90USB1286 (Teensy++ 2.0 compatible pin numbering)
// =============================================================================

// --- Stepper Motor: Egg Turner (using X-axis driver) ---
#define TURNER_STEP_PIN     0
#define TURNER_DIR_PIN      1
#define TURNER_ENABLE_PIN   39

// --- Heater: Incubator heating element (heatbed MOSFET) ---
#define HEATER_PIN          20   // Heatbed MOSFET output (PWM)

// --- Thermistor: Temperature sensor near eggs (heatbed thermistor input) ---
#define THERMISTOR_PIN      0    // Analog channel 0 (ADC0 / PF0)

// --- Spare thermistor (extruder input, available for future use) ---
#define THERMISTOR_SPARE_PIN 1   // Analog channel 1 (ADC1 / PF1)

// --- Fan: Air circulation + cooling + humidity regulation ---
#define FAN_PIN             16   // Fan MOSFET output (PWM)

// --- DHT22: Humidity + temperature sensor (on EXP2 header) ---
#define DHT22_PIN           33   // PE0 on EXP2 expansion header

// --- Status outputs (on EXP1 header) ---
#define LED_PIN             15   // PB5 — Status LED
#define BUZZER_PIN          16   // PB6 — Piezo buzzer for alarms
// Note: If buzzer conflicts with FAN_PIN, use a different EXP pin.
// For Rev D, we'll use an available EXP2 pin for buzzer instead:
#undef BUZZER_PIN
#define BUZZER_PIN          29   // PD4 on EXP2

// --- Spare MOSFET (extruder heater output, unused) ---
#define SPARE_MOSFET_PIN    21   // Extruder MOSFET — available for future use

// =============================================================================
// THERMISTOR CONFIGURATION
// =============================================================================

// NTC 100K thermistor (typical 3D printer thermistor)
// Steinhart-Hart coefficients for 100K NTC (EPCOS B57560G104F)
#define THERM_NOMINAL_R     100000.0f  // Resistance at nominal temperature
#define THERM_NOMINAL_T     25.0f      // Nominal temperature (°C)
#define THERM_BETA          3950.0f    // Beta coefficient
#define THERM_SERIES_R      4700.0f    // Series resistor value (ohms)
#define THERM_ADC_MAX       1023       // 10-bit ADC

// ADC oversampling
#define THERM_OVERSAMPLE    16         // Number of ADC samples to average

// =============================================================================
// PID CONFIGURATION
// =============================================================================

#define PID_SAMPLE_MS       1000       // PID update interval (ms)
#define PID_DEFAULT_KP      4.0f
#define PID_DEFAULT_KI      0.2f
#define PID_DEFAULT_KD      1.0f
#define PID_OUTPUT_MIN      0
#define PID_OUTPUT_MAX      255
#define PID_WINDUP_LIMIT    200.0f     // Anti-windup integral limit

// Autotune
#define AUTOTUNE_CYCLES     8          // Number of oscillation cycles to measure
#define AUTOTUNE_HYSTERESIS 0.5f       // °C band around setpoint for relay

// =============================================================================
// TEMPERATURE SAFETY LIMITS
// =============================================================================

#define TEMP_MAX_CUTOFF     40.0f      // °C — heater OFF immediately
#define TEMP_MIN_WARNING    35.0f      // °C — alarm if below for too long
#define TEMP_MIN_WARN_MS    600000UL   // 10 minutes below min → warning
#define TEMP_SENSOR_FAIL_LO 1          // ADC value indicating open circuit
#define TEMP_SENSOR_FAIL_HI 1022       // ADC value indicating short circuit

// =============================================================================
// DHT22 CONFIGURATION
// =============================================================================

#define DHT_READ_INTERVAL_MS  5000     // Read DHT22 every 5 seconds
#define DHT_MAX_FAILURES      3        // Consecutive failures before warning
#define HUMIDITY_MAX_ALARM    80.0f    // % — fan ramps up
#define HUMIDITY_MIN_ALARM    30.0f    // % — warning to refill water

// =============================================================================
// FAN CONFIGURATION
// =============================================================================

#define FAN_BASE_SPEED      76         // ~30% PWM — base circulation speed
#define FAN_MIN_SPEED       40         // Minimum fan speed (don't stall)
#define FAN_MAX_SPEED       255        // Maximum fan speed

// =============================================================================
// TURNER (STEPPER) CONFIGURATION
// =============================================================================

#define TURNER_STEPS_PER_REV   3200    // 200 steps × 16 microsteps
#define TURNER_DEFAULT_DEGREES 90      // Degrees to rotate per turn
#define TURNER_DEFAULT_RPM     2       // Very slow rotation speed
#define TURNER_ACCEL_STEPS     200     // Steps for acceleration ramp
// Direction alternates each turn automatically

// =============================================================================
// EEPROM CONFIGURATION
// =============================================================================

#define EEPROM_MAGIC_BYTE      0xA5
#define EEPROM_SAVE_INTERVAL_MS 600000UL  // Save state every 10 minutes

// EEPROM addresses
#define EEPROM_ADDR_MAGIC      0x00
#define EEPROM_ADDR_SPECIES    0x01
#define EEPROM_ADDR_STATE      0x02
#define EEPROM_ADDR_START_TIME 0x03    // 4 bytes
#define EEPROM_ADDR_ELAPSED    0x07    // 4 bytes
#define EEPROM_ADDR_DAY        0x0B    // 2 bytes
#define EEPROM_ADDR_TURNS      0x0D    // 1 byte
#define EEPROM_ADDR_KP         0x0E    // 4 bytes (float)
#define EEPROM_ADDR_KI         0x12    // 4 bytes (float)
#define EEPROM_ADDR_KD         0x16    // 4 bytes (float)
#define EEPROM_ADDR_TARGET_TEMP 0x1A   // 2 bytes (°C × 10)
#define EEPROM_ADDR_HUMIDITY   0x1C    // 2 bytes (% × 10)
#define EEPROM_ADDR_CHECKSUM   0x1E    // 1 byte
#define EEPROM_ADDR_CUSTOM     0x20    // 96 bytes — custom species params
#define EEPROM_ADDR_LOG        0x80    // 128 bytes — event log

// =============================================================================
// SERIAL / TERMINAL CONFIGURATION
// =============================================================================

#define SERIAL_BAUD         115200
#define TERMINAL_BUF_SIZE   64         // Command input buffer size
#define STATUS_REPORT_MS    60000UL    // Auto-report status every 60 seconds

// =============================================================================
// TIMING
// =============================================================================

#define SECONDS_PER_DAY     86400UL
#define MS_PER_SECOND       1000UL

#endif // CONFIG_H
