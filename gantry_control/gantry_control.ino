/*
  AF-434 gantry firmware
  Arduino Mega 2560 driving four Leadshine iCL42 integrated closed-loop steppers
  (driver + 14-bit encoder built into each motor, PUL/DIR step interface).

  Axes
    X  (forward / backward)  motor 1 (LEFT) + motor 2 (RIGHT, mounted mirrored)
    Y  (left / right)        motor 3
    Z  (up / down)           motor 4

  Wiring (common-cathode, 5 V logic, see README.md)
    Motor 1  PUL+ -> D9   DIR+ -> D8   ENA+ -> D22   ALM+ -> D26
    Motor 2  PUL+ -> D7   DIR+ -> D6   ENA+ -> D23   ALM+ -> D27
    Motor 3  PUL+ -> D5   DIR+ -> D4   ENA+ -> D24   ALM+ -> D28
    Motor 4  PUL+ -> D3   DIR+ -> D2   ENA+ -> D25   ALM+ -> D29
    All motors: PUL-, DIR-, ENA- and ALM- -> Mega GND.  RS232 left unconnected.
    Motor power: external 24 V DC supply to each motor's VCC / GND, never to the Mega.

  Motion
    A Timer1 interrupt at 20 kHz generates step pulses for every axis.  The main
    loop runs a 1 kHz velocity controller that ramps each axis toward its target
    speed with a fixed acceleration, so starting, stopping and reversing are all
    smooth.  Releasing a key does not stop instantly: the axis decelerates to zero.
    An optional "go to position" command decelerates on approach the same way.

  Serial protocol, 115200 baud, one character per command unless noted
    w / s   jog X forward / backward        x   release X (ramp to stop)
    d / a   jog Y right / left              y   release Y
    q / e   jog Z up / down                 z   release Z
    +  -    jog speed up / down (x1.25)     S   ramp-stop every axis
    !       EMERGENCY stop, no ramp         h   heartbeat (keeps motion allowed)
    ?       print firmware id               p   print positions, velocities, enable, alarms
    0       zero all positions (only while stopped)
    N       enable all drivers (hold torque)   F   free all drivers (only while stopped)
    C       clear the alarm latch after fixing the cause
    G<axis><steps>\n   go to absolute position, e.g.  GX1600\n  or  GZ-400\n
  If no byte arrives for 500 ms every axis ramps to a stop (lost USB / crashed PC).
  If any driver raises ALM, every axis hard-stops, "ALARM Mn" is printed, and
  motion is refused until C is sent and the driver has dropped its alarm.
*/

#define FIRMWARE_ID "AF434-GANTRY-v1"

// ---------------------------------------------------------------- pins ----
const uint8_t M1_PUL = 9, M1_DIR = 8;   // Motor 1: X axis, LEFT side
const uint8_t M2_PUL = 7, M2_DIR = 6;   // Motor 2: X axis, RIGHT side (mirrored)
const uint8_t M3_PUL = 5, M3_DIR = 4;   // Motor 3: Y axis
const uint8_t M4_PUL = 3, M4_DIR = 2;   // Motor 4: Z axis

const uint8_t ENA_PINS[4] = { 22, 23, 24, 25 };  // motor 1..4 ENA+ (ENA- -> GND)
const uint8_t ALM_PINS[4] = { 26, 27, 28, 29 };  // motor 1..4 ALM+ (ALM- -> GND)

// -------------------------------------------------- enable and alarm ----
// iCL42 ENA input: optocoupler OFF (Mega pin LOW) = driver ENABLED.  That is
// the same state as leaving ENA unconnected.  Pin HIGH turns the opto on and
// frees the motor.  If the shafts turn by hand as soon as this sketch starts,
// swap these two values.
const uint8_t ENA_ENABLED_LEVEL  = LOW;
const uint8_t ENA_DISABLED_LEVEL = HIGH;
// ALM is an open-collector output.  ALM+ goes to a Mega pin with the internal
// pull-up, ALM- to GND.  On these motors the output conducts (pin LOW) during
// normal operation and opens (pin HIGH) on a fault, so HIGH = alarm.  That was
// confirmed on the real gantry: with LOW here, all four reported alarm at idle.
// If a different driver revision reports ALARM on every motor at idle with
// this set to HIGH, change it to LOW.
const uint8_t ALM_ACTIVE_LEVEL   = HIGH;
const uint8_t ALM_DEBOUNCE_MS    = 5;

// ------------------------------------------------- direction convention ----
// Which DIR level makes an iCL42 turn clockwise (looking at the shaft face).
// This depends on the driver's SW5 setting.  All four motors should use the
// same SW5 position; if EVERY motor turns the wrong way, swap these two values.
const uint8_t CW_LEVEL  = HIGH;
const uint8_t CCW_LEVEL = LOW;

// Does the axis's POSITIVE direction (W, D, Q) need clockwise rotation?
//   Motor 1: forward = CW      Motor 2: forward = CCW  (mounted opposite)
//   Motor 3: right   = CW      Motor 4: up      = CCW
// TEST: both X motors turn the SAME way (both CW on W).  If the beam now
// travels backwards on W, set both of these to false.  If the beam skews or
// stalls, the motors need opposite senses and the original (true, false)
// or (false, true) pair is the right one.
const bool M1_POSITIVE_IS_CW = true;
const bool M2_POSITIVE_IS_CW = true;
const bool M3_POSITIVE_IS_CW = true;
const bool M4_POSITIVE_IS_CW = false;

// ------------------------------------------------------- motion tuning ----
// Units are steps (pulses).  At the driver's default 1600 pulses/rev,
// 1600 steps/s = 60 RPM.
const uint16_t TICK_HZ          = 20000; // step generator rate; max step rate
const float    DEFAULT_SPEED    = 1000;  // jog speed at power-up, steps/s
const float    MIN_SPEED        = 100;   // lower bound for + / -
const float    MAX_SPEED        = 8000;  // upper bound for + / -
const float    SPEED_STEP       = 1.25;  // each + or - multiplies/divides by this
const float    ACCEL            = 3000;  // steps/s^2, ramp up AND ramp down
const float    CRAWL_SPEED      = 150;   // final approach speed for G moves
const unsigned long COMMAND_TIMEOUT_MS = 500;
const unsigned long CONTROL_PERIOD_US  = 1000;   // 1 kHz velocity controller
const uint8_t  PULSE_WIDTH_US   = 3;     // iCL42 minimum 2.5 us

// --------------------------------------------------------------- types ----
struct Motor {
  uint8_t pulPin, dirPin;
  bool positiveIsCW;
  volatile uint8_t *pulPort;   // direct port access so the ISR stays short
  uint8_t pulMask;
};

struct Axis {
  char name;
  Motor *motors[2];
  uint8_t motorCount;

  // Shared with the ISR
  volatile int16_t  velocity;     // commanded steps/s, signed
  volatile uint16_t accumulator;  // fractional step accumulator
  volatile int32_t  position;     // steps from zero
  volatile int8_t   currentDir;   // +1 / -1, what the DIR pins currently show

  // Main-loop control state
  float   velocityF;              // ramped velocity, steps/s
  int8_t  jogDir;                 // -1, 0, +1 from the keyboard
  bool    gotoActive;
  int32_t gotoTarget;
};

Motor motor1 = { M1_PUL, M1_DIR, M1_POSITIVE_IS_CW, 0, 0 };
Motor motor2 = { M2_PUL, M2_DIR, M2_POSITIVE_IS_CW, 0, 0 };
Motor motor3 = { M3_PUL, M3_DIR, M3_POSITIVE_IS_CW, 0, 0 };
Motor motor4 = { M4_PUL, M4_DIR, M4_POSITIVE_IS_CW, 0, 0 };

Axis axes[3] = {
  { 'X', { &motor1, &motor2 }, 2, 0, 0, 0, 1, 0.0f, 0, false, 0 },
  { 'Y', { &motor3, 0 },       1, 0, 0, 0, 1, 0.0f, 0, false, 0 },
  { 'Z', { &motor4, 0 },       1, 0, 0, 0, 1, 0.0f, 0, false, 0 },
};
const uint8_t AXIS_COUNT = 3;

float jogSpeed = DEFAULT_SPEED;
unsigned long lastCommandMs = 0;
unsigned long lastControlUs = 0;
bool wasMoving = false;

bool motorsEnabled = true;
uint8_t alarmMask = 0;          // latched: bit n set = motor n+1 raised ALM
uint8_t alarmRawLast = 0;       // debounce state
uint8_t alarmDebounced = 0;
unsigned long alarmRawChangedMs = 0;

// Line buffer for the multi-character G command
char lineBuf[16];
uint8_t lineLen = 0;
bool inLine = false;

// ------------------------------------------------------------ helpers ----
static inline void writeDirPins(Axis &a, int8_t dir) {
  for (uint8_t i = 0; i < a.motorCount; ++i) {
    Motor *m = a.motors[i];
    bool cw = (dir > 0) ? m->positiveIsCW : !m->positiveIsCW;
    digitalWrite(m->dirPin, cw ? CW_LEVEL : CCW_LEVEL);
  }
  a.currentDir = dir;
}

int32_t readPosition(Axis &a) {
  noInterrupts();
  int32_t p = a.position;
  interrupts();
  return p;
}

void setVelocity(Axis &a, int16_t v) {
  noInterrupts();
  a.velocity = v;
  interrupts();
}

bool anyAxisMoving() {
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
    if (axes[i].velocityF != 0.0f || axes[i].velocity != 0) return true;
  }
  return false;
}

// -------------------------------------------------------- step generator ----
ISR(TIMER1_COMPA_vect) {
  uint8_t raised = 0;  // bit i set => axis i pulsed this tick
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
    Axis &a = axes[i];
    int16_t v = a.velocity;
    if (v == 0) continue;
    int8_t dir = (v > 0) ? 1 : -1;
    if (dir != a.currentDir) {
      writeDirPins(a, dir);     // one tick (50 us) of DIR setup before stepping
      continue;
    }
    a.accumulator += (uint16_t)(v > 0 ? v : -v);
    if (a.accumulator >= TICK_HZ) {
      a.accumulator -= TICK_HZ;
      for (uint8_t k = 0; k < a.motorCount; ++k) {
        *(a.motors[k]->pulPort) |= a.motors[k]->pulMask;
      }
      a.position += dir;
      raised |= (1 << i);
    }
  }
  if (raised) {
    delayMicroseconds(PULSE_WIDTH_US);
    for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
      if (!(raised & (1 << i))) continue;
      Axis &a = axes[i];
      for (uint8_t k = 0; k < a.motorCount; ++k) {
        *(a.motors[k]->pulPort) &= ~(a.motors[k]->pulMask);
      }
    }
  }
}

// ---------------------------------------------------- velocity control ----
void updateAxis(Axis &a, float dt) {
  float target;

  if (a.gotoActive) {
    int32_t remaining = a.gotoTarget - readPosition(a);
    if (remaining == 0) {
      // Arrived.  We are crawling at CRAWL_SPEED at most, so snapping to zero
      // here is imperceptible and avoids hunting around the target.
      a.gotoActive = false;
      a.velocityF = 0.0f;
      setVelocity(a, 0);
      Serial.print(F("DONE "));
      Serial.println(a.name);
      return;
    }
    // Speed we may still have at this distance and still be able to stop.
    // 0.85 keeps the curve a little shallower than ACCEL so the ramp below
    // can always follow it without overshooting.
    float dist = (remaining > 0) ? (float)remaining : (float)(-remaining);
    float allowed = sqrt(2.0f * ACCEL * 0.85f * dist);
    if (allowed < CRAWL_SPEED) allowed = CRAWL_SPEED;
    if (allowed > jogSpeed) allowed = jogSpeed;
    target = (remaining > 0) ? allowed : -allowed;
  } else {
    target = a.jogDir * jogSpeed;
  }

  float dv = ACCEL * dt;
  if (a.velocityF < target) {
    a.velocityF += dv;
    if (a.velocityF > target) a.velocityF = target;
  } else if (a.velocityF > target) {
    a.velocityF -= dv;
    if (a.velocityF < target) a.velocityF = target;
  }
  setVelocity(a, (int16_t)a.velocityF);
}

void rampStopAll() {
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
    axes[i].jogDir = 0;
    axes[i].gotoActive = false;
  }
}

void hardStopAll() {
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
    axes[i].jogDir = 0;
    axes[i].gotoActive = false;
    axes[i].velocityF = 0.0f;
    setVelocity(axes[i], 0);
  }
}

bool motionAllowed() {
  if (alarmMask) {
    Serial.println(F("ERR alarm active: fix the cause, then send C"));
    return false;
  }
  if (!motorsEnabled) {
    Serial.println(F("ERR motors are free: send N to enable"));
    return false;
  }
  return true;
}

void jog(Axis &a, int8_t dir) {
  if (dir != 0 && !motionAllowed()) return;
  a.gotoActive = false;
  a.jogDir = dir;
}

// ------------------------------------------------- enable and alarm ----
void setMotorsEnabled(bool enabled) {
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(ENA_PINS[i], enabled ? ENA_ENABLED_LEVEL : ENA_DISABLED_LEVEL);
  }
  motorsEnabled = enabled;
  Serial.println(enabled ? F("ENABLED") : F("FREE"));
}

void printAlarmList(uint8_t mask) {
  for (uint8_t i = 0; i < 4; ++i) {
    if (mask & (1 << i)) { Serial.print(F(" M")); Serial.print(i + 1); }
  }
}

uint8_t readAlarmPins() {
  uint8_t raw = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    if (digitalRead(ALM_PINS[i]) == ALM_ACTIVE_LEVEL) raw |= (1 << i);
  }
  return raw;
}

void pollAlarms() {
  uint8_t raw = readAlarmPins();
  unsigned long now = millis();
  if (raw != alarmRawLast) {
    alarmRawLast = raw;
    alarmRawChangedMs = now;
  } else if (now - alarmRawChangedMs >= ALM_DEBOUNCE_MS) {
    alarmDebounced = raw;
  }
  uint8_t newAlarms = alarmDebounced & ~alarmMask;
  if (newAlarms) {
    hardStopAll();
    alarmMask |= newAlarms;
    Serial.print(F("ALARM"));
    printAlarmList(newAlarms);
    Serial.println();
  }
}

void clearAlarms() {
  alarmDebounced = readAlarmPins();
  alarmRawLast = alarmDebounced;
  alarmRawChangedMs = millis();
  alarmMask = alarmDebounced;
  if (alarmMask) {
    Serial.print(F("ALARM still active:"));
    printAlarmList(alarmMask);
    Serial.println(F(". Power-cycle the motor or free/enable it with F then N."));
  } else {
    Serial.println(F("ALARM CLEARED"));
  }
}

// ------------------------------------------------------------ reporting ----
void printStatus() {
  Serial.print(F("POS"));
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
    Serial.print(' '); Serial.print(axes[i].name); Serial.print('=');
    Serial.print(readPosition(axes[i]));
  }
  Serial.print(F(" VEL"));
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
    Serial.print(' '); Serial.print(axes[i].name); Serial.print('=');
    Serial.print(axes[i].velocity);
  }
  Serial.print(F(" SPEED="));
  Serial.print((long)jogSpeed);
  Serial.print(F(" ENABLED="));
  Serial.print(motorsEnabled ? 1 : 0);
  Serial.print(F(" ALARM="));
  if (alarmMask) printAlarmList(alarmMask); else Serial.print(F(" none"));
  Serial.println();
}

void printSpeed() {
  Serial.print(F("SPEED "));
  Serial.println((long)jogSpeed);
}

Axis *axisByName(char c) {
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
    if (axes[i].name == c) return &axes[i];
  }
  return 0;
}

void handleLine() {
  // Only G<axis><steps> is defined.  Example: "GX1600" or "GZ-400"
  lineBuf[lineLen] = '\0';
  if (lineLen >= 3 && lineBuf[0] == 'G') {
    Axis *a = axisByName(toupper(lineBuf[1]));
    if (a) {
      if (!motionAllowed()) return;
      a->gotoTarget = atol(lineBuf + 2);
      a->jogDir = 0;
      a->gotoActive = true;
      Serial.print(F("GOTO ")); Serial.print(a->name); Serial.print(' ');
      Serial.println(a->gotoTarget);
      return;
    }
  }
  Serial.print(F("ERR bad line: "));
  Serial.println(lineBuf);
}

// ------------------------------------------------------- command input ----
void handleChar(char c) {
  if (inLine) {
    if (c == '\n' || c == '\r') {
      inLine = false;
      if (lineLen) handleLine();
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
    return;
  }

  switch (c) {
    case 'w': jog(axes[0], +1); break;
    case 's': jog(axes[0], -1); break;
    case 'd': jog(axes[1], +1); break;
    case 'a': jog(axes[1], -1); break;
    case 'q': jog(axes[2], +1); break;
    case 'e': jog(axes[2], -1); break;
    case 'x': jog(axes[0], 0);  break;
    case 'y': jog(axes[1], 0);  break;
    case 'z': jog(axes[2], 0);  break;
    case 'S': rampStopAll(); break;
    case '!': hardStopAll(); Serial.println(F("ESTOP")); break;
    case '+':
      jogSpeed *= SPEED_STEP;
      if (jogSpeed > MAX_SPEED) jogSpeed = MAX_SPEED;
      printSpeed();
      break;
    case '-':
      jogSpeed /= SPEED_STEP;
      if (jogSpeed < MIN_SPEED) jogSpeed = MIN_SPEED;
      printSpeed();
      break;
    case 'h': break;                      // heartbeat only
    case '?': Serial.println(F(FIRMWARE_ID)); break;
    case 'p': printStatus(); break;
    case '0':
      if (anyAxisMoving()) {
        Serial.println(F("ERR cannot zero while moving"));
      } else {
        noInterrupts();
        for (uint8_t i = 0; i < AXIS_COUNT; ++i) axes[i].position = 0;
        interrupts();
        Serial.println(F("ZEROED"));
      }
      break;
    case 'N': setMotorsEnabled(true); break;
    case 'F':
      if (anyAxisMoving()) {
        Serial.println(F("ERR cannot free motors while moving"));
      } else {
        rampStopAll();
        setMotorsEnabled(false);
      }
      break;
    case 'C': clearAlarms(); break;
    case 'G':
      inLine = true;
      lineLen = 0;
      lineBuf[lineLen++] = 'G';
      break;
    default: break;                       // ignore newlines and unknown bytes
  }
}

// --------------------------------------------------------------- setup ----
void setupMotor(Motor &m) {
  m.pulPort = portOutputRegister(digitalPinToPort(m.pulPin));
  m.pulMask = digitalPinToBitMask(m.pulPin);
  digitalWrite(m.pulPin, LOW);
  pinMode(m.pulPin, OUTPUT);
  pinMode(m.dirPin, OUTPUT);
}

void setup() {
  setupMotor(motor1);
  setupMotor(motor2);
  setupMotor(motor3);
  setupMotor(motor4);
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) writeDirPins(axes[i], +1);

  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(ENA_PINS[i], ENA_ENABLED_LEVEL);   // preload before OUTPUT
    pinMode(ENA_PINS[i], OUTPUT);
    pinMode(ALM_PINS[i], INPUT_PULLUP);
  }
  motorsEnabled = true;

  // Timer1, CTC mode, prescaler 1: 16 MHz / (OCR1A + 1) = TICK_HZ
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A  = (uint16_t)(F_CPU / TICK_HZ) - 1;
  TCCR1B |= (1 << WGM12) | (1 << CS10);
  TIMSK1 |= (1 << OCIE1A);
  interrupts();

  Serial.begin(115200);
  lastCommandMs = millis();
  lastControlUs = micros();
}

// ---------------------------------------------------------------- loop ----
void loop() {
  // Bounded so the control loop still runs if bytes arrive continuously.
  for (uint8_t i = 0; i < 32 && Serial.available(); ++i) {
    handleChar((char)Serial.read());
    lastCommandMs = millis();
  }

  if (millis() - lastCommandMs >= COMMAND_TIMEOUT_MS) {
    rampStopAll();           // host went silent: coast to a stop, don't jerk
  }

  unsigned long nowUs = micros();
  unsigned long elapsed = nowUs - lastControlUs;
  if (elapsed >= CONTROL_PERIOD_US) {
    lastControlUs = nowUs;
    float dt = elapsed * 1e-6f;
    if (dt > 0.01f) dt = 0.01f;   // never take a giant ramp step after a stall
    pollAlarms();
    for (uint8_t i = 0; i < AXIS_COUNT; ++i) updateAxis(axes[i], dt);

    bool moving = anyAxisMoving();
    if (wasMoving && !moving) Serial.println(F("STOPPED"));
    wasMoving = moving;
  }
}
