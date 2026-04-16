#include "heater.h"
#include "config.h"
#include <math.h>
#include <EEPROM.h>

// =============================================================================
// Heater Implementation
// =============================================================================

Heater::Heater()
    : _calCount(0), _currentPWM(0), _sensorFailed(false), _isShutdown(false),
      _manualMode(false), _manualPWM(0), _tempOffset(0.0f),
      _customNominalR(0.0f), _customBeta(0.0f) {
    for (uint8_t i = 0; i < CALIB_MAX_POINTS; i++) {
        _calTable[i].adc = 0;
        _calTable[i].actualTemp = 0.0f;
    }
}

void Heater::begin() {
    pinMode(HEATER_PIN, OUTPUT);
    analogWrite(HEATER_PIN, 0); // Start with heater OFF
    loadCalibrationPoints();
}

uint16_t Heater::readRawADC() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < THERM_OVERSAMPLE; i++) {
        sum += analogRead(THERMISTOR_PIN);
    }
    return sum / THERM_OVERSAMPLE;
}

float Heater::readTemperature() {
    uint16_t adcValue = readRawADC();

    // Check for sensor failure (open circuit or short)
    if (adcValue <= TEMP_SENSOR_FAIL_LO || adcValue >= TEMP_SENSOR_FAIL_HI) {
        _sensorFailed = true;
        return -999.0f; // Invalid reading
    }

    _sensorFailed = false;
    float offset = (_tempOffset == _tempOffset) ? _tempOffset : 0.0f; // NaN guard
    return adcToTemperature(adcValue) + offset;
}

void Heater::setOutput(uint8_t pwm) {
    if (_isShutdown) {
        pwm = 0; // Enforce shutdown
    } else if (_manualMode) {
        pwm = _manualPWM;
    }
    _currentPWM = pwm;
    analogWrite(HEATER_PIN, _currentPWM);
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
    analogWrite(HEATER_PIN, 0);
}

float Heater::adcToTemperature(uint16_t adcValue) {
    if (_calCount >= 2) {
        return interpolateFromTable(adcValue);
    }
    float nominalR = (_customNominalR > 0.0f) ? _customNominalR : THERM_NOMINAL_R;
    float beta = (_customBeta > 0.0f) ? _customBeta : THERM_BETA;
    return adcToTemperature(adcValue, nominalR, beta);
}

float Heater::adcToTemperature(uint16_t adcValue, float nominalR, float beta) {
    // Convert ADC reading to resistance
    // Voltage divider: Vout = Vcc * R_therm / (R_series + R_therm)
    // ADC = Vout / Vcc * ADC_MAX
    // So: R_therm = R_series * ADC / (ADC_MAX - ADC)
    float resistance = THERM_SERIES_R * ((float)adcValue / (float)(THERM_ADC_MAX - adcValue));

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

// =============================================================================
// Calibration Table Methods
// =============================================================================

void Heater::loadCalibrationPoints() {
#if USE_HARDCODED_CAL_TABLE
    _calCount = (hardcodedCalCount > CALIB_MAX_POINTS) ? CALIB_MAX_POINTS : hardcodedCalCount;
    for (uint8_t i = 0; i < _calCount; i++) {
        memcpy_P(&_calTable[i], &hardcodedCalTable[i], sizeof(CalibrationPoint));
    }
#else
    _calCount = EEPROM.read(EEPROM_ADDR_CAL_COUNT);
    if (_calCount == 0xFF) _calCount = 0;
    if (_calCount > CALIB_MAX_POINTS) _calCount = 0;

    for (uint8_t i = 0; i < _calCount; i++) {
        uint16_t addr = EEPROM_ADDR_CAL_TABLE + i * 4;
        uint16_t adc = (uint16_t)EEPROM.read(addr)
                     | ((uint16_t)EEPROM.read(addr + 1) << 8);
        int16_t tempX10 = (int16_t)EEPROM.read(addr + 2)
                        | ((int16_t)EEPROM.read(addr + 3) << 8);
        _calTable[i].adc = adc;
        _calTable[i].actualTemp = (float)tempX10 / 10.0f;
    }
#endif
}

void Heater::saveCalibrationPoints() {
    EEPROM.update(EEPROM_ADDR_CAL_COUNT, _calCount);
    for (uint8_t i = 0; i < _calCount; i++) {
        uint16_t addr = EEPROM_ADDR_CAL_TABLE + i * 4;
        EEPROM.update(addr, (uint8_t)(_calTable[i].adc & 0xFF));
        EEPROM.update(addr + 1, (uint8_t)((_calTable[i].adc >> 8) & 0xFF));
        int16_t tempX10 = (int16_t)(_calTable[i].actualTemp * 10.0f);
        EEPROM.update(addr + 2, (uint8_t)(tempX10 & 0xFF));
        EEPROM.update(addr + 3, (uint8_t)((tempX10 >> 8) & 0xFF));
    }
    for (uint8_t i = _calCount; i < CALIB_MAX_POINTS; i++) {
        uint16_t addr = EEPROM_ADDR_CAL_TABLE + i * 4;
        EEPROM.update(addr, 0xFF);
        EEPROM.update(addr + 1, 0xFF);
        EEPROM.update(addr + 2, 0xFF);
        EEPROM.update(addr + 3, 0xFF);
    }
}

bool Heater::addCalibrationPoint(float actualTemp) {
    if (_calCount >= CALIB_MAX_POINTS) {
        Serial.println(F("[CAL] Table full — clear points first."));
        return false;
    }

    // Stabilize ADC with 32-sample average
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 32; i++) {
        sum += analogRead(THERMISTOR_PIN);
        delay(2);
    }
    uint16_t adc = (uint16_t)(sum / 32);

    _calTable[_calCount].adc = adc;
    _calTable[_calCount].actualTemp = actualTemp;
    _calCount++;

    // Sort by ADC ascending (thermistor ADC goes down as temp goes up,
    // but any monotonic order works for interpolation)
    for (uint8_t i = 0; i < _calCount - 1; i++) {
        for (uint8_t j = i + 1; j < _calCount; j++) {
            if (_calTable[j].adc < _calTable[i].adc) {
                CalibrationPoint tmp = _calTable[i];
                _calTable[i] = _calTable[j];
                _calTable[j] = tmp;
            }
        }
    }

    saveCalibrationPoints();

    Serial.print(F("[CAL] Point added: ADC="));
    Serial.print(adc);
    Serial.print(F(" -> "));
    Serial.print(actualTemp, 1);
    Serial.println(F("C"));
    return true;
}

void Heater::clearCalibrationPoints() {
    _calCount = 0;
    for (uint8_t i = 0; i < CALIB_MAX_POINTS; i++) {
        _calTable[i].adc = 0;
        _calTable[i].actualTemp = 0.0f;
    }
    saveCalibrationPoints();
    Serial.println(F("[CAL] Calibration points cleared."));
}

float Heater::interpolateFromTable(uint16_t adcValue) {
    if (_calCount == 0) return -999.0f;
    if (_calCount == 1) return _calTable[0].actualTemp;

    // Below first point — extrapolate using first two points
    if (adcValue <= _calTable[0].adc) {
        float t = (float)(adcValue - _calTable[0].adc)
                / (float)(_calTable[1].adc - _calTable[0].adc);
        return _calTable[0].actualTemp
             + t * (_calTable[1].actualTemp - _calTable[0].actualTemp);
    }

    // Above last point — extrapolate using last two points
    if (adcValue >= _calTable[_calCount - 1].adc) {
        float t = (float)(adcValue - _calTable[_calCount - 2].adc)
                / (float)(_calTable[_calCount - 1].adc - _calTable[_calCount - 2].adc);
        return _calTable[_calCount - 2].actualTemp
             + t * (_calTable[_calCount - 1].actualTemp - _calTable[_calCount - 2].actualTemp);
    }

    // Bracketed — linear interpolation (16 points max, linear search is fine)
    for (uint8_t i = 0; i < _calCount - 1; i++) {
        if (adcValue >= _calTable[i].adc && adcValue <= _calTable[i + 1].adc) {
            float t = (float)(adcValue - _calTable[i].adc)
                    / (float)(_calTable[i + 1].adc - _calTable[i].adc);
            return _calTable[i].actualTemp
                 + t * (_calTable[i + 1].actualTemp - _calTable[i].actualTemp);
        }
    }

    return -999.0f;
}

void Heater::printCalibrationPoints() {
    Serial.print(F("Calibration points ("));
    Serial.print(_calCount);
    Serial.println(F("):"));
    for (uint8_t i = 0; i < _calCount; i++) {
        Serial.print(F("  ["));
        Serial.print(i);
        Serial.print(F("] ADC="));
        Serial.print(_calTable[i].adc);
        Serial.print(F(" -> "));
        Serial.print(_calTable[i].actualTemp, 1);
        Serial.println(F("C"));
    }
}

void Heater::printCalibrationTable() {
    Serial.print(F("[CALTABLE] "));
    Serial.print(_calCount);
    Serial.print(F(":"));
    for (uint8_t i = 0; i < _calCount; i++) {
        Serial.print(F(" "));
        Serial.print(_calTable[i].adc);
        Serial.print(F(","));
        Serial.print(_calTable[i].actualTemp, 1);
    }
    Serial.println();
}

void Heater::generateTableCode() {
    Serial.println(F(""));
    Serial.println(F("// ========== HARDCODED CALIBRATION TABLE =========="));
    Serial.println(F("// Paste this block into src/heater.cpp, then set"));
    Serial.println(F("// #define USE_HARDCODED_CAL_TABLE 1 in include/heater.h"));
    Serial.println(F(""));
    Serial.print(F("static const uint8_t hardcodedCalCount = "));
    Serial.print(_calCount);
    Serial.println(F(";"));
    Serial.println(F("static const CalibrationPoint PROGMEM hardcodedCalTable[] = {"));
    for (uint8_t i = 0; i < _calCount; i++) {
        Serial.print(F("    {"));
        Serial.print(_calTable[i].adc);
        Serial.print(F(", "));
        Serial.print(_calTable[i].actualTemp, 1);
        Serial.print(F("}"));
        if (i < _calCount - 1) Serial.print(F(","));
        Serial.println();
    }
    Serial.println(F("};"));
    Serial.println(F("// ================================================="));
}

#if USE_HARDCODED_CAL_TABLE
// Paste your generated table here after running `cal generate`
static const uint8_t hardcodedCalCount = 0;
static const CalibrationPoint PROGMEM hardcodedCalTable[] = {
    // {adc, actualTemp},
};
#endif
