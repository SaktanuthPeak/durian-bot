/*
 * FreeRTOS CAN motor controller for Arduino Mega 2560, with a USB-Serial link
 * to the Raspberry Pi (FastAPI) at 115200 baud, 8N1, one ASCII line per frame.
 *
 * Required libraries: Arduino_FreeRTOS_Library (feilipu), mcp_can.
 *
 * Tasks (higher number = higher priority):
 *   CAN_RX     3  woken by the MCP2515 INT pin (ISR -> semaphore), or every tick
 *                 when INT is not wired; pushes joystick commands into the queue
 *   CONTROL    2  sole owner of the command state: drains the queue, picks the
 *                 command source, runs the timeouts, drives the motors
 *   SERIAL_RX  2  reads lines from the Pi and pushes commands into the queue
 *   TELEMETRY  1  reads the battery ADC and prints MS1 every 100 ms, or at once
 *                 when CONTROL notifies a change
 *
 * Pi -> Mega commands (terminated by LF, CR ignored):
 *   CMD:MOTOR:<0..10>          drive the wheels directly
 *   CMD:ARM:<0..8,11..14>      forwarded to the arm controller on CAN ID 0x101
 *   CMD:ALL:0 | STOP           stop wheels, pump off, arm stop
 *   PING                       replies {"t":"PONG"}
 * Replies are JSON lines ({"t":"ACK",...} / {"t":"ERR",...}), which the backend
 * ignores because they do not start with a telemetry prefix.
 *
 * A serial command overrides the joystick's CAN commands and expires after
 * 1000 ms unless repeated, so the robot stops if the Pi or USB link dies.
 *
 * Mega -> Pi telemetry:
 *   MS1,<motor_code>,<motor_alive>,<arm_code>,<arm_alive>,<battery_mV>,<battery_adc>,<pwm>,<age_ms>,<seq>*<CK>
 * Codes are -1 when no source is live. CK is a 2-digit hex XOR of every byte
 * before '*'. seq wraps at 65535. age_ms is the age of the command driving the
 * wheels (0 when idle).
 * Example: MS1,1,1,-1,0,12048,493,150,24,812*27
 *
 * The FreeRTOS AVR port uses the watchdog timer as its tick source (~15 ms), so
 * this sketch cannot also use the watchdog as a reset timer.
 */

// Arduino_FreeRTOS.h must be included before the other FreeRTOS headers.
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

#include <SPI.h>
#include <mcp_can.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battery_sensor.h"

constexpr uint8_t M1_PWM = 5, M1_IN1 = 22, M1_IN2 = 23;
constexpr uint8_t M2_PWM = 6, M2_IN1 = 24, M2_IN2 = 25;
constexpr uint8_t M3_PWM = 7, M3_IN1 = 30, M3_IN2 = 31;
constexpr uint8_t M4_PWM = 8, M4_IN1 = 32, M4_IN2 = 33;
constexpr uint8_t CAN_CS_PIN = 53;
constexpr uint8_t CAN_INT_PIN = 2;          // INT4 on the Mega; optional, see CAN_RX
constexpr unsigned long CAN_ID_MOTOR = 0x100;
constexpr unsigned long CAN_ID_ARM = 0x101;
constexpr unsigned long CAN_TIMEOUT = 300;
constexpr unsigned long SERIAL_COMMAND_TIMEOUT_MS = 1000;
constexpr unsigned long SERIAL_CAN_TX_INTERVAL_MS = 50;
#define CAN_CLOCK_SET MCP_8MHZ
constexpr uint8_t MOTOR_PWM = 150;

static const TickType_t CONTROL_PERIOD = pdMS_TO_TICKS(20);
static const TickType_t TELEMETRY_PERIOD = pdMS_TO_TICKS(100);

// Stack depth is in bytes on the AVR port. snprintf needs the most.
constexpr uint16_t CAN_TASK_STACK = 256;
constexpr uint16_t CONTROL_TASK_STACK = 256;
constexpr uint16_t SERIAL_TASK_STACK = 320;
constexpr uint16_t TELEMETRY_TASK_STACK = 448;
constexpr UBaseType_t COMMAND_QUEUE_LENGTH = 8;

MCP_CAN CAN0(CAN_CS_PIN);

enum Command : uint8_t {
  STOP = 0, FORWARD, BACKWARD, LEFT, RIGHT, FORWARD_LEFT,
  FORWARD_RIGHT, BACKWARD_LEFT, BACKWARD_RIGHT, SPIN_LEFT, SPIN_RIGHT,
  PUMP_ON, PUMP_OFF, HEAD_UP, HEAD_DOWN
};

enum Source : uint8_t { FROM_CAN, FROM_SERIAL };
enum Channel : uint8_t { CH_MOTOR, CH_ARM, CH_ALL_STOP };

// One command from either input task. code is -1 for an invalid CAN frame,
// which marks that joystick channel dead.
struct CommandEvent {
  Source source;
  Channel channel;
  int8_t code;
};

// Shared status under stateMutex: CONTROL writes the command fields,
// TELEMETRY writes the battery and reads everything.
struct ControlSnapshot {
  int8_t motor;
  int8_t arm;
  uint8_t pwm;
  unsigned long commandTime;  // millis() of the command driving the wheels
  BatterySample battery;
};

// Private to the CONTROL task; no other task touches it. Defined up here so
// the Arduino-generated prototypes that take ControlState& can see it.
struct ControlState {
  int8_t canMotor = -1;
  unsigned long canMotorTime = 0;
  int8_t canArm = -1;
  unsigned long canArmTime = 0;

  int8_t serialMotor = STOP;
  bool serialMotorActive = false;
  unsigned long serialMotorTime = 0;
  int8_t serialArm = STOP;
  bool serialArmActive = false;
  unsigned long serialArmTime = 0;
  unsigned long armTxTime = 0;

  int8_t appliedMotor = -1;
};

struct MotorPins { uint8_t pwm, in1, in2; };
const MotorPins motors[] = {
  {M1_PWM, M1_IN1, M1_IN2}, {M2_PWM, M2_IN1, M2_IN2},
  {M3_PWM, M3_IN1, M3_IN2}, {M4_PWM, M4_IN1, M4_IN2}
};

static QueueHandle_t commandQueue = nullptr;
static SemaphoreHandle_t canRxSemaphore = nullptr;  // given by the INT ISR
static SemaphoreHandle_t canBusMutex = nullptr;     // MCP2515 SPI access
static SemaphoreHandle_t serialTxMutex = nullptr;   // whole lines on Serial
static SemaphoreHandle_t stateMutex = nullptr;      // ControlSnapshot
static TaskHandle_t telemetryTask = nullptr;
static ControlSnapshot snapshot = {-1, -1, 0, 0, {0, 0}};

bool validMotorCommand(long code) { return code >= STOP && code <= SPIN_RIGHT; }
bool validArmCommand(long code) {
  return (code >= STOP && code <= BACKWARD_RIGHT) || (code >= PUMP_ON && code <= HEAD_DOWN);
}

// ----------------------------------------------------------------------------
// Motors (CONTROL task only)
// ----------------------------------------------------------------------------
void stopMotors() {
  for (uint8_t i = 0; i < 4; ++i) {
    analogWrite(motors[i].pwm, 0);
    digitalWrite(motors[i].in1, LOW);
    digitalWrite(motors[i].in2, LOW);
  }
}

void setMotor(uint8_t index, int8_t direction, uint8_t pwm) {
  const MotorPins &m = motors[index];
  digitalWrite(m.in1, direction > 0 ? HIGH : LOW);
  digitalWrite(m.in2, direction < 0 ? HIGH : LOW);
  analogWrite(m.pwm, pwm);
}

void applyCommand(uint8_t command) {
  // Direction is per motor: +1 forward, -1 reverse, 0 coast. This table
  // matches the direct pin writes in the original simple controller.
  static const int8_t directions[11][4] = {
    { 0, 0, 0, 0}, { 1, 1, 1, 1}, {-1,-1,-1,-1},
    {-1, 1, 1,-1}, { 1,-1,-1, 1}, { 0, 1, 1, 0},
    { 1, 0, 0, 1}, { 0,-1,-1, 0}, {-1, 0, 0,-1},
    {-1, 1,-1, 1}, { 1,-1, 1,-1}
  };
  for (uint8_t i = 0; i < 4; ++i) {
    const int8_t direction = directions[command][i];
    setMotor(i, direction, direction == 0 ? 0 : MOTOR_PWM);
  }
}

bool sendCan(unsigned long id, uint8_t code) {
  uint8_t data[1] = {code};
  bool ok = false;
  if (xSemaphoreTake(canBusMutex, portMAX_DELAY) == pdTRUE) {
    ok = CAN0.sendMsgBuf(id, 0, 1, data) == CAN_OK;
    xSemaphoreGive(canBusMutex);
  }
  return ok;
}

// ----------------------------------------------------------------------------
// Serial output (any task), one whole line per mutex hold
// ----------------------------------------------------------------------------
void serialLine(const __FlashStringHelper *prefix, const __FlashStringHelper *value,
                const __FlashStringHelper *suffix) {
  if (xSemaphoreTake(serialTxMutex, portMAX_DELAY) != pdTRUE) return;
  Serial.print(prefix);
  if (value) Serial.print(value);
  Serial.println(suffix);
  xSemaphoreGive(serialTxMutex);
}

void serialAck(const __FlashStringHelper *channel) {
  serialLine(F("{\"t\":\"ACK\",\"cmd\":\""), channel, F("\",\"ok\":true}"));
}

void serialError(const __FlashStringHelper *reason) {
  serialLine(F("{\"t\":\"ERR\",\"error\":\""), reason, F("\"}"));
}

// ----------------------------------------------------------------------------
// Task: CAN_RX
// ----------------------------------------------------------------------------
void onCanInterrupt() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(canRxSemaphore, &woken);
  // CAN_RX runs at the next tick at the latest; the AVR port has no portable
  // yield-from-ISR, so the flag is not acted on here.
  (void)woken;
}

void taskCanReceive(void *) {
  for (;;) {
    // Wake on the INT pin, or poll every tick when it is not wired. Polling
    // also catches frames that arrive while INT is already low.
    xSemaphoreTake(canRxSemaphore, 1);

    for (;;) {
      unsigned long id = 0;
      byte length = 0;
      byte data[8];
      bool got = false;
      if (xSemaphoreTake(canBusMutex, portMAX_DELAY) == pdTRUE) {
        got = CAN0.checkReceive() == CAN_MSGAVAIL &&
              CAN0.readMsgBuf(&id, &length, data) == CAN_OK;
        xSemaphoreGive(canBusMutex);
      }
      if (!got) break;
      if (length < 1) continue;

      CommandEvent event;
      event.source = FROM_CAN;
      if (id == CAN_ID_MOTOR) {
        event.channel = CH_MOTOR;
        event.code = validMotorCommand(data[0]) ? (int8_t)data[0] : -1;
      } else if (id == CAN_ID_ARM) {
        // Only observed for telemetry; the arm controller acts on it directly.
        event.channel = CH_ARM;
        event.code = validArmCommand(data[0]) ? (int8_t)data[0] : -1;
      } else {
        continue;
      }
      // Joystick frames repeat every 50 ms, so dropping one on a full queue is harmless.
      xQueueSend(commandQueue, &event, 0);
    }
  }
}

// ----------------------------------------------------------------------------
// Task: SERIAL_RX
// ----------------------------------------------------------------------------
bool parseCode(const char *line, const char *prefix, long *result) {
  const size_t prefixLength = strlen(prefix);
  if (strncmp(line, prefix, prefixLength) != 0 || line[prefixLength] == '\0') return false;
  char *end = nullptr;
  *result = strtol(line + prefixLength, &end, 10);
  return *end == '\0';
}

void queueSerialCommand(Channel channel, int8_t code, const __FlashStringHelper *ack) {
  const CommandEvent event = {FROM_SERIAL, channel, code};
  // Short wait so a burst from the Pi is not rejected while CONTROL catches up.
  if (xQueueSend(commandQueue, &event, pdMS_TO_TICKS(30)) == pdTRUE) serialAck(ack);
  else serialError(F("QUEUE_FULL"));
}

void processSerialCommand(const char *line) {
  if (line[0] == '\0') return;

  if (strcmp(line, "PING") == 0) {
    serialLine(F("{\"t\":\"PONG\"}"), nullptr, F(""));
    return;
  }

  if (strcmp(line, "STOP") == 0 || strcmp(line, "CMD:ALL:0") == 0) {
    queueSerialCommand(CH_ALL_STOP, STOP, F("ALL_STOP"));
    return;
  }

  long code = 0;
  if (parseCode(line, "CMD:MOTOR:", &code) && validMotorCommand(code)) {
    queueSerialCommand(CH_MOTOR, (int8_t)code, F("MOTOR"));
    return;
  }

  if (parseCode(line, "CMD:ARM:", &code) && validArmCommand(code)) {
    queueSerialCommand(CH_ARM, (int8_t)code, F("ARM"));
    return;
  }

  serialError(F("UNKNOWN_OR_INVALID_COMMAND"));
}

void taskSerialReceive(void *) {
  char line[48];
  uint8_t length = 0;

  for (;;) {
    // The 64-byte hardware RX buffer holds ~5 ms of 115200 baud, far more than
    // one tick of the short command lines the Pi sends.
    while (Serial.available() > 0) {
      const char c = (char)Serial.read();
      if (c == '\n') {
        line[length] = '\0';
        processSerialCommand(line);
        length = 0;
      } else if (c == '\r') {
        continue;
      } else if (length < sizeof(line) - 1) {
        line[length++] = c;
      } else {
        length = 0;  // Drop an overlong line instead of running a truncated command.
      }
    }
    vTaskDelay(1);
  }
}

// ----------------------------------------------------------------------------
// Task: CONTROL (owns its ControlState; no other task touches it)
// ----------------------------------------------------------------------------
// Serial beats CAN: the joystick keeps sending STOP heartbeats, which would
// otherwise cancel every web command within 50 ms.
int8_t activeMotor(const ControlState &s) {
  if (s.serialMotorActive) return s.serialMotor;
  return s.canMotor;
}

int8_t activeArm(const ControlState &s) {
  if (s.serialArmActive) return s.serialArm;
  return s.canArm;
}

void startSerialArm(ControlState &s, uint8_t code, unsigned long now) {
  s.serialArm = code;
  s.serialArmActive = true;
  s.serialArmTime = now;
  s.armTxTime = now;
  sendCan(CAN_ID_ARM, code);
}

void handleEvent(ControlState &s, const CommandEvent &event, unsigned long now) {
  if (event.source == FROM_CAN) {
    if (event.channel == CH_MOTOR) {
      s.canMotor = event.code;
      s.canMotorTime = now;
    } else {
      s.canArm = event.code;
      s.canArmTime = now;
    }
    return;
  }

  switch (event.channel) {
    case CH_MOTOR:
      s.serialMotor = event.code;
      s.serialMotorActive = true;
      s.serialMotorTime = now;
      break;
    case CH_ARM:
      startSerialArm(s, (uint8_t)event.code, now);
      break;
    case CH_ALL_STOP:
      // Hold STOP for the serial timeout so a joystick heartbeat cannot undo
      // an emergency stop. STOP alone does not switch the arm's relay off.
      s.serialMotor = STOP;
      s.serialMotorActive = true;
      s.serialMotorTime = now;
      sendCan(CAN_ID_ARM, PUMP_OFF);
      startSerialArm(s, STOP, now);
      break;
  }
}

void checkTimeouts(ControlState &s, unsigned long now) {
  if (s.canMotor >= 0 && now - s.canMotorTime > CAN_TIMEOUT) s.canMotor = -1;
  if (s.canArm >= 0 && now - s.canArmTime > CAN_TIMEOUT) s.canArm = -1;
  if (s.serialMotorActive && now - s.serialMotorTime > SERIAL_COMMAND_TIMEOUT_MS) {
    s.serialMotorActive = false;
  }
  if (s.serialArmActive && now - s.serialArmTime > SERIAL_COMMAND_TIMEOUT_MS) {
    s.serialArmActive = false;
    sendCan(CAN_ID_ARM, STOP);  // Stop servos now instead of waiting for the arm's own 1 s timeout.
  }
}

// Repeat the Pi's arm command on CAN while it is live, the same 50 ms
// heartbeat the joystick uses. The joystick's own 0x101 frames still reach
// the arm controller directly, so the two sources interleave if both are on.
void serviceSerialArm(ControlState &s, unsigned long now) {
  if (!s.serialArmActive || now - s.armTxTime < SERIAL_CAN_TX_INTERVAL_MS) return;
  s.armTxTime = now;
  sendCan(CAN_ID_ARM, (uint8_t)s.serialArm);
}

void taskControl(void *) {
  ControlState state;

  for (;;) {
    // Block until a command arrives or one period passes, so timeouts still run
    // when both inputs go quiet.
    CommandEvent event;
    if (xQueueReceive(commandQueue, &event, CONTROL_PERIOD) == pdTRUE) {
      do {
        handleEvent(state, event, millis());
      } while (xQueueReceive(commandQueue, &event, 0) == pdTRUE);
    }

    const unsigned long now = millis();
    checkTimeouts(state, now);
    serviceSerialArm(state, now);

    const int8_t motor = activeMotor(state);
    const bool changed = motor != state.appliedMotor;
    if (changed) {
      state.appliedMotor = motor;
      if (motor < 0) stopMotors();
      else applyCommand((uint8_t)motor);
    }

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
      snapshot.motor = motor;
      snapshot.arm = activeArm(state);
      snapshot.pwm = motor > STOP ? MOTOR_PWM : 0;
      snapshot.commandTime = state.serialMotorActive ? state.serialMotorTime
                           : (state.canMotor >= 0 ? state.canMotorTime : now);
      xSemaphoreGive(stateMutex);
    }

    // Send telemetry now instead of waiting up to 100 ms.
    if (changed && telemetryTask) xTaskNotifyGive(telemetryTask);
  }
}

// ----------------------------------------------------------------------------
// Task: TELEMETRY
// ----------------------------------------------------------------------------
void taskTelemetry(void *) {
  uint16_t sequence = 0;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, TELEMETRY_PERIOD);

    // The only task that touches the ADC.
    const BatterySample battery = battery_read();

    ControlSnapshot copy;
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE) continue;
    snapshot.battery = battery;
    copy = snapshot;
    xSemaphoreGive(stateMutex);

    const unsigned long age = copy.motor >= 0 ? millis() - copy.commandTime : 0;

    char body[72];
    // Body excludes the '*' and checksum.
    snprintf(body, sizeof(body), "MS1,%d,%u,%d,%u,%lu,%u,%u,%lu,%u",
             copy.motor, copy.motor >= 0 ? 1 : 0, copy.arm, copy.arm >= 0 ? 1 : 0,
             (unsigned long)battery.millivolts, battery.adc, copy.pwm, age, sequence++);
    uint8_t checksum = 0;
    for (const char *p = body; *p; ++p) checksum ^= (uint8_t)*p;

    if (xSemaphoreTake(serialTxMutex, portMAX_DELAY) == pdTRUE) {
      Serial.print(body);
      Serial.print('*');
      if (checksum < 16) Serial.print('0');
      Serial.println(checksum, HEX);
      xSemaphoreGive(serialTxMutex);
    }
  }
}

// ----------------------------------------------------------------------------
// Setup: hardware init, then create the RTOS objects. The scheduler starts
// after setup() returns and loop() becomes the idle task.
// ----------------------------------------------------------------------------
void halt(const __FlashStringHelper *reason) {
  stopMotors();
  Serial.println(reason);
  for (;;) {}
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(motors[i].pwm, OUTPUT);
    pinMode(motors[i].in1, OUTPUT);
    pinMode(motors[i].in2, OUTPUT);
  }
  stopMotors();

  pinMode(CAN_CS_PIN, OUTPUT);
  digitalWrite(CAN_CS_PIN, HIGH);
  pinMode(50, INPUT_PULLUP); // MISO
  pinMode(51, OUTPUT);       // MOSI
  pinMode(52, OUTPUT);       // SCK
  SPI.begin();
  while (CAN0.begin(MCP_ANY, CAN_500KBPS, CAN_CLOCK_SET) != CAN_OK) delay(1000);
  CAN0.setMode(MCP_NORMAL);

  battery_init();

  commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(CommandEvent));
  canRxSemaphore = xSemaphoreCreateBinary();
  canBusMutex = xSemaphoreCreateMutex();
  serialTxMutex = xSemaphoreCreateMutex();
  stateMutex = xSemaphoreCreateMutex();
  if (!commandQueue || !canRxSemaphore || !canBusMutex || !serialTxMutex || !stateMutex) {
    halt(F("{\"t\":\"ERR\",\"error\":\"RTOS_OBJECTS\"}"));
  }

  // Pull-up keeps the pin quiet when the MCP2515 INT wire is not connected.
  pinMode(CAN_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CAN_INT_PIN), onCanInterrupt, FALLING);

  const bool created =
    xTaskCreate(taskCanReceive, "CAN_RX", CAN_TASK_STACK, nullptr, 3, nullptr) == pdPASS &&
    xTaskCreate(taskControl, "CONTROL", CONTROL_TASK_STACK, nullptr, 2, nullptr) == pdPASS &&
    xTaskCreate(taskSerialReceive, "SERIAL_RX", SERIAL_TASK_STACK, nullptr, 2, nullptr) == pdPASS &&
    xTaskCreate(taskTelemetry, "TELEMETRY", TELEMETRY_TASK_STACK, nullptr, 1, &telemetryTask) == pdPASS;
  if (!created) halt(F("{\"t\":\"ERR\",\"error\":\"RTOS_TASKS\"}"));
}

// FreeRTOS tasks own all work; loop() runs as the idle task.
void loop() {}
