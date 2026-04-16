#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// PRINTRBOARD REV D — PIN DEFINITIONS
// MCU: AT90USB1286 (Teensy++ 2.0 / Marlin pins_PRINTRBOARD.h numbering)
// Verified against compiled binary (avr-objdump) and Marlin firmware.
// =============================================================================

// --- Stepper Motor: Egg Turner (using X-axis driver) ---
// Marlin: X_STEP_PIN 28, X_DIR_PIN 29, X_ENABLE_PIN 19
#define TURNER_STEP_PIN     28
#define TURNER_DIR_PIN      29
#define TURNER_ENABLE_PIN   19

// --- Heater: Incubator heating element ---
// Marlin HEATER_BED_PIN 14 (2-pin Molex header above the reset button)
#define HEATER_PIN          14

// --- Thermistor: Temperature sensor near eggs ---
// Analog channel 0 = bed thermistor (PF0 / ADC0)
#define THERMISTOR_PIN      0

// --- Spare thermistor (extruder input, available for future use) ---
// Analog channel 1 = extruder thermistor (PF1 / ADC1)
#define THERMISTOR_SPARE_PIN 1

// --- Fan: Air circulation + cooling + humidity regulation ---
// Marlin FAN_PIN 16 (small 2-pin Molex FAN header)
#define FAN_PIN             16

// --- DHT22: Humidity + temperature sensor (optional) ---
// Using PD2 on EXP2 header — safe, does not conflict with steppers/heaters
#define DHT22_PIN           2

// --- Status outputs ---
// LED on PB5 (EXP1 pin 12, or any PB5 breakout)
#define LED_PIN             25
// Buzzer on PD4 (EXP2 pin, does not conflict with X_DIR)
#define BUZZER_PIN          4

// --- Spare MOSFET (extruder heater output, unused) ---
// Marlin HEATER_0_PIN 15 — will stay off in this firmware
#define SPARE_MOSFET_PIN    15

// --- SD Card (onboard microSD slot) ---
// CS is PB6 = Teensyduino pin 26. MOSI/SCK/MISO are hardware SPI (PB2/PB1/PB3).
#define SD_CS_PIN           26

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
#define STATUS_REPORT_MS    10000UL    // Auto-report status every 10 seconds

// =============================================================================
// TIMING
// =============================================================================

#define SECONDS_PER_DAY     86400UL
#define MS_PER_SECOND       1000UL

// =============================================================================
// DS3231 RTC CONFIGURATION (optional — I2C on PD0/PD1)
// =============================================================================

#define RTC_I2C_ADDR        0x68       // DS3231 I2C address
#define RTC_CHECK_INTERVAL  60000UL    // Sync software clock to RTC every 60s

#endif // CONFIG_H
