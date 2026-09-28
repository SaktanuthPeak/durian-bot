# Mega 2560 Motor Controller (FreeRTOS)

บอร์ดที่เสียบ USB กับ Raspberry Pi — ขับล้อ 4 ล้อตามคำสั่งจากจอย (CAN) หรือเว็บ (USB serial),
ส่งต่อคำสั่งแขน/ปั๊มจากเว็บเข้า CAN ไปหา `arm_controller` และส่ง telemetry `MS1` พร้อมแรงดันแบตเตอรี่

```text
Raspberry Pi / FastAPI ── USB Serial 115200 ── Mega 2560 (FreeRTOS)
                                               ├─ L298N ×2 ── มอเตอร์ Mecanum 4 ล้อ (analogWrite → ENA/ENB)
                                               ├─ battery module (A0, ADC)
                                               └─ MCP2515 ── CAN 500 kbps ─┬─ can-sender (จอย PS2)
                                                                           └─ arm_controller
```

## Upload

- บอร์ด: `Arduino Mega or Mega 2560`
- ไลบรารี: **Arduino_FreeRTOS_Library** (feilipu), และ **mcp_can** — ติดตั้งผ่าน Library Manager

```bash
arduino-cli lib install "FreeRTOS" "mcp_can"
arduino-cli compile --fqbn arduino:avr:mega .
arduino-cli upload  --fqbn arduino:avr:mega -p /dev/ttyACM0 .
```

## Pin map

| อุปกรณ์ | Mega 2560 |
| --- | --- |
| L298N #1 ENA / IN1 / IN2 → M1 หน้าซ้าย | D5 / D22 / D23 |
| L298N #1 ENB / IN3 / IN4 → M2 หน้าขวา | D6 / D24 / D25 |
| L298N #2 ENA / IN1 / IN2 → M3 หลังซ้าย | D7 / D30 / D31 |
| L298N #2 ENB / IN3 / IN4 → M4 หลังขวา | D8 / D32 / D33 |
| L298N GND (ทั้ง 2 บอร์ด) | GND ร่วม |
| MCP2515 CS / SO / SI / SCK | D53 / D50 / D51 / D52 |
| MCP2515 INT (ไม่บังคับ) | D2 (INT4) |
| Battery module `S` (หรือ `+` ของตัวแบ่งแรงดัน) | A0 |
| Battery module `−` | GND ร่วมกับ Mega |

ถอด jumper ที่ ENA/ENB ของ L298N ก่อนต่อสาย PWM และดูข้อควรระวังเรื่องไฟเลี้ยงใน [README หลัก](../../README.md#-motor-driver-l298n)

ถ้ายังไม่ได้ต่อสาย INT ก็ใช้งานได้ — task `CAN_RX` จะ poll ทุก tick (~15 ms) แทน
ต่อ INT แล้วจะตื่นทันทีที่มีข้อความเข้า

### Battery

ใช้โมดูลวัดแรงดัน 0–25 V (ตัวแบ่ง 30 kΩ / 7.5 kΩ ลดแรงดันลง 5 เท่า) ต่อขา `S` เข้า A0, `−` เข้า GND ร่วมกับ Mega

```text
Vbat = analogRead(A0) × 5.0 / 1023 × 5
```

อ่านด้วย `analogRead()` แล้วเฉลี่ย 8 ค่าล่าสุด (moving average) เพื่อลด noise จากมอเตอร์
ถ้าค่าบนเว็บไม่ตรงกับมัลติมิเตอร์ ให้วัดขา 5V ของ Mega แล้วแก้ `ADC_REFERENCE_VOLTAGE` ใน `battery_sensor.h`

### ไฟล์

| ไฟล์ | หน้าที่ |
| --- | --- |
| `motor_controller_simplify.ino` | task ทั้งหมด, protocol, ขับมอเตอร์ |
| `battery_sensor.h/.cpp` | อ่านแบตด้วย `analogRead()` + moving average |

## Tasks

| Task | Priority | ทำอะไร |
| --- | ---: | --- |
| `CAN_RX` | 3 | รอ semaphore จาก ISR ขา INT (หรือ timeout 1 tick) แล้วอ่านทุกข้อความ CAN ส่งเข้า `commandQueue` |
| `CONTROL` | 2 | เจ้าของ state คำสั่งแต่ผู้เดียว — ดึงจาก queue, เลือกแหล่งคำสั่ง (serial ก่อน CAN), เช็ก timeout, ขับมอเตอร์ |
| `SERIAL_RX` | 2 | อ่านบรรทัดคำสั่งจาก Pi ส่งเข้า `commandQueue` แล้วตอบ ACK/ERR |
| `TELEMETRY` | 1 | อ่านแบต แล้วส่ง `MS1` ทุก 100 ms หรือทันทีเมื่อ `CONTROL` แจ้งผ่าน task notification |

| RTOS object | ใช้ทำอะไร |
| --- | --- |
| `commandQueue` (8 ช่อง) | ส่งคำสั่งจาก `CAN_RX`/`SERIAL_RX` ไป `CONTROL` — ไม่มีตัวแปร global ที่หลาย task เขียนพร้อมกัน |
| `canRxSemaphore` (binary) | ISR ขา INT ปลุก `CAN_RX` |
| `canBusMutex` | MCP2515 ใช้ SPI ร่วมกันระหว่าง `CAN_RX` (อ่าน) และ `CONTROL` (ส่ง) |
| `serialTxMutex` | กันบรรทัด telemetry กับ ACK พิมพ์ปนกัน |
| `stateMutex` | snapshot สถานะที่ `CONTROL` เขียนและ `TELEMETRY` อ่าน |

FreeRTOS บน AVR ใช้ Watchdog Timer เป็นตัวสร้าง tick จึงใช้ WDT reset ในบอร์ดนี้ไม่ได้
(ตัวอย่าง WDT reset อยู่ที่ `arm_controller`)

## Protocol

คำสั่ง Pi → Mega (จบด้วย `\n`):

```text
CMD:MOTOR:<0..10>        ขับล้อ
CMD:ARM:<0..8,11..14>    ส่งต่อเข้า CAN 0x101 ซ้ำทุก 50 ms จนคำสั่งหมดอายุ
CMD:ALL:0  หรือ  STOP    หยุดล้อ + PUMP_OFF + หยุดแขน
PING                     ตอบ {"t":"PONG"}
```

คำสั่งจาก serial มีสิทธิ์เหนือจอยและหมดอายุใน 1000 ms ถ้าไม่ส่งซ้ำ ส่วนคำสั่ง CAN หมดอายุใน 300 ms

Telemetry Mega → Pi:

```text
MS1,<motor_code>,<motor_alive>,<arm_code>,<arm_alive>,<battery_mV>,<battery_adc>,<pwm>,<age_ms>,<seq>*<CK>
MS1,1,1,-1,0,12048,493,150,24,812*27
```

`CK` = XOR ของทุก byte ก่อน `*` เป็นเลขฐาน 16 สองหลัก, รหัสเป็น `-1` เมื่อไม่มีแหล่งคำสั่ง

## ข้อจำกัด

- จอยส่ง CAN `0x101` ตรงถึง `arm_controller` ถ้าเปิดจอยไว้พร้อมกับสั่งแขนจากเว็บ คำสั่งแขนจะสลับกัน
  (ล้อไม่มีปัญหาเพราะ Mega เลือกแหล่งเอง)
- ยังไม่ได้ทดสอบบนบอร์ดจริง — ตรวจแล้วแค่ syntax ด้วย g++ กับ header จำลอง
