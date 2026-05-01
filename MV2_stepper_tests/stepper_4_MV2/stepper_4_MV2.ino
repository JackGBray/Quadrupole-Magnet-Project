#include <AccelStepper.h>

#define NUM_MOTORS 4

#define STEPS_PER_REV 200
#define MICROSTEP 4
#define GEAR_RATIO 50.0
#define STEPS_PER_DEG (STEPS_PER_REV * MICROSTEP * GEAR_RATIO / 360.0)


const uint8_t STEP_PINS[NUM_MOTORS] = {2, 4, 6, 8};
const uint8_t DIR_PINS[NUM_MOTORS]  = {3, 5, 7, 9};

// This makes all the motors go the same way
const int MOTOR_DIR_SIGN[NUM_MOTORS] = {
  1,   // motor 1 normal
  1,   // motor 2 normal
 -1,   // motor 3 flipped
 -1    // motor 4 flipped
};

AccelStepper steppers[NUM_MOTORS] = {
  AccelStepper(AccelStepper::DRIVER, STEP_PINS[0], DIR_PINS[0]),
  AccelStepper(AccelStepper::DRIVER, STEP_PINS[1], DIR_PINS[1]),
  AccelStepper(AccelStepper::DRIVER, STEP_PINS[2], DIR_PINS[2]),
  AccelStepper(AccelStepper::DRIVER, STEP_PINS[3], DIR_PINS[3])
};

// -------- Helpers --------

long degToSteps(int motorIndex, float deg) {
  return (long)(deg * STEPS_PER_DEG * MOTOR_DIR_SIGN[motorIndex]);
}

bool anyMotorRunning() {
  for (int i = 0; i < NUM_MOTORS; i++) {
    if (steppers[i].distanceToGo() != 0) return true;
  }
  return false;
}

// -------- Setup --------

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_MOTORS; i++) {
    steppers[i].setMaxSpeed(4000);
    steppers[i].setAcceleration(2000);
  }

  Serial.println("Stepper ready");
}

// -------- Command handler --------

void processCommand(String cmd) {
  cmd.trim();

  if (cmd == "ZERO") {
    for (int i = 0; i < NUM_MOTORS; i++) {
      steppers[i].setCurrentPosition(0);
      steppers[i].moveTo(0); // safety thing
    }
    Serial.println("OK");
    return;
  }

  if (cmd == "BUSY") {
    Serial.println(anyMotorRunning() ? "BUSY" : "IDLE");
    return;
  }

  if (cmd.startsWith("GOTO")) {
    int a = cmd.indexOf(' ');
    int b = cmd.indexOf(' ', a + 1);
    if (b == -1) { Serial.println("ERR"); return; }

    String target = cmd.substring(a + 1, b);
    float angle = cmd.substring(b + 1).toFloat();
    // long steps = degToSteps(angle);

    if (target == "all") {
      for (int i = 0; i < NUM_MOTORS; i++) {
        steppers[i].moveTo(degToSteps(i, angle));
      }
      Serial.println("OK");
      return;
    }

    int motor = target.toInt();
    if (motor >= 1 && motor <= NUM_MOTORS) {
      steppers[motor - 1].moveTo(degToSteps(motor-1, angle));
      Serial.println("OK");
      return;
    }

    Serial.println("ERR");
    return;
  }

  if (cmd.startsWith("MOVE")) {
    int a = cmd.indexOf(' ');
    int b = cmd.indexOf(' ', a + 1);
    if (b == -1) { Serial.println("ERR"); return; }

    String target = cmd.substring(a + 1, b);
    float delta = cmd.substring(b + 1).toFloat();
    // long dsteps = degToSteps(delta);

    if (target == "all") {
      for (int i = 0; i < NUM_MOTORS; i++) {
        steppers[i].move(degToSteps(i, delta));
      }
      Serial.println("OK");
      return;
    }

    int motor = target.toInt();
    if (motor >= 1 && motor <= NUM_MOTORS) {
      steppers[motor - 1].move(degToSteps(motor-1, delta));
      Serial.println("OK");
      return;
    }

    Serial.println("ERR");
    return;
  }

  Serial.println("ERR");
}

// -------- Loop --------

String inputBuffer = "";

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processCommand(inputBuffer);
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }

  for (int i = 0; i < NUM_MOTORS; i++) {
    steppers[i].run();
  }
}