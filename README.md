# 🌳 Durian Bot

หุ่นยนต์ภาคสนามสำหรับ **ฉีดพ่นยา/สารในสวนทุเรียน** ผู้ควบคุมบังคับหุ่นผ่านเว็บแดชบอร์ดบนแท็บเล็ตหรือโน้ตบุ๊ก
ที่เชื่อมต่อ Wi-Fi Access Point ของ Raspberry Pi บนตัวหุ่นได้โดยตรง ไม่ต้องใช้อินเทอร์เน็ตหรือเราเตอร์ในสวน
หรือบังคับด้วยจอย PS2 ผ่าน CAN bus

> โปรเจกต์นี้ต่อยอดมาจาก **Rescue Robot / FireBot** เดิม บางไฟล์และเอกสารจึงยังใช้ชื่อเก่าอยู่
> (เช่น `docs/firebot-spec.md`, `firmware/flame_telemetry/`) ดูหัวข้อ [ส่วนที่มาจากโปรเจกต์เดิม](#-ส่วนที่มาจากโปรเจกต์เดิม)

## ✨ ความสามารถหลัก

- 🚜 **ขับเคลื่อน 4 ล้อ Mecanum** — เดินหน้า/ถอย/สไลด์ซ้าย-ขวา/เฉียง/หมุนตัว ด้วย Arduino Mega 2560 + L298N 2 ตัว
- 💧 **ระบบฉีดพ่น** — เปิด/ปิดปั๊มผ่าน relay และปรับมุมหัวฉีด 3 แกนด้วยเซอร์โว (PCA9685)
- 🔋 **วัดแรงดันแบตเตอรี่** แสดงบนหน้า `/control`
- 🎥 **กล้อง USB** สตรีมภาพสด (OpenCV → MJPEG) พร้อม **ตรวจจับทรงพุ่ม (canopy)** — วัดสัดส่วนใบสีเขียวในภาพ ขึ้น `READY` เมื่อหัวฉีดหันเข้าต้น
- 🎮 **ควบคุมได้ 2 ทาง** — เว็บ (ปุ่มกดค้างบนหน้า `/control` → FastAPI → USB serial) หรือจอย PS2 (CAN bus) โดยคำสั่งจากเว็บมีสิทธิ์ก่อน
- 🛑 **Fail-safe** — คำสั่งจากเว็บหมดอายุใน 1 วินาที, จากจอยหมดอายุใน 300 ms, ปุ่ม E-STOP ตัดทั้งล้อและปั๊ม

## 🏗️ ภาพรวมระบบ

```text
แท็บเล็ต / โน้ตบุ๊ก (เบราว์เซอร์)
        │  Wi-Fi AP ของ Pi (192.168.4.1)
        ▼
Raspberry Pi 5 (RAM 8 GB) ── Docker Compose
  ├─ frontend (nginx + SvelteKit)  :80   ── proxy /v1 ──┐
  └─ backend  (FastAPI + OpenCV)   :9000 ◄──────────────┘
        │ USB serial 115200            │ USB
        │  ↓ CMD:...   ↑ MS1           ▼
        ▼                            กล้อง USB
Arduino Mega 2560 (FreeRTOS) ── motor_controller_simplify
  ├─ L298N #1 ── มอเตอร์ล้อหน้าซ้าย / หน้าขวา
  ├─ L298N #2 ── มอเตอร์ล้อหลังซ้าย / หลังขวา
  ├─ A0 ◄── battery module
  └─ MCP2515 ══ CAN bus 500 kbps ══╦═ arm_controller (UNO): เซอร์โวหัวฉีด (PCA9685) + relay ปั๊ม
                                    ╚═ can-sender (UNO): จอย PS2
```

รายละเอียดเพิ่มเติม: [`docs/system-architecture.md`](docs/system-architecture.md) และโครงรายงาน [`docs/doc.md`](docs/doc.md)

## 🔧 ฮาร์ดแวร์

| อุปกรณ์ | จำนวน | หน้าที่ |
| --- | ---: | --- |
| Raspberry Pi 5 (RAM 8 GB) + adapter USB-C 5V 5A (27 W) | 1 | Wi-Fi AP, รัน backend/frontend ด้วย Docker, อ่านกล้อง |
| Arduino Mega 2560 | 1 | ขับมอเตอร์, วัดแบต, คุยกับ Pi ผ่าน USB และกับบอร์ดอื่นผ่าน CAN |
| Arduino Uno R3 | 2 | `arm_controller` (หัวฉีด + ปั๊ม), `can-sender` (จอย) |
| MCP2515 CAN module (คริสตัล 8 MHz) | 3 | สาย CAN ระหว่าง Arduino ทั้ง 3 บอร์ด (ใส่ jumper 120 Ω ที่ 2 บอร์ดปลายสาย) |
| **L298N dual H-bridge** | 2 | ขับมอเตอร์ DC ตัวละ 2 ล้อ (ดู [หัวข้อ L298N](#-motor-driver-l298n)) |
| DC gear motor + ล้อ Mecanum | 4 | ขับเคลื่อน |
| PCA9685 + เซอร์โว | 1 + 3 | ปรับมุมหัวฉีด |
| Relay 5V + ปั๊มน้ำ DC + หัวฉีด | 1 ชุด | ฉีดพ่น |
| Battery voltage sensor (0–25 V, 30k/7.5k) | 1 | วัดแรงดันแบตเข้า A0 ของ Mega |
| จอย PS2 ไร้สาย + receiver | 1 | บังคับในสวน |
| USB webcam | 1 | ภาพสด |

## ⚙️ Motor driver L298N

L298N เป็น dual H-bridge 1 บอร์ดขับมอเตอร์ DC ได้ 2 ตัว (ช่อง A และ B) ใช้ 2 บอร์ดสำหรับ 4 ล้อ

- **ทิศทาง** คุมด้วยขา `IN1/IN2` (ช่อง A) และ `IN3/IN4` (ช่อง B)
- **ความเร็ว** คุมด้วย PWM ที่ขา `ENA` / `ENB` — Mega ใช้ `analogWrite()` ที่ขา D5–D8 (Timer3/Timer4 ≈ 490 Hz)

| IN1 | IN2 | EN (PWM) | ผล |
| :-: | :-: | :-: | --- |
| HIGH | LOW | 0–255 | หมุนไปข้างหน้า ความเร็วตาม duty |
| LOW | HIGH | 0–255 | หมุนถอยหลัง |
| LOW | LOW | 0 | ปล่อยหมุนอิสระ (coast) — firmware ใช้ท่านี้ตอนหยุด |
| HIGH | HIGH | 255 | เบรก (ลัดวงจรขั้วมอเตอร์) — firmware ไม่ได้ใช้ |

### การต่อสาย (Mega 2560 ↔ L298N)

| L298N | ขา | Mega | ล้อ |
| --- | --- | --- | --- |
| #1 ช่อง A | ENA / IN1 / IN2 | D5 / D22 / D23 | M1 หน้าซ้าย |
| #1 ช่อง B | ENB / IN3 / IN4 | D6 / D24 / D25 | M2 หน้าขวา |
| #2 ช่อง A | ENA / IN1 / IN2 | D7 / D30 / D31 | M3 หลังซ้าย |
| #2 ช่อง B | ENB / IN3 / IN4 | D8 / D32 / D33 | M4 หลังขวา |
| ทั้ง 2 บอร์ด | `GND` | GND | **ต้องต่อ GND ร่วมกับ Mega** ไม่งั้นสัญญาณ IN/EN ไม่มีอ้างอิง |
| ทั้ง 2 บอร์ด | `12V` (Vs) | – | ขั้วบวกแบตมอเตอร์ |

ตำแหน่งล้ออนุมานจากตารางทิศทางใน firmware (SPIN_LEFT = M1, M3 ถอย / M2, M4 เดินหน้า)
ถ้าล้อไหนหมุนกลับทาง ให้สลับสาย `OUT1/OUT2` ของล้อนั้นที่ L298N ไม่ต้องแก้โค้ด

### ข้อควรระวัง

- **ถอด jumper ที่ ENA/ENB ออก** ก่อนต่อสาย PWM — ถ้าเสียบค้างไว้ ขา EN จะถูกดึงขึ้น 5V ตลอด มอเตอร์วิ่งเต็มสปีดเสมอ
- **Jumper 5V-EN (regulator บนบอร์ด):** เสียบไว้ได้เมื่อ Vs ≤ 12 V (บอร์ดจะจ่าย 5V ออกมาเอง) ถ้า Vs > 12 V ต้องถอดแล้วจ่าย 5V ให้ขา `5V` แยก
  ห้ามต่อขา 5V ของ L298N เข้าขา 5V ของ Mega ขณะที่ Mega เสียบ USB กับ Pi อยู่ (ไฟ 2 แหล่งจ่ายชนกัน) — ต่อแค่ GND ร่วมก็พอ
- **แรงดันตก ~2 V** เพราะ L298N เป็นทรานซิสเตอร์แบบ BJT — แบต 12 V มอเตอร์จะได้ ~10 V
  และที่ `MOTOR_PWM = 150` (duty ≈ 59%) แรงดันเฉลี่ยที่มอเตอร์ ≈ 10 × 0.59 ≈ **5.9 V**
- **กระแสสูงสุด 2 A ต่อช่อง** (peak 3 A) บอร์ดร้อนเร็วเมื่อมอเตอร์ติดขัด ควรมี heatsink และฟิวส์ที่สายแบต
- เดินสายไฟมอเตอร์แยกจากสาย CAN/USB และใส่ตัวเก็บประจุ 0.1 µF คร่อมขั้วมอเตอร์ ลด noise ที่ทำให้ serial/CAN เพี้ยน

## 📡 Protocol Raspberry Pi ↔ Mega (USB serial)

ตั้งค่า **115200 baud, 8N1** ข้อความเป็น ASCII 1 บรรทัดต่อ 1 frame จบด้วย `\n`

### Pi → Mega: คำสั่ง

```text
CMD:MOTOR:<0..10>        ขับล้อ (backend เปิดให้ใช้ 0..8)
CMD:ARM:<0..8,11..14>    ส่งต่อเข้า CAN 0x101 ไปหา arm_controller
CMD:ALL:0  หรือ  STOP    E-STOP: หยุดล้อ + ปิดปั๊ม + หยุดแขน
PING                     ตอบ {"t":"PONG"}
```

Mega ตอบกลับเป็น JSON (`{"t":"ACK",...}` / `{"t":"ERR",...}`) และคำสั่งจะหมดอายุใน 1 วินาทีถ้าไม่ส่งซ้ำ

### Mega → Pi: telemetry `MS1`

**`MS1` คือชื่อ (prefix) ของบรรทัดสถานะที่ Mega ส่งขึ้น Pi** ย่อมาจาก **M**otor controller **S**implify รุ่นที่ **1**
ตั้งชื่อแยกจาก `MC1` ซึ่งเป็นรูปแบบของ `motor_controller_mega` รุ่นเต็ม (20 field มี IR + encoder)
backend ดู prefix นี้เพื่อรู้ว่าจะแยก field อย่างไร และบรรทัดที่ไม่ขึ้นต้นด้วย prefix ที่รู้จักจะถูกทิ้ง
(กันขยะที่ bootloader พิมพ์ตอน reset) ส่งทุก 100 ms และส่งทันทีเมื่อคำสั่งเปลี่ยน

```text
MS1,<motor_code>,<motor_alive>,<arm_code>,<arm_alive>,<battery_mV>,<battery_adc>,<pwm>,<age_ms>,<seq>*<CK>
MS1,1,1,-1,0,12048,493,150,24,812*27
```

| # | Field | ตัวอย่าง | ความหมาย |
| ---: | --- | --- | --- |
| 1 | `motor_code` | `1` | คำสั่งล้อที่กำลังใช้ (ตารางรหัสด้านล่าง), `-1` = ไม่มีแหล่งคำสั่ง |
| 2 | `motor_alive` | `1` | มีคำสั่งล้อที่ยังไม่หมดอายุไหม (0/1) |
| 3 | `arm_code` | `-1` | คำสั่งแขนล่าสุด (จากเว็บ หรือที่เห็นจากจอยบน CAN) |
| 4 | `arm_alive` | `0` | มีคำสั่งแขนที่ยังไม่หมดอายุไหม (0/1) |
| 5 | `battery_mV` | `12048` | แรงดันแบต (mV) = 12.05 V |
| 6 | `battery_adc` | `493` | ค่า ADC ดิบ 0–1023 ที่ A0 (เฉลี่ย 8 ค่า) |
| 7 | `pwm` | `150` | duty ที่ส่งให้ ENA/ENB ของ L298N (0–255) |
| 8 | `age_ms` | `24` | อายุของคำสั่งล้อ (ms) |
| 9 | `seq` | `812` | ตัวนับ frame 0–65535 ใช้ดูว่า frame หาย |
| – | `*CK` | `*27` | XOR checksum ของทุก byte ก่อน `*` เป็น hex 2 หลัก — backend ทิ้งบรรทัดที่ไม่ตรง |

ตรวจ checksum เองได้ด้วย:

```python
payload = "MS1,1,1,-1,0,12048,493,150,24,812"
ck = 0
for b in payload.encode():
    ck ^= b
print(f"{payload}*{ck:02X}")   # MS1,1,1,-1,0,12048,493,150,24,812*27
```

ดูข้อความจริงจากบอร์ด: `python -m serial.tools.miniterm /dev/ttyACM0 115200` (ปิด backend ก่อน เพราะพอร์ตเปิดได้ทีละโปรแกรม)

## 📁 โครงสร้างโปรเจกต์

| โฟลเดอร์ | รายละเอียด |
| --- | --- |
| [`backend/`](backend/README.md) | FastAPI — telemetry hub, WebSocket, สั่งงานหุ่น, กล้อง |
| [`frontend/`](frontend/README.md) | SvelteKit 2 / Svelte 5 + Tailwind v4 — operator console (`/`, `/control`, `/monitor`) |
| [`firmware/`](firmware/README.md) | Arduino sketch ของแต่ละบอร์ด (ดูตารางด้านล่าง) |
| `scripts/` | สคริปต์ deploy ไป Pi, ตั้งค่า Wi-Fi AP, อ่าน/debug CAN bus |
| `docs/` | เอกสารสถาปัตยกรรม, การ deploy และโครงรายงาน (`doc.md`) |
| `docker-compose*.yml` | stack หลัก + overlay สำหรับ Pi AP, serial และกล้อง |

### Firmware

| Sketch | บอร์ด | หน้าที่ |
| --- | --- | --- |
| [`motor_controller_simplify/`](firmware/motor_controller_simplify/README.md) | Mega 2560 | **ที่ใช้อยู่ตอนนี้** (FreeRTOS) — ขับล้อผ่าน L298N จากคำสั่ง CAN (จอย) หรือ USB serial (เว็บ), ส่งต่อคำสั่งแขนเข้า CAN, วัดแบต, ส่ง telemetry `MS1` |
| [`motor_controller_superloop/`](firmware/motor_controller_superloop/README.md) | Mega 2560 | ทางเลือกแบบไม่มี RTOS (super loop + `millis()`) — ต่อขาและ protocol เหมือน `motor_controller_simplify`, วัดแบตด้วย `analogRead()`, เปิด Watchdog 500 ms |
| `arm_controller/` | UNO | รับคำสั่งแขนจาก CAN — เซอร์โวหัวฉีด (PCA9685) และ relay ปั๊ม, มี Watchdog (Interrupt + Reset) / INT0 / Power-down sleep / EEPROM สถิติ |
| `can-sender/` | UNO | อ่านจอย PS2 แล้วส่งคำสั่งเข้า CAN bus ทุก 50 ms |
| [`motor_controller_mega/`](firmware/motor_controller_mega/README.md) | Mega 2560 | รุ่นเต็ม — มอเตอร์ + PID, encoder, battery, IR 4 ทิศ, telemetry `MC1` |
| [`can_receiver/`](firmware/can_receiver/README.md), `receiver-canbus/` | UNO | bridge CAN ↔ USB serial รุ่นก่อน (protocol `RB2`–`RB4`) |
| `motor_controller/`, `motor_controller_rtos*/` | Mega 2560 | มอเตอร์คอนโทรลเลอร์รุ่นก่อนหน้า |
| `can_bus_debug/` | UNO | ใช้ทดสอบ CAN bus |

### รหัสคำสั่ง (ใช้ร่วมกันทุกบอร์ด)

| Code | คำสั่ง | Code | คำสั่ง |
| ---: | --- | ---: | --- |
| 0 | STOP | 8 | BACKWARD_RIGHT |
| 1 | FORWARD | 9 | SPIN_LEFT |
| 2 | BACKWARD | 10 | SPIN_RIGHT |
| 3 | LEFT | 11 | PUMP_ON |
| 4 | RIGHT | 12 | PUMP_OFF |
| 5 | FORWARD_LEFT | 13 | HEAD_UP |
| 6 | FORWARD_RIGHT | 14 | HEAD_DOWN |
| 7 | BACKWARD_LEFT | | |

Backend รับคำสั่งที่ `POST /v1/robot/command` ด้วย body `{"channel": "motor" | "arm" | "all", "code": 0..14}`
แล้วแปลงเป็นบรรทัด `CMD:<CHANNEL>:<code>` ส่งลง serial (channel `all` รับเฉพาะ code `0` = หยุดทุกอย่าง)

## 🚀 เริ่มต้นใช้งาน

### รันบนเครื่องพัฒนา (ไม่ต้องมีฮาร์ดแวร์)

ค่า default ของ backend คือ `TELEMETRY_SOURCE=mock` ซึ่งสร้างข้อมูลจำลองให้แดชบอร์ดใช้งานได้ทันที

```bash
# backend — http://localhost:9000/docs
cd backend
cp .env.sample .env
poetry install
./scripts/run-dev

# frontend — http://localhost:5173
cd frontend
pnpm install
pnpm dev
```

### Deploy บน Raspberry Pi 5

ใช้ **Raspberry Pi OS 64-bit (Bookworm)** — Pi 5 รองรับเฉพาะ Bookworm ขึ้นไป และสคริปต์ตั้ง Wi-Fi AP ใช้ NetworkManager

```bash
sudo ./scripts/setup-rpi-ap.sh --no-compose   # ตั้ง Wi-Fi AP (192.168.4.1)
cp .env.example .env
# ตั้ง SECRET_KEY:  python -c "import secrets; print(secrets.token_urlsafe(48))"
docker compose up -d --build
```

`.env.example` ตั้ง `COMPOSE_FILE` ให้รวม overlay ของ Pi AP, Arduino serial และกล้อง USB ไว้แล้ว
จากนั้นเชื่อม Wi-Fi ของหุ่นแล้วเปิด `http://192.168.4.1/` บนแท็บเล็ต (API docs อยู่ที่ `/docs`)

Pi 5 RAM 8 GB build image บนตัวเองได้เลย ไม่ต้อง cross-build แล้ว
(ถ้าอยาก build บนโน้ตบุ๊กเพื่อความเร็ว ยังใช้ `PI_HOST=user@192.168.4.1 ./scripts/deploy-to-pi.sh` ได้)

ดูขั้นตอนเต็มและการแก้ปัญหาได้ที่ [`docs/docker-deployment.md`](docs/docker-deployment.md)

### Upload firmware

เปิดโฟลเดอร์ sketch ใน Arduino IDE (ชื่อ `.ino` ต้องตรงกับชื่อโฟลเดอร์) แล้วเลือกบอร์ดให้ตรง หรือใช้ `arduino-cli`:

```bash
arduino-cli lib install "FreeRTOS" "mcp_can"
cd firmware/motor_controller_simplify
arduino-cli compile --fqbn arduino:avr:mega .
arduino-cli upload  --fqbn arduino:avr:mega -p /dev/ttyACM0 .
```

ปิด backend (`docker compose stop backend`) ก่อน upload เพราะ backend จับพอร์ต `/dev/ttyACM0` อยู่
Pin map และ protocol ของแต่ละบอร์ดอยู่ใน README ของ sketch นั้นๆ

## 🧪 การทดสอบ

```bash
cd backend && poetry run pytest
cd frontend && pnpm check && pnpm lint
```

## 🗂️ ส่วนที่มาจากโปรเจกต์เดิม

ไฟล์ต่อไปนี้เป็นของ Rescue Robot / FireBot และเก็บไว้เพื่ออ้างอิง ยังไม่ได้ใช้ในงานฉีดพ่นสวนทุเรียน:

- `firmware/flame_telemetry/`, `firmware/Robot_main/` และ protocol `FB1` (`TELEMETRY_SOURCE=serial`)
- `docs/firebot-spec.md` และส่วน rescue/flame ใน `docs/system-architecture.md`
- โมดูลตัวอย่าง `hospital` และ `pet` ใน backend
