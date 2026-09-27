#ifndef MOTOR_CONTROLLER_SIMPLIFY_OLED_DISPLAY_H
#define MOTOR_CONTROLLER_SIMPLIFY_OLED_DISPLAY_H

#include <Arduino.h>

// SSD1306 128x64 on the Mega's hardware I2C (TWI): SDA = D20, SCL = D21.
// receiver-canbus.ino drives the same panel over software I2C on D6/D7, but
// those are L298N PWM pins on this board.
constexpr uint32_t OLED_I2C_CLOCK_HZ = 400000;  // I2C fast mode

enum CommandSource : uint8_t { SOURCE_NONE, SOURCE_JOYSTICK, SOURCE_WEB };

struct DisplayData
{
    int8_t motor;          // -1 when no source is live
    int8_t arm;
    CommandSource source;  // who drives the wheels
    uint8_t pwm;
    uint32_t batteryMillivolts;
};

// Call before the scheduler starts: U8g2 uses delay() while the panel boots.
void oled_init();

// Redraws the whole screen. Page-buffer mode: 128 bytes of RAM, 8 I2C pages
// per frame (~25 ms at 400 kHz). Call from one task only.
void oled_draw(const DisplayData &data);

#endif
