/*
 * Super-loop CAN motor controller for Arduino Mega 2560 (no RTOS), with a
 * USB-Serial link to the Raspberry Pi (FastAPI) at 115200 baud, 8N1, one ASCII
 * line per frame. Same wiring and protocol as motor_controller_simplify, so the
 * backend works with either sketch.
 *
 * Required library: mcp_can.
 *
 * loop() runs every job in turn and never blocks; each job checks millis():
 *   readCan()        drain the MCP2515 when its INT pin fired (or every
 *                    CAN_POLL_INTERVAL_MS when INT is not wired)
 *   readSerial()     collect bytes from the Pi and run each complete line
 *   updateControl()  timeouts, pick the command source, drive the motors
 *   sendTelemetry()  battery ADC + MS1 every 100 ms, or at once on a change
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
 * Without FreeRTOS the watchdog timer is free, so it resets the board if
 * loop() stalls for more than 500 ms (motors are stopped again in setup()).
 */

#include <SPI.h>
#include <mcp_can.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battery_sensor.h"

constexpr uint8_t M1_PWM = 5, M1_IN1 = 22, M1_IN2 = 23;
constexpr uint8_t M2_PWM = 6, M2_IN1 = 24, M2_IN2 = 25;
constexpr uint8_t M3_PWM = 7, M3_IN1 = 30, M3_IN2 = 31;
constexpr uint8_t M4_PWM = 8, M4_IN1 = 32, M4_IN2 = 33;
constexpr uint8_t CAN_CS_PIN = 53;
constexpr uint8_t CAN_INT_PIN = 2;          // INT4 on the Mega; optional, see readCan()
constexpr unsigned long CAN_ID_MOTOR = 0x100;
constexpr unsigned long CAN_ID_ARM = 0x101;
constexpr unsigned long CAN_TIMEOUT = 300;
constexpr unsigned long CAN_POLL_INTERVAL_MS = 5;
constexpr unsigned long SERIAL_COMMAND_TIMEOUT_MS = 1000;
constexpr unsigned long SERIAL_CAN_TX_INTERVAL_MS = 50;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 100;
#define CAN_CLOCK_SET MCP_8MHZ
constexpr uint8_t MOTOR_PWM = 150;

MCP_CAN CAN0(CAN_CS_PIN);

enum Command : uint8_t {
  STOP = 0, FORWARD, BACKWARD, LEFT, RIGHT, FORWARD_LEFT,
  FORWARD_RIGHT, BACKWARD_LEFT, BACKWARD_RIGHT, SPIN_LEFT, SPIN_RIGHT,
  PUMP_ON, PUMP_OFF, HEAD_UP, HEAD_DOWN
};

struct MotorPins { uint8_t pwm, in1, in2; };
const MotorPins motors[] = {
  {M1_PWM, M1_IN1, M1_IN2}, {M2_PWM, M2_IN1, M2_IN2},
  {M3_PWM, M3_IN1, M3_IN2}, {M4_PWM, M4_IN1, M4_IN2}
};

// Command state. Single-threaded, so plain globals are safe; only the ISR flag
// is shared with an interrupt and therefore volatile.
volatile bool canInterrupt = false;

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
bool telemetryDue = false;
unsigned long lastCanPoll = 0;
unsigned long lastTelemetry = 0;
uint16_t telemetrySequence = 0;

bool validMotorCommand(long code) { return code >= STOP && code <= SPIN_RIGHT; }
bool validArmCommand(long code) {
  return (code >= STOP && code <= BACKWARD_RIGHT) || (code >= PUMP_ON && code <= HEAD_DOWN);
}

// ----------------------------------------------------------------------------
// Motors
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
  // Direction is per motor: +1 forward, -1 reverse, 0 coast.
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
  return CAN0.sendMsgBuf(id, 0, 1, data) == CAN_OK;
}

// ----------------------------------------------------------------------------
// Serial replies
// ----------------------------------------------------------------------------
void serialAck(const __FlashStringHelper *channel) {
  Serial.print(F("{\"t\":\"ACK\",\"cmd\":\""));
  Serial.print(channel);
  Serial.println(F("\",\"ok\":true}"));
}

void serialError(const __FlashStringHelper *reason) {
  Serial.print(F("{\"t\":\"ERR\",\"error\":\""));
  Serial.print(reason);
  Serial.println(F("\"}"));
}

// ----------------------------------------------------------------------------
// CAN input (joystick board)
// ----------------------------------------------------------------------------
void onCanInterrupt() {
  canInterrupt = true;  // keep the ISR short; readCan() does the SPI work
}

void readCan(unsigned long now) {
  // Read on the INT pin, or poll when it is not wired. Polling also catches
  // frames that arrive while INT is already low.
  if (!canInterrupt && now - lastCanPoll < CAN_POLL_INTERVAL_MS) return;
  canInterrupt = false;
  lastCanPoll = now;

  // The MCP2515 has only 2 RX buffers and the joystick sends 0x100 and 0x101
  // back to back, so drain everything that is waiting.
  while (CAN0.checkReceive() == CAN_MSGAVAIL) {
    unsigned long id = 0;
    byte length = 0;
    byte data[8];
    if (CAN0.readMsgBuf(&id, &length, data) != CAN_OK || length < 1) continue;

    if (id == CAN_ID_MOTOR) {
      canMotor = validMotorCommand(data[0]) ? (int8_t)data[0] : -1;
      canMotorTime = now;
    } else if (id == CAN_ID_ARM) {
      // Only observed for telemetry; the arm board acts on it directly.
      canArm = validArmCommand(data[0]) ? (int8_t)data[0] : -1;
      canArmTime = now;
    }
  }
}

// ----------------------------------------------------------------------------
// Serial input (Raspberry Pi)
// ----------------------------------------------------------------------------
bool parseCode(const char *line, const char *prefix, long *result) {
  const size_t prefixLength = strlen(prefix);
  if (strncmp(line, prefix, prefixLength) != 0 || line[prefixLength] == '\0') return false;
  char *end = nullptr;
  *result = strtol(line + prefixLength, &end, 10);
  return *end == '\0';
}

void startSerialArm(uint8_t code, unsigned long now) {
  serialArm = code;
  serialArmActive = true;
  serialArmTime = now;
  armTxTime = now;
  sendCan(CAN_ID_ARM, code);
}

void processSerialCommand(const char *line, unsigned long now) {
  if (line[0] == '\0') return;

  if (strcmp(line, "PING") == 0) {
    Serial.println(F("{\"t\":\"PONG\"}"));
    return;
  }

  if (strcmp(line, "STOP") == 0 || strcmp(line, "CMD:ALL:0") == 0) {
    // Hold STOP for the serial timeout so a joystick heartbeat cannot undo an
    // emergency stop. STOP alone does not switch the arm's relay off.
    serialMotor = STOP;
    serialMotorActive = true;
    serialMotorTime = now;
    sendCan(CAN_ID_ARM, PUMP_OFF);
    startSerialArm(STOP, now);
    serialAck(F("ALL_STOP"));
    return;
  }

  long code = 0;
  if (parseCode(line, "CMD:MOTOR:", &code) && validMotorCommand(code)) {
    serialMotor = (int8_t)code;
    serialMotorActive = true;
    serialMotorTime = now;
    serialAck(F("MOTOR"));
    return;
  }

  if (parseCode(line, "CMD:ARM:", &code) && validArmCommand(code)) {
    startSerialArm((uint8_t)code, now);
    serialAck(F("ARM"));
    return;
  }

  serialError(F("UNKNOWN_OR_INVALID_COMMAND"));
}

void readSerial(unsigned long now) {
  static char line[48];
  static uint8_t length = 0;

  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\n') {
      line[length] = '\0';
      processSerialCommand(line, now);
      length = 0;
    } else if (c == '\r') {
      continue;
    } else if (length < sizeof(line) - 1) {
      line[length++] = c;
    } else {
      length = 0;  // Drop an overlong line instead of running a truncated command.
    }
  }
}

// ----------------------------------------------------------------------------
// Control
// ----------------------------------------------------------------------------
// Serial beats CAN: the joystick keeps sending STOP heartbeats, which would
// otherwise cancel every web command within 50 ms.
int8_t activeMotor() { return serialMotorActive ? serialMotor : canMotor; }
int8_t activeArm() { return serialArmActive ? serialArm : canArm; }

void updateControl(unsigned long now) {
  if (canMotor >= 0 && now - canMotorTime > CAN_TIMEOUT) canMotor = -1;
  if (canArm >= 0 && now - canArmTime > CAN_TIMEOUT) canArm = -1;
  if (serialMotorActive && now - serialMotorTime > SERIAL_COMMAND_TIMEOUT_MS) {
    serialMotorActive = false;
  }
  if (serialArmActive && now - serialArmTime > SERIAL_COMMAND_TIMEOUT_MS) {
    serialArmActive = false;
    sendCan(CAN_ID_ARM, STOP);  // Stop servos now instead of waiting for the arm's own 1 s timeout.
  }

  // Repeat the Pi's arm command on CAN while it is live, the same 50 ms
  // heartbeat the joystick uses.
  if (serialArmActive && now - armTxTime >= SERIAL_CAN_TX_INTERVAL_MS) {
    armTxTime = now;
    sendCan(CAN_ID_ARM, (uint8_t)serialArm);
  }

  const int8_t motor = activeMotor();
  if (motor != appliedMotor) {
    appliedMotor = motor;
    if (motor < 0) stopMotors();
    else applyCommand((uint8_t)motor);
    telemetryDue = true;  // report the change now instead of waiting up to 100 ms
  }
}

// ----------------------------------------------------------------------------
// Telemetry
// ----------------------------------------------------------------------------
void sendTelemetry(unsigned long now) {
  if (!telemetryDue && now - lastTelemetry < TELEMETRY_INTERVAL_MS) return;
  telemetryDue = false;
  lastTelemetry = now;

  const BatterySample battery = battery_read();
  const int8_t motor = activeMotor();
  const int8_t arm = activeArm();
  const uint8_t pwm = motor > STOP ? MOTOR_PWM : 0;
  const unsigned long commandTime = serialMotorActive ? serialMotorTime
                                  : (canMotor >= 0 ? canMotorTime : now);
  const unsigned long age = motor >= 0 ? now - commandTime : 0;

  char body[72];
  // Body excludes the '*' and checksum.
  snprintf(body, sizeof(body), "MS1,%d,%u,%d,%u,%lu,%u,%u,%lu,%u",
           motor, motor >= 0 ? 1 : 0, arm, arm >= 0 ? 1 : 0,
           (unsigned long)battery.millivolts, battery.adc, pwm, age, telemetrySequence++);
  uint8_t checksum = 0;
  for (const char *p = body; *p; ++p) checksum ^= (uint8_t)*p;

  Serial.print(body);
  Serial.print('*');
  if (checksum < 16) Serial.print('0');
  Serial.println(checksum, HEX);
}

// ----------------------------------------------------------------------------
// Setup / loop
// ----------------------------------------------------------------------------
void setup() {
  // A watchdog reset leaves the watchdog enabled; turn it off before the slow
  // CAN init below so the board does not reset in a loop.
  MCUSR &= ~(1 << WDRF);
  wdt_disable();

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

  // Pull-up keeps the pin quiet when the MCP2515 INT wire is not connected.
  pinMode(CAN_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CAN_INT_PIN), onCanInterrupt, FALLING);

  wdt_enable(WDTO_500MS);
}

void loop() {
  wdt_reset();
  const unsigned long now = millis();
  readCan(now);
  readSerial(now);
  updateControl(now);
  sendTelemetry(now);
}
