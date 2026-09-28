#ifndef MOTOR_CONTROLLER_SUPERLOOP_BATTERY_SENSOR_H
#define MOTOR_CONTROLLER_SUPERLOOP_BATTERY_SENSOR_H

#include <Arduino.h>

// Voltage sensor module 0-25 V: a 30k / 7.5k divider on the board scales the
// battery down 5 times, so 25 V at the input becomes 5 V at the S pin.
constexpr uint8_t BATTERY_PIN = A0;              // module "S" pin
constexpr float BATTERY_DIVIDER_RATIO = 5.0f;    // (30k + 7.5k) / 7.5k
constexpr float ADC_REFERENCE_VOLTAGE = 5.0f;    // measure the Mega's 5V pin and adjust
constexpr float ADC_COUNTS = 1023.0f;
constexpr uint8_t BATTERY_SAMPLES = 8;           // moving average window

struct BatterySample
{
    uint16_t adc;         // averaged analogRead() value, 0..1023
    uint32_t millivolts;  // battery voltage
};

void battery_init();

// Takes one new analogRead() and returns the average of the last readings.
BatterySample battery_read();

#endif
