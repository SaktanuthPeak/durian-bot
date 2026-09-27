#include "oled_display.h"

#include <U8g2lib.h>
#include <Wire.h>

namespace
{
    // Page buffer (_1_) keeps the frame buffer at 128 bytes instead of 1 KB.
    U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

    // If the panel is an SH1106, use this line instead:
    // U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

    // Same short labels as receiver-canbus.ino, extended with spin/pump/head.
    const char *status_text(int8_t status)
    {
        switch (status)
        {
            case 0: return "STOP";
            case 1: return "FWD";
            case 2: return "BACK";
            case 3: return "LEFT";
            case 4: return "RIGHT";
            case 5: return "FWD-L";
            case 6: return "FWD-R";
            case 7: return "BACK-L";
            case 8: return "BACK-R";
            case 9: return "SPIN-L";
            case 10: return "SPIN-R";
            case 11: return "PUMP ON";
            case 12: return "PUMP OFF";
            case 13: return "HEAD UP";
            case 14: return "HEAD DN";
            default: return "NO DATA";
        }
    }

    const char *source_text(CommandSource source)
    {
        switch (source)
        {
            case SOURCE_JOYSTICK: return "JOY";
            case SOURCE_WEB: return "WEB";
            default: return "---";
        }
    }
}

void oled_init()
{
    oled.setBusClock(OLED_I2C_CLOCK_HZ);
    oled.begin();

    oled.firstPage();
    do
    {
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(34, 28, "DURIAN BOT");
        oled.drawStr(34, 44, "Starting...");
    } while (oled.nextPage());
}

void oled_draw(const DisplayData &data)
{
    oled.firstPage();
    do
    {
        oled.setFont(u8g2_font_6x10_tf);

        // บรรทัด 1: ชื่อ + แหล่งคำสั่งที่คุมล้ออยู่
        oled.setCursor(0, 10);
        oled.print("DURIAN BOT   SRC:");
        oled.print(source_text(data.source));

        // บรรทัด 2: แรงดันแบต
        oled.setCursor(0, 23);
        oled.print("BAT: ");
        oled.print(data.batteryMillivolts / 1000.0f, 2);
        oled.print(" V");

        // บรรทัด 3: มอเตอร์
        oled.setCursor(0, 36);
        oled.print("M: ");
        oled.print(status_text(data.motor));

        // บรรทัด 4: แขน / ปั๊ม
        oled.setCursor(0, 49);
        oled.print("A: ");
        oled.print(status_text(data.arm));

        // บรรทัด 5: PWM ที่ส่งให้ L298N
        oled.setCursor(0, 62);
        oled.print("PWM: ");
        oled.print(data.pwm);
        oled.print("/255");
    } while (oled.nextPage());
}
