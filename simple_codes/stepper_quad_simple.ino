#include <AccelStepper.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ===============================
// Pin definitions
// Change these to suit your wiring
// ===============================
const uint8_t NUM_MOTORS = 4;   // change depending on number of motors used
const uint8_t MAX_MOTORS = 4;   // array capacity

uint8_t STEP_PINS[MAX_MOTORS];
uint8_t DIR_PINS[MAX_MOTORS];
uint8_t EN_PINS[MAX_MOTORS];

float motorStepsPerRev[MAX_MOTORS];
float microsteps[MAX_MOTORS];
float gearRatio[MAX_MOTORS];
float maxSpeedSteps[MAX_MOTORS];
float accelSteps[MAX_MOTORS];

AccelStepper* steppers[MAX_MOTORS]; // pointers to motor objects

// ===============================
// Serial input buffer
// ===============================
const uint16_t CMD_BUF_LEN = 96;
char cmdBuffer[CMD_BUF_LEN];
uint16_t cmdIndex = 0;

// ===============================
// Utility functions
// ===============================
float outputStepsPerRev(uint8_t m) {
  return motorStepsPerRev[m] * microsteps[m] * gearRatio[m];
}

long degreesToSteps(uint8_t m, float degrees) {
  return lround((degrees / 360.0) * outputStepsPerRev(m));
}

float stepsToDegrees(uint8_t m, long steps) {
  return (360.0 * steps) / outputStepsPerRev(m);
}

bool isAllToken(const char* s) {
  return (strcmp(s, "all") == 0 || strcmp(s, "ALL") == 0);
}

bool parseMotorToken(const char* s, int &motorIndex, bool &allSelected) {
  allSelected = false;
  motorIndex = -1;

  if (isAllToken(s)) {
    allSelected = true;
    return true;
  }

  int val = atoi(s);
  if (val >= 1 && val <= NUM_MOTORS) {
    motorIndex = val - 1;   // user enters 1..NUM_MOTORS
    return true;
  }

  return false;
}

void applyMotionSettings(uint8_t m) {
  steppers[m]->setMaxSpeed(maxSpeedSteps[m]);
  steppers[m]->setAcceleration(accelSteps[m]);
}

void enableMotor(uint8_t m, bool enable) {
  if (EN_PINS[m] == 255) return;

  // IMPORTANT:
  // TB6600 enable polarity varies by board/wiring.
  // This code assumes:
  // LOW = enabled, HIGH = disabled
  // If yours behaves opposite, invert these writes.
  digitalWrite(EN_PINS[m], enable ? HIGH : LOW);
}

void enableAll(bool enable) {
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    enableMotor(i, enable);
  }
}

void printMotorStatus(uint8_t m) {
  Serial.print(F("Motor "));
  Serial.print(m + 1);
  Serial.print(F(": pos_steps="));
  Serial.print(steppers[m]->currentPosition());
  Serial.print(F(", pos_deg="));
  Serial.print(stepsToDegrees(m, steppers[m]->currentPosition()), 3);
  Serial.print(F(", target_steps="));
  Serial.print(steppers[m]->targetPosition());
  Serial.print(F(", target_deg="));
  Serial.print(stepsToDegrees(m, steppers[m]->targetPosition()), 3);
  Serial.print(F(", motor_steps_rev="));
  Serial.print(motorStepsPerRev[m], 3);
  Serial.print(F(", microsteps="));
  Serial.print(microsteps[m], 3);
  Serial.print(F(", gear="));
  Serial.print(gearRatio[m], 3);
  Serial.print(F(", output_steps_rev="));
  Serial.print(outputStepsPerRev(m), 3);
  Serial.print(F(", max_speed="));
  Serial.print(maxSpeedSteps[m], 3);
  Serial.print(F(", accel="));
  Serial.println(accelSteps[m], 3);
}

void printStatusAll() {
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    printMotorStatus(i);
  }
}

void printHelp() {
  Serial.println(F("\nCommands:"));
  Serial.println(F("  HELP"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  STATUS <motor|all>"));
  Serial.println(F("  EN <on|off>"));
  Serial.println(F("  ZERO <motor|all>"));
  Serial.println(F("  STOP <motor|all>"));
  Serial.println(F("  MOVEDEG <motor|all> <deg>        ; relative move in output degrees"));
  Serial.println(F("  GOTO <motor|all> <deg>           ; absolute move in output degrees"));
  Serial.println(F("  MOVESTEP <motor|all> <steps>     ; relative move in input steps"));
  Serial.println(F("  GOTOSTEP <motor|all> <steps>     ; absolute move in input steps"));
  Serial.println(F("  SETSPR <motor|all> <steps/rev>   ; motor native full steps/rev"));
  Serial.println(F("  SETMICRO <motor|all> <micro>     ; e.g. 1, 2, 4, 8, 16"));
  Serial.println(F("  SETGEAR <motor|all> <ratio>      ; e.g. 50.0"));
  Serial.println(F("  SPEED <motor|all> <steps/sec>"));
  Serial.println(F("  ACCEL <motor|all> <steps/sec^2>"));
  Serial.println(F("\nExamples:"));
  Serial.println(F("  MOVEDEG 1 90"));
  Serial.println(F("  MOVEDEG all 45"));
  Serial.println(F("  GOTO 2 180"));
  Serial.println(F("  SETSPR all 200"));
  Serial.println(F("  SETMICRO all 8"));
  Serial.println(F("  SETGEAR all 50"));
  Serial.println(F("  SPEED 1 1200"));
  Serial.println(F("  ZERO all"));
  Serial.println();
}

void moveRelativeDegrees(uint8_t m, float deg) {
  long delta = degreesToSteps(m, deg);
  steppers[m]->move(delta);
}

void moveAbsoluteDegrees(uint8_t m, float deg) {
  long target = degreesToSteps(m, deg);
  steppers[m]->moveTo(target);
}

void moveRelativeSteps(uint8_t m, long steps) {
  steppers[m]->move(steps);
}

void moveAbsoluteSteps(uint8_t m, long steps) {
  steppers[m]->moveTo(steps);
}

void stopMotor(uint8_t m) {
  steppers[m]->stop();
}

void zeroMotor(uint8_t m) {
  steppers[m]->setCurrentPosition(0);
}

// ===============================
// Command processing
// ===============================
void processCommand(char* line) {
  char* token = strtok(line, " \t\r\n");
  if (!token) return;

  // Uppercase command in place
  for (char* p = token; *p; p++) *p = toupper(*p);

  if (strcmp(token, "HELP") == 0) {
    printHelp();
    return;
  }

  if (strcmp(token, "STATUS") == 0) {
    char* arg1 = strtok(NULL, " \t\r\n");
    if (!arg1) {
      printStatusAll();
      return;
    }

    int motorIndex;
    bool allSelected;
    if (!parseMotorToken(arg1, motorIndex, allSelected)) {
      Serial.print(F("ERR: motor must be 1.."));
      Serial.print(NUM_MOTORS);
      Serial.println(F(" or all"));
      return;
    }

    if (allSelected) printStatusAll();
    else printMotorStatus((uint8_t)motorIndex);
    return;
  }

  if (strcmp(token, "EN") == 0) {
    char* arg1 = strtok(NULL, " \t\r\n");
    if (!arg1) {
      Serial.println(F("ERR: use EN on or EN off"));
      return;
    }

    for (char* p = arg1; *p; p++) *p = tolower(*p);

    if (strcmp(arg1, "on") == 0) {
      enableAll(true);
      Serial.println(F("OK: motors enabled"));
    } else if (strcmp(arg1, "off") == 0) {
      enableAll(false);
      Serial.println(F("OK: motors disabled"));
    } else {
      Serial.println(F("ERR: use EN on or EN off"));
    }
    return;
  }

  if (strcmp(token, "ZERO") == 0 ||
      strcmp(token, "STOP") == 0 ||
      strcmp(token, "MOVEDEG") == 0 ||
      strcmp(token, "GOTO") == 0 ||
      strcmp(token, "MOVESTEP") == 0 ||
      strcmp(token, "GOTOSTEP") == 0 ||
      strcmp(token, "SETSPR") == 0 ||
      strcmp(token, "SETMICRO") == 0 ||
      strcmp(token, "SETGEAR") == 0 ||
      strcmp(token, "SPEED") == 0 ||
      strcmp(token, "ACCEL") == 0) {

    char* arg1 = strtok(NULL, " \t\r\n");
    if (!arg1) {
      Serial.println(F("ERR: missing motor argument"));
      return;
    }

    int motorIndex;
    bool allSelected;
    if (!parseMotorToken(arg1, motorIndex, allSelected)) {
      Serial.print(F("ERR: motor must be 1.."));
      Serial.print(NUM_MOTORS);
      Serial.println(F(" or all"));
      return;
    }

    if (strcmp(token, "ZERO") == 0) {
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) zeroMotor(i);
      } else {
        zeroMotor((uint8_t)motorIndex);
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "STOP") == 0) {
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) stopMotor(i);
      } else {
        stopMotor((uint8_t)motorIndex);
      }
      Serial.println(F("OK"));
      return;
    }

    char* arg2 = strtok(NULL, " \t\r\n");
    if (!arg2) {
      Serial.println(F("ERR: missing value"));
      return;
    }

    if (strcmp(token, "MOVEDEG") == 0) {
      float val = atof(arg2);
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) moveRelativeDegrees(i, val);
      } else {
        moveRelativeDegrees((uint8_t)motorIndex, val);
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "GOTO") == 0) {
      float val = atof(arg2);
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) moveAbsoluteDegrees(i, val);
      } else {
        moveAbsoluteDegrees((uint8_t)motorIndex, val);
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "MOVESTEP") == 0) {
      long val = atol(arg2);
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) moveRelativeSteps(i, val);
      } else {
        moveRelativeSteps((uint8_t)motorIndex, val);
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "GOTOSTEP") == 0) {
      long val = atol(arg2);
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) moveAbsoluteSteps(i, val);
      } else {
        moveAbsoluteSteps((uint8_t)motorIndex, val);
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "SETSPR") == 0) {
      float val = atof(arg2);
      if (val <= 0) {
        Serial.println(F("ERR: steps/rev must be > 0"));
        return;
      }
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) motorStepsPerRev[i] = val;
      } else {
        motorStepsPerRev[motorIndex] = val;
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "SETMICRO") == 0) {
      float val = atof(arg2);
      if (val <= 0) {
        Serial.println(F("ERR: microsteps must be > 0"));
        return;
      }
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) microsteps[i] = val;
      } else {
        microsteps[motorIndex] = val;
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "SETGEAR") == 0) {
      float val = atof(arg2);
      if (val <= 0) {
        Serial.println(F("ERR: gear ratio must be > 0"));
        return;
      }
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) gearRatio[i] = val;
      } else {
        gearRatio[motorIndex] = val;
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "SPEED") == 0) {
      float val = atof(arg2);
      if (val <= 0) {
        Serial.println(F("ERR: speed must be > 0"));
        return;
      }
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) {
          maxSpeedSteps[i] = val;
          applyMotionSettings(i);
        }
      } else {
        maxSpeedSteps[motorIndex] = val;
        applyMotionSettings((uint8_t)motorIndex);
      }
      Serial.println(F("OK"));
      return;
    }

    if (strcmp(token, "ACCEL") == 0) {
      float val = atof(arg2);
      if (val <= 0) {
        Serial.println(F("ERR: accel must be > 0"));
        return;
      }
      if (allSelected) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) {
          accelSteps[i] = val;
          applyMotionSettings(i);
        }
      } else {
        accelSteps[motorIndex] = val;
        applyMotionSettings((uint8_t)motorIndex);
      }
      Serial.println(F("OK"));
      return;
    }
  }

  Serial.println(F("ERR: unknown command. Type HELP"));
}

// ===============================
// Setup / loop
// ===============================
void setup() {
  Serial.begin(115200);
  delay(3000);

  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    STEP_PINS[i] = 2 * i + 2;   // 2, 4, 6, 8
    DIR_PINS[i]  = 2 * i + 3;   // 3, 5, 7, 9
    EN_PINS[i]   = i + 10;      // 10, 11, 12, 13

    motorStepsPerRev[i] = 200;
    microsteps[i]       = 4;
    gearRatio[i]        = 50.0;

    maxSpeedSteps[i] = 800.0;
    accelSteps[i]    = 400.0;

    steppers[i] = new AccelStepper(
      AccelStepper::DRIVER,
      STEP_PINS[i],
      DIR_PINS[i]
    );

    if (EN_PINS[i] != 255) {
      pinMode(EN_PINS[i], OUTPUT);
    }

    applyMotionSettings(i);
    steppers[i]->setCurrentPosition(0);
  }

  enableAll(true);

  //Serial.println(F("Motors ready"));
  //printHelp();
}

void loop() {
  // Keep motors running
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    steppers[i]->run();
  }

  // Read serial commands line-by-line
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmdIndex > 0) {
        cmdBuffer[cmdIndex] = '\0';
        processCommand(cmdBuffer);
        cmdIndex = 0;
      }
    } else {
      if (cmdIndex < CMD_BUF_LEN - 1) {
        cmdBuffer[cmdIndex++] = c;
      }
    }
  }
}