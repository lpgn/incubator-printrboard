#include "heater.h"
#include "config.h"
#include "sht31.h"
#include "humidity.h"
#include <math.h>

// =============================================================================
// Heater Implementation
// =============================================================================

Heater::Heater()
    : _currentPWM(0), _sensorFailed(false), _isShutdown(false),
      _manualMode(false), _manualPWM(0), _tempOffset(0.0f),
      _customNominalR(0.0f), _customBeta(0.0f),
      _tempSource(TEMP_SOURCE_THERMISTOR),
      _thermChannel(THERMISTOR_PIN), _digitalFault(false),
      _sht31(nullptr), _dht(nullptr) {
}

void Heater::begin() {
    pinMode(HEATER_PIN, OUTPUT);
    _currentPWM = 0;
    update(); // Start with heater OFF
}

uint16_t Heater::readRawADC() {
    // Return the 16-sample SUM (no divide): heater-PWM ripple dithers the ADC
    // LSB, so the sum carries ~2 extra effective bits of temperature resolution
    // (~0.35°C/count averaged down to ~0.09°C/count).
    uint32_t sum = 0;
    for (uint8_t i = 0; i < THERM_OVERSAMPLE; i++) {
        sum += analogRead(_thermChannel);
    }
    return (uint16_t)sum; // max 16×1023 = 16368, fits uint16_t
}

float Heater::readTemperature() {
    uint16_t adcValue = readRawADC();

    // Check for sensor failure (short or open circuit)
    if (adcValue <= TEMP_SENSOR_FAIL_LO || adcValue >= TEMP_SENSOR_FAIL_HI) {
        _sensorFailed = true;
        return -999.0f; // Invalid reading
    }

    _sensorFailed = false;
    float offset = (_tempOffset == _tempOffset) ? _tempOffset : 0.0f; // NaN guard
    return adcToTemperature(adcValue) + offset;
}

bool Heater::hasDigitalSensor() const {
    if (_tempSource == TEMP_SOURCE_SHT31) return _sht31 && _sht31->isValid();
    if (_tempSource == TEMP_SOURCE_DHT)   return _dht && _dht->isPresent();
    return false;
}

uint8_t Heater::getSht31Address() const {
    return _sht31 ? _sht31->getAddress() : 0;
}

float Heater::readControlTemperature() {
    _digitalFault = false;

    if (_tempSource == TEMP_SOURCE_SHT31) {
        if (_sht31 && _sht31->isValid()) {
            _sensorFailed = false;       // digital source healthy
            return _sht31->getTemperature();
        }
        // Selected but not answering: fault only if it had been working
        // (absent-at-boot is handled at setup, not as a mid-run alarm).
        if (_sht31 && _sht31->everPresent()) _digitalFault = true;
        // fall through to the thermistor — never control blind
    } else if (_tempSource == TEMP_SOURCE_DHT) {
        if (_dht && _dht->isPresent()) {
            float t = _dht->getTemperature();
            if (t > -40.0f && t < 80.0f) {
                _sensorFailed = false;
                return t;
            }
        }
        if (_dht && _dht->everPresent()) _digitalFault = true;
        // fall through to the thermistor
    }

    // Thermistor (default source, or fallback). Sets _sensorFailed on a
    // genuine open/short — if the fallback thermistor is ALSO dead the caller
    // sees isSensorFailed()==true and shuts down (templess = unsafe).
    return readTemperature();
}

void Heater::setOutput(uint8_t pwm) {
    if (_isShutdown) {
        pwm = 0; // Enforce shutdown
    } else if (_manualMode) {
        pwm = _manualPWM;
    }
    _currentPWM = pwm;
    update();
}

void Heater::update() {
    // Slow PWM: toggle pin once per HEATER_SLOW_PWM_PERIOD_MS to eliminate audible whine
    unsigned long now = millis();
    uint16_t period = HEATER_SLOW_PWM_PERIOD_MS;
    unsigned long onTime = ((unsigned long)_currentPWM * period) / 255;
    unsigned long phase = now % period;
    digitalWrite(HEATER_PIN, phase < onTime ? HIGH : LOW);
}

void Heater::setCustomThermistor(float nominalR, float beta) {
    _customNominalR = (nominalR > 0.0f) ? nominalR : 0.0f;
    _customBeta = (beta > 0.0f) ? beta : 0.0f;
}

void Heater::setManualSpeed(int16_t speed) {
    _manualMode = (speed >= 0);
    _manualPWM = (speed >= 0) ? (uint8_t)speed : 0;
}

void Heater::shutdown() {
    _isShutdown = true;
    _currentPWM = 0;
    update();
}

float Heater::adcToTemperature(uint16_t adcValue) {
    float nominalR = (_customNominalR > 0.0f) ? _customNominalR : THERM_NOMINAL_R;
    float beta = (_customBeta > 0.0f) ? _customBeta : THERM_BETA;
    return adcToTemperature(adcValue, nominalR, beta);
}

float Heater::adcToTemperature(uint16_t adcValue, float nominalR, float beta) {
    // Guard: near full scale the divider term (SUM_MAX - adc) → 0 and the
    // math blows up to +inf. That's an open thermistor — return the fail
    // sentinel instead of dividing.
    if (adcValue >= THERM_ADC_SUM_MAX - THERM_OVERSAMPLE) return -999.0f;

    // Convert ADC reading to resistance
    // Voltage divider: Vout = Vcc * R_therm / (R_series + R_therm)
    // ADC = Vout / Vcc * ADC_MAX
    // So: R_therm = R_series * ADC / (ADC_MAX - ADC)
    // adcValue is the 16-sample SUM, so both numerator and denominator use the
    // summed scale: sum = 16×avg and SUM_MAX = 16×1023, hence
    // sum/(SUM_MAX−sum) = 16·avg/(16·1023−16·avg) = avg/(1023−avg) — identical
    // ratio, identical temperature. E.g. 25.0°C: R=100k → avg ≈ 977.0 counts,
    // sum ≈ 15632 → R = 4700·15632/(16368−15632) ≈ 99.8k → 25.0°C. Same
    // invariance holds at 37.5°C (R ≈ 55k, avg ≈ 943, sum ≈ 15088).
    float resistance = THERM_SERIES_R * ((float)adcValue / (float)(THERM_ADC_SUM_MAX - adcValue));

    // Simplified Steinhart-Hart using Beta equation:
    // 1/T = 1/T0 + (1/B) * ln(R/R0)
    float steinhart;
    steinhart = resistance / nominalR;                // R/R0
    steinhart = log(steinhart);                       // ln(R/R0)
    steinhart /= beta;                                // (1/B) * ln(R/R0)
    steinhart += 1.0f / (THERM_NOMINAL_T + 273.15f); // + 1/T0
    steinhart = 1.0f / steinhart;                     // Invert
    steinhart -= 273.15f;                             // Convert to °C

    return steinhart;
}
