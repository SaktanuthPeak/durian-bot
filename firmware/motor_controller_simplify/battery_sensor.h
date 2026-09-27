#ifndef MOTOR_CONTROLLER_SIMPLIFY_BATTERY_SENSOR_H
#define MOTOR_CONTROLLER_SIMPLIFY_BATTERY_SENSOR_H

#include <Arduino.h>

// Common 0-25 V analog voltage module (30k / 7.5k divider), same values as
// firmware/motor_controller_mega/robot_config.h and receiver-canbus.ino.
constexpr uint8_t BATTERY_ADC_CHANNEL = 0;  // A0
constexpr float BATTERY_R1_OHMS = 30000.0f;
constexpr float BATTERY_R2_OHMS = 7500.0f;
constexpr float ADC_REFERENCE_VOLTAGE = 5.0f;
constexpr float ADC_COUNTS = 1023.0f;
constexpr uint8_t BATTERY_SAMPLES = 8;      // moving average window

struct BatterySample
{
    uint16_t adc;         // averaged raw ADC, 0..1023
    uint32_t millivolts;  // battery voltage after the divider
};

void battery_init();

// Takes one new ADC reading and returns the moving average. Call from one
// task only: the ADC is a single shared peripheral.
BatterySample battery_read();

#endif
