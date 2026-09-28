# Mega 2560 Motor Controller (Super Loop, ไม่มี RTOS)

ทำงานเหมือน [`motor_controller_simplify`](../motor_controller_simplify/README.md) ทุกอย่าง — ต่อขาเหมือนกัน
ใช้ protocol serial (`CMD:...`, ACK/ERR, telemetry `MS1`) และ CAN ID เดียวกัน backend จึงใช้ได้ทันที
ต่างกันที่ไม่ใช้ FreeRTOS แต่ให้ `loop()` เรียกงานทีละอย่างตาม `millis()` แบบ non-blocking

## Upload

- บอร์ด: `Arduino Mega or Mega 2560`
- ไลบรารี: **mcp_can** อย่างเดียว

```bash
arduino-cli lib install "mcp_can"
arduino-cli compile --fqbn arduino:avr:mega .
arduino-cli upload  --fqbn arduino:avr:mega -p /dev/ttyACM0 .
```

## การทำงานใน `loop()`

| ฟังก์ชัน | ทำอะไร |
| --- | --- |
| `readCan()` | อ่าน CAN จนหมด buffer เมื่อขา INT ของ MCP2515 แจ้ง (ISR ตั้ง flag) หรือทุก 5 ms ถ้าไม่ได้ต่อสาย INT |
| `readSerial()` | รับคำสั่งจาก Pi ทีละบรรทัด ตรวจรูปแบบ แล้วตอบ ACK/ERR |
| `updateControl()` | เช็ก timeout (CAN 300 ms, serial 1000 ms), เลือกแหล่งคำสั่ง (serial ก่อน CAN), ขับมอเตอร์, ส่งคำสั่งแขนจากเว็บเข้า CAN ทุก 50 ms |
| `sendTelemetry()` | อ่านแบต แล้วส่ง `MS1` ทุก 100 ms หรือทันทีเมื่อคำสั่งล้อเปลี่ยน |

## Battery

โมดูลวัดแรงดัน 0–25 V ต่อขา `S` เข้า A0 และ `−` เข้า GND ร่วม

```text
Vbat = analogRead(A0) × 5.0 / 1023 × 5
```

อ่านด้วย `analogRead()` แล้วเฉลี่ย 8 ค่าล่าสุด ถ้าค่าไม่ตรงกับมัลติมิเตอร์ให้แก้ `ADC_REFERENCE_VOLTAGE` ใน `battery_sensor.h`

## Watchdog

เมื่อไม่มี FreeRTOS แล้ว Watchdog Timer ว่าง จึงเปิดไว้ที่ 500 ms — ถ้า `loop()` ค้างเกินนั้นบอร์ดจะ reset
และ `setup()` หยุดมอเตอร์ก่อนทำอย่างอื่น (WDT เปิดหลัง init MCP2515 เสร็จ เพราะลูปรอ `CAN0.begin()` มี `delay(1000)`)

## ข้อจำกัด

- งานทุกอย่างอยู่ใน `loop()` เดียว ถ้าเพิ่มงานที่ใช้เวลานาน (เช่น `delay()`) จะทำให้ทุกอย่างช้าลงพร้อมกัน
- ยังไม่ได้ทดสอบบนบอร์ดจริง — compile ผ่านด้วย avr-gcc + Arduino core แล้ว (ใช้ header จำลองของ `mcp_can`)
