#include "battery_sensor.h"

namespace
{
    uint16_t samples[BATTERY_SAMPLES];
    uint8_t sampleIndex = 0;
    uint8_t sampleCount = 0;

    // Register-level single conversion, equivalent to analogRead() on A0..A7.
    uint16_t adc_read(uint8_t channel)
    {
        ADMUX = (1 << REFS0) | (channel & 0x07);  // AVCC reference, channel 0..7
        ADCSRB &= ~(1 << MUX5);                   // MUX5 = 0 selects A0..A7
        ADCSRA = (1 << ADEN) | (1 << ADSC)        // enable, start one conversion
               | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // 16 MHz / 128 = 125 kHz
        while (ADCSRA & (1 << ADSC)) {}           // ~104 us at 125 kHz
        return ADC;
    }
}

void battery_init()
{
    pinMode(A0 + BATTERY_ADC_CHANNEL, INPUT);
}

BatterySample battery_read()
{
    samples[sampleIndex] = adc_read(BATTERY_ADC_CHANNEL);
    sampleIndex = (sampleIndex + 1) % BATTERY_SAMPLES;
    if (sampleCount < BATTERY_SAMPLES) sampleCount++;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < sampleCount; i++) sum += samples[i];
    const uint16_t adc = static_cast<uint16_t>(sum / sampleCount);

    const float moduleVoltage =
        (static_cast<float>(adc) * ADC_REFERENCE_VOLTAGE) / ADC_COUNTS;
    const float batteryVoltage = moduleVoltage *
        ((BATTERY_R1_OHMS + BATTERY_R2_OHMS) / BATTERY_R2_OHMS);

    return BatterySample{
        adc,
        batteryVoltage > 0.0f
            ? static_cast<uint32_t>(batteryVoltage * 1000.0f + 0.5f)
            : 0,
    };
}
