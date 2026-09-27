# บันทึกสำหรับทีม (ไม่ต้องใส่ในรายงาน)

ไฟล์นี้แยกออกมาจาก `docs/doc.md` เพื่อให้ `doc.md` เป็นเนื้อหารายงานล้วน

## วิธีใช้ doc.md

- ข้อความใน `[วงเล็บเหลี่ยม]` คือส่วนที่ต้องเติมเอง (ชื่อ, รูปถ่าย, ผลทดลอง, ราคา)
- รูปแผนภาพอยู่ใน `docs/images/` (PNG พื้นขาว เส้นดำ) และต้นฉบับ Mermaid อยู่ใน `docs/diagrams/*.mmd`
  ถ้าแก้แผนภาพ ให้แก้ไฟล์ `.mmd` แล้ว render ใหม่ เช่น
  `npx -y @mermaid-js/mermaid-cli -i docs/diagrams/<ชื่อ>.mmd -o docs/images/<ชื่อ>.png -b white -s 3`
- โค้ดในรายงานเป็นส่วนสำคัญที่ตัดมา ไม่ใช่ทั้งไฟล์
- เลขรูป/ตารางใช้รูปแบบ "รูปที่ บท.ลำดับ" และ "ตารางที่ บท.ลำดับ" (คำบรรยายตารางอยู่เหนือตาราง คำบรรยายรูปอยู่ใต้รูป)

## Z.0 ตารางความครอบคลุมเนื้อหาวิชา (เทียบกับ 4 บอร์ดหลัก)

| หัวข้อ | can-sender | arm_controller | can_receiver | motor_simplify | สถานะ |
| --- | :-: | :-: | :-: | :-: | --- |
| GPIO | ✅ | ✅ register | ✅ | ✅ | ครบ |
| PWM | – | ✅ PCA9685 | ✅ PCA9685 | ✅ analogWrite | ครบ (อาจเพิ่มแบบ register) |
| ADC | – | – | – | ✅ register (แบต) | ครบ |
| Interrupt | – | ✅ INT0 register | – (polling) | ✅ INT4 → semaphore | มี 2 บอร์ด |
| Watchdog | – | ✅ register (System Reset) | – | – (FreeRTOS ใช้ WDT เป็น tick) | มี 1 บอร์ด (ยังไม่มีโหมด Interrupt) |
| Sleep mode | – | ⚠️ software | ⚠️ software | – | ยังไม่มี sleep mode ของ AVR จริง (Z.3) |
| UART | debug | debug | ✅ protocol 2 ทาง | ✅ protocol 2 ทาง | ครบ |
| SPI | ✅ bit-bang + MCP2515 | ✅ | ✅ | ✅ | ครบ |
| I2C | – | ✅ driver เอง (PCA9685) | ✅ | – (ตัดจอ OLED ออกแล้ว) | ครบ |
| RTOS | – | – | – | ✅ FreeRTOS 5 task | ครบ |
| EEPROM | – | ❌ | – | – | ยังไม่มีในบอร์ดหลัก (Z.4) |
| OpenCV | backend | | | | ✅ HSV mask + morphology + contour (ทรงพุ่ม) |

## Z.1 ✅ แก้แล้ว: Mega `motor_controller_simplify` ↔ backend

- เดิม firmware ส่ง `MC1,...,<CK>` (7 field, ไม่มี `*`) ซึ่ง parser ของ backend ปฏิเสธทุกบรรทัด และ Mega ไม่อ่านคำสั่งจาก Serial
- ตอนนี้ firmware ส่ง `MS1,motor,motor_alive,arm,arm_alive,battery_mV,battery_adc,pwm,age_ms,seq*CK` และ backend รู้จัก `MS1` แล้ว
  (`receiver_canbus.py`, schema ของ robot ทั้ง backend และ frontend, มี unit test)
- Mega รับ `CMD:MOTOR` / `CMD:ARM` / `CMD:ALL:0` / `STOP` / `PING` ผ่าน Serial, `CMD:ARM` ส่งต่อเข้า CAN `0x101`
- **ยังต้องทดสอบกับบอร์ดจริง** (ยังไม่ได้ compile ด้วย Arduino IDE — ตรวจแค่ syntax ด้วย g++)
- ข้อจำกัด: ถ้าเปิดจอยไว้ด้วย จอยส่ง `0x101` ตรงถึงบอร์ดแขน Mega กันไม่ได้ → คำสั่งแขนจากเว็บกับจอยจะสลับกัน
  (ล้อไม่มีปัญหาเพราะ Mega เป็นคนตัดสินเอง) — ถ้าจะแก้ ให้บอร์ดแขนเช็กแหล่งคำสั่งเหมือนหัวข้อ 6.5.4

✅ หน้า `/control` มีปุ่มกดค้างเพื่อสั่งแล้ว (หัวข้อ 6.6.7) และ backend เปิดรหัส SPIN 9/10 แล้ว

## Z.2 ✅ ADC — ทำแล้ว (แบตเตอรี่)

- Mega อ่านแบตที่ `A0` แบบ register + moving average แล้วส่งใน `MS1` → หน้า `/control` แสดงแรงดันแล้ว
- **ต้องทำเอง:** ต่อโมดูลวัดแรงดันเข้า A0 + GND ร่วม, วัดแบตจริงด้วยมัลติมิเตอร์เทียบกับค่าบนเว็บ ถ้าคลาดเคลื่อนให้แก้
  `ADC_REFERENCE_VOLTAGE` (วัดขา 5V ของ Mega จริง มักได้ 4.8–5.0 V) หรือค่าตัวต้านทาน
- ต่อยอดได้: เซนเซอร์ระดับน้ำ analog วัดสารในถัง (ใช้ `adc_read()` ใน `battery_sensor.cpp` ช่องอื่นได้เลย)

## Z.3 ⚠️ Sleep Mode — ตอนนี้เป็น software sleep (ยังไม่ได้ทำ Power-down)

- โครงร่างรุ่นก่อนเขียนถึง Power-down ที่ปลุกด้วย INT0 และ Watchdog แบบ Interrupt + Reset แต่ **ยังไม่มีใน `arm_controller.ino`**
  ตอนนี้เนื้อหาในรายงาน (หัวข้อ 6.3) เขียนตามโค้ดจริงแล้ว ถ้าจะทำเพิ่ม ใช้แบบร่างนี้:

```cpp
void enter_power_down() {
    Serial.flush();                            // ส่งข้อความค้างให้หมดก่อนหลับ
    wdt_disable();                             // ไม่งั้น WDT จะ reset ระหว่างหลับ
    ADCSRA &= ~(1 << ADEN);                    // ไม่ใช้ ADC ปิดลดกระแส

    cli();
    EICRA &= ~((1 << ISC01) | (1 << ISC00));   // INT0 = low level (edge ปลุกจาก Power-down ไม่ได้)
    EIFR = (1 << INTF0);
    EIMSK |= (1 << INT0);
    asleep = true;
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);       // ต้อง #include <avr/sleep.h>
    sleep_enable();
    sleep_bod_disable();                       // ปิด brown-out detector ระหว่างหลับ
    sei();
    sleep_cpu();                               // คำสั่งถัดจาก sei ทำก่อน interrupt เสมอ จึงไม่พลาดการปลุก

    sleep_disable();                           // --- ตื่นแล้ว ---
    cli();
    asleep = false;
    EICRA = (EICRA & ~((1 << ISC00) | (1 << ISC01))) | (1 << ISC01);  // กลับเป็นขอบขาลง
    EIFR = (1 << INTF0);
    EIMSK |= (1 << INT0);
    sei();
    // เปิด WDT กลับ
}

ISR(INT0_vect) {
    isDataReady = true;
    if (asleep) EIMSK &= ~(1 << INT0);         // low level จะเข้า ISR ซ้ำตราบใดที่ขายัง LOW จึงปิดไว้ก่อน
}
```

- ถ้าข้อความที่ปลุกเป็นแค่ STOP heartbeat ให้อ่านทิ้งแล้วหลับต่อ, ช่วงหลับ `millis()` หยุดนับ (Timer0 หยุด)
- ทำแล้วต้องวัดกระแสก่อน/หลังหลับด้วยมัลติมิเตอร์ใส่ในบทที่ 7
- ต่อยอดได้: ตั้ง mask/filter ของ MCP2515 ให้รับเฉพาะ ID `0x101` → ไม่ต้องตื่นเพราะข้อความ `0x100` ของล้อ

## Z.4 ❌ EEPROM — ยังไม่มีในบอร์ดหลัก

- มีแค่ใน `firmware/motor_controller_rtos_pid/pid_config.cpp` (รุ่นทดลอง ไม่ได้ใช้ในระบบหลัก) — เก็บค่า PID + magic `0x5049` + version
- แบบร่างสำหรับ `arm_controller` (สถิติที่จำข้ามการปิดเครื่อง):

```cpp
struct ArmStats {
    uint16_t magic;           // 0xA7A7 = เคยบันทึกแล้ว (EEPROM ใหม่อ่านได้ 0xFF)
    uint8_t  version;
    uint16_t watchdogTrips;   // จำนวนครั้งที่ WDT ทำงาน (ต้องใช้ WDT โหมด Interrupt + Reset)
    uint16_t bootCount;
    uint32_t pumpSeconds;     // เวลาเปิดปั๊มสะสม
};
// load: EEPROM.get → ถ้า magic/version ไม่ตรง ให้เริ่มนับใหม่
// save: EEPROM.put เฉพาะตอนบูต / ปิดปั๊ม / WDT (เขียนเฉพาะ byte ที่เปลี่ยน ยืดอายุ ~100,000 ครั้ง/ช่อง)
```

- Watchdog โหมด Interrupt + Reset: ตั้ง `WDTCSR = (1 << WDIE) | (1 << WDE) | (1 << WDP2) | (1 << WDP0)`,
  ใน `ISR(WDT_vect)` ปิดปั๊มทันทีและ `watchdogTrips++` ลง EEPROM, ใน `loop()` ต้อง `WDTCSR |= (1 << WDIE)` ทุกรอบ
  (hardware ล้าง `WDIE` เองหลังเข้า ISR)
- ทำแล้วเพิ่มกรณีทดสอบ: เปิดปั๊ม 10 s แล้วถอดไฟ เสียบใหม่ → `boots` +1, `pump_seconds` +10

## Z.4.1 ⚠️ Fail-safe ปั๊มเมื่อ CAN ขาด

- ตอนนี้ CAN ขาด > 1 s แค่หยุดแขน ปั๊มยังเปิดจนครบ 30 s ของ sleep — ควรเพิ่ม `digitalWrite(RELAY_PUMP_PIN, RELAY_OFF_STATE)` ใน fail-safe

## Z.5 ✅ RTOS — ทำแล้ว

- `motor_controller_simplify` เป็น FreeRTOS แล้ว (หัวข้อ 6.5) ต้องติดตั้งไลบรารี **FreeRTOS** (feilipu) ใน Arduino IDE ก่อน compile
- **ต้องทดสอบบนบอร์ดจริง:** ดูว่า stack ไม่ล้น (ถ้าล้น ไลบรารีจะกระพริบ LED ขา 13 ค้าง) และลองถอด/ต่อสาย INT ว่ายังรับ CAN ได้ทั้ง 2 แบบ

## Z.6 ✅ OpenCV — ทำแล้ว (ตรวจทรงพุ่ม, หัวข้อ 6.6.5)

- **ต้องปรับค่าในสวนจริง:** ช่วงสี `GREEN_LOWER` / `GREEN_UPPER` และ `CANOPY_READY_RATIO` ขึ้นกับแสงและกล้อง
  ลองถ่ายภาพในสวนแล้วปรับจน READY ขึ้นตอนหันเข้าต้นเท่านั้น
- ต่อยอดได้: ตรวจลำต้น/แนวแถวด้วย `Canny` + `HoughLinesP`, หรือ spot spraying อัตโนมัติ (ต้องมี E-STOP และโหมด manual override)

## Z.7 เติมเล็กๆ ที่ได้คะแนนง่าย

- **Watchdog ในบอร์ดอื่น:** เพิ่ม `wdt_enable(WDTO_500MS)` + `wdt_reset()` ใน `can_receiver`
  — ระวัง `while (CAN0.begin(...) != CAN_OK) delay(1000);` ใน setup ต้องเปิด WDT หลังบรรทัดนี้ หรือใส่ `wdt_reset()` ในลูปรอ
  (Mega ใช้ไม่ได้แล้ว เพราะ FreeRTOS ใช้ WDT เป็น tick — ถ้าต้องการกันค้างให้ทำ software watchdog: task CONTROL ต้อง "เช็กอิน" ทุกรอบ
  ไม่งั้น TELEMETRY หยุดมอเตอร์)
- **Interrupt ใน can_receiver:** ตอนนี้ poll `checkReceive()` — ต่อขา INT ของ MCP2515 แล้วใช้รูปแบบเดียวกับ arm_controller
- **Timer interrupt:** ส่ง telemetry ทุก 100 ms ด้วย Timer1 CTC (`TCCR1B`, `OCR1A`, `TIMSK1`) ตั้ง flag ใน ISR แทน `millis()`
  → ได้หัวข้อ Timer/Counter เพิ่ม (คำนวณ: 16 MHz / 1024 / 10 Hz − 1 = `OCR1A = 1562`)
- **Buzzer เตือน:** ปั๊มเปิด / แบตต่ำ / CAN หลุด → ใช้ `tone()` หรือ PWM Timer
