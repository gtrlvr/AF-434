// Two iCL42 motors controlled together by an Arduino Mega 2560 (5 V).
// Motor 1 (LEFT): D9 -> PUL+, D8 -> DIR+.
// Motor 2 (RIGHT): D7 -> PUL+, D6 -> DIR+.
// Both motors: PUL- and DIR- -> Arduino GND; leave ENA+/- unconnected.
// Separate motor supply: 24 V DC -> VCC, supply negative -> motor GND.
// Never connect the 24 V supply to an Arduino pin. See README.md for setup.
// Mirrored mounting: travel forward = motor 1 forward + motor 2 backward.
// Travel backward = motor 1 backward + motor 2 forward.
// Serial: a = travel backward, d = travel forward, s = stop, q/e = speed +/-.
// h = heartbeat; stop both if no valid command arrives for 500 ms.
// ? = report firmware version and Arduino DIR output states.
// Calibration: r flips RIGHT direction and stops (until reset).
// 1/2 = LEFT DIR LOW/HIGH, 3/4 = RIGHT DIR LOW/HIGH; no pulses.

const int MOTOR1_PUL_PIN = 9;
const int MOTOR1_DIR_PIN = 8;
const int MOTOR2_PUL_PIN = 7;
const int MOTOR2_DIR_PIN = 6;

// Each motor's rotation labels for this mounting, calibrated independently.
// HIGH/LOW must be checked against actual rotation and each driver's SW5.
// If one motor's labels are reversed, swap ONLY its forward/backward levels.
const int MOTOR1_FORWARD_DIR_LEVEL = HIGH;
const int MOTOR1_BACKWARD_DIR_LEVEL = LOW;
// After the ENA wiring fix, reverse motor 2 relative to the v7 mapping.
// D: D8 HIGH / D6 LOW. A: D8 LOW / D6 HIGH.
const int MOTOR2_FORWARD_DIR_LEVEL = HIGH;
const int MOTOR2_BACKWARD_DIR_LEVEL = LOW;

const unsigned long PULSE_WIDTH_US = 5;       // iCL42 minimum: 2.5 us
const unsigned long DIR_SETUP_US = 10;        // iCL42 minimum: 5 us
const unsigned long COMMAND_TIMEOUT_MS = 500;
const unsigned long MIN_INTERVAL_US = 60;
const unsigned long MAX_INTERVAL_US = 5000;

// Set BOTH drivers to the same pulses/revolution (see README.md).
// 2000 us = 500 pulses/sec = 18.75 RPM at 1600 pulses/revolution.
unsigned long stepIntervalUs = 2000;
unsigned long lastStepUs = 0;
unsigned long lastCommandMs = 0;
bool running = false;
bool currentTravelForward = true;
bool rightDirectionFlipped = false;

void writeDirection(bool travelForward) {
  if (travelForward) {
    // D: motor 1 forward, motor 2 backward.
    digitalWrite(MOTOR1_DIR_PIN, MOTOR1_FORWARD_DIR_LEVEL);
    digitalWrite(MOTOR2_DIR_PIN, MOTOR2_BACKWARD_DIR_LEVEL);
  } else {
    // A: motor 1 backward, motor 2 forward.
    digitalWrite(MOTOR1_DIR_PIN, MOTOR1_BACKWARD_DIR_LEVEL);
    digitalWrite(MOTOR2_DIR_PIN, MOTOR2_FORWARD_DIR_LEVEL);
  }
  if (rightDirectionFlipped) {
    int configuredLevel = travelForward ? MOTOR2_BACKWARD_DIR_LEVEL : MOTOR2_FORWARD_DIR_LEVEL;
    digitalWrite(MOTOR2_DIR_PIN, configuredLevel == HIGH ? LOW : HIGH);
  }
  currentTravelForward = travelForward;
}

void stopMotors() {
  running = false;
  digitalWrite(MOTOR1_PUL_PIN, LOW);
  digitalWrite(MOTOR2_PUL_PIN, LOW);
}

void reportState() {
  // Arduino pin states only: these do not measure the motor's DIR inputs.
  Serial.print(F("STATE LEFT_D8="));
  Serial.print(digitalRead(MOTOR1_DIR_PIN));
  Serial.print(F(" RIGHT_D6="));
  Serial.print(digitalRead(MOTOR2_DIR_PIN));
  Serial.print(F(" RIGHT_FLIPPED="));
  Serial.print(rightDirectionFlipped);
  Serial.print(F(" RUNNING="));
  Serial.println(running);
}

void testDirectionOutput(int pin, bool high) {
  stopMotors();
  digitalWrite(pin, high ? HIGH : LOW);
  delayMicroseconds(DIR_SETUP_US);
  reportState();
}

void startMotors(bool travelForward) {
  if (!running || travelForward != currentTravelForward) {
    stopMotors();
    writeDirection(travelForward);
    delayMicroseconds(DIR_SETUP_US);
    lastStepUs = micros();
  }
  running = true;
}

void setup() {
  // Preload output levels before enabling the pins.
  digitalWrite(MOTOR1_PUL_PIN, LOW);
  digitalWrite(MOTOR2_PUL_PIN, LOW);
  writeDirection(currentTravelForward);
  pinMode(MOTOR1_PUL_PIN, OUTPUT);
  pinMode(MOTOR1_DIR_PIN, OUTPUT);
  pinMode(MOTOR2_PUL_PIN, OUTPUT);
  pinMode(MOTOR2_DIR_PIN, OUTPUT);
  Serial.begin(115200);
  lastCommandMs = millis();
}

void loop() {
  // Bound serial work so pulses still get serviced if input is continuous.
  for (int i = 0; i < 16 && Serial.available(); ++i) {
    char c = Serial.read();
    switch (c) {
      case 'd':
        startMotors(true);
        reportState();
        break;
      case 'a':
        startMotors(false);
        reportState();
        break;
      case 's':
        stopMotors();
        break;
      case 'q':
        stepIntervalUs = stepIntervalUs * 8 / 10;
        if (stepIntervalUs < MIN_INTERVAL_US) stepIntervalUs = MIN_INTERVAL_US;
        break;
      case 'e':
        stepIntervalUs = stepIntervalUs * 10 / 8;
        if (stepIntervalUs > MAX_INTERVAL_US) stepIntervalUs = MAX_INTERVAL_US;
        break;
      case 'h':
        break;  // Keep the connection alive; never restart a stopped motor.
      case '?':
        Serial.println(F("AF434-LR-DIR-v8"));
        reportState();
        continue;  // A version query must not keep motion alive.
      case 'r':
        stopMotors();
        rightDirectionFlipped = !rightDirectionFlipped;
        writeDirection(currentTravelForward);
        delayMicroseconds(DIR_SETUP_US);
        reportState();
        continue;
      case '1':
        testDirectionOutput(MOTOR1_DIR_PIN, false);
        continue;
      case '2':
        testDirectionOutput(MOTOR1_DIR_PIN, true);
        continue;
      case '3':
        testDirectionOutput(MOTOR2_DIR_PIN, false);
        continue;
      case '4':
        testDirectionOutput(MOTOR2_DIR_PIN, true);
        continue;
      default:
        continue;
    }
    lastCommandMs = millis();
  }

  if (running && millis() - lastCommandMs >= COMMAND_TIMEOUT_MS) {
    stopMotors();
  }

  unsigned long nowUs = micros();
  if (running && nowUs - lastStepUs >= stepIntervalUs) {
    lastStepUs = nowUs;
    // Every step gives BOTH motors one pulse, with a few us between edges.
    digitalWrite(MOTOR1_PUL_PIN, HIGH);
    digitalWrite(MOTOR2_PUL_PIN, HIGH);
    delayMicroseconds(PULSE_WIDTH_US);
    digitalWrite(MOTOR1_PUL_PIN, LOW);
    digitalWrite(MOTOR2_PUL_PIN, LOW);
  }
}
