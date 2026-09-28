#include "battery_sensor.h"

namespace
{
    uint16_t samples[BATTERY_SAMPLES];
    uint8_t sampleIndex = 0;
    uint8_t sampleCount = 0;
}

void battery_init()
{
    pinMode(BATTERY_PIN, INPUT);
}

BatterySample battery_read()
{
    // Keep the last few readings and average them: the motors make the raw
    // value jump around.
    samples[sampleIndex] = analogRead(BATTERY_PIN);
    sampleIndex = (sampleIndex + 1) % BATTERY_SAMPLES;
    if (sampleCount < BATTERY_SAMPLES) sampleCount++;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < sampleCount; i++) sum += samples[i];
    const uint16_t adc = sum / sampleCount;

    // analogRead 0..1023 -> 0..5 V at the S pin -> x5 for the battery voltage.
    const float pinVoltage = adc * ADC_REFERENCE_VOLTAGE / ADC_COUNTS;
    const float batteryVoltage = pinVoltage * BATTERY_DIVIDER_RATIO;

    return BatterySample{adc, (uint32_t)(batteryVoltage * 1000.0f + 0.5f)};
}
