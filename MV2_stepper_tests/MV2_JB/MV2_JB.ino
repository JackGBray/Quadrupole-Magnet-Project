// This code is for simply reading out the MV2 magnetometer data through digital spi.

// This connects to a python script via serial
// May 1st 2026, Jack


#include <SPI.h>

// From Metrolab MV2Hal.h
#define NDIGITAL_ANALOG_PIN 8
#define D_DR_PIN 2
#define D_INIT_PIN 7

#if defined(__AVR_ATmega328P__)     // Arduino Uno
  #define D_CHIP_SELECT_PIN 10
#else
  #error "Unknown board. Check MV2 pin definitions."
#endif

#define MV2_SPI_CLK_FREQ 1000000

uint16_t spiWriteRead16(uint16_t data) {
  SPI.beginTransaction(SPISettings(MV2_SPI_CLK_FREQ, MSBFIRST, SPI_MODE0));

  digitalWrite(D_CHIP_SELECT_PIN, LOW);
  uint16_t result = SPI.transfer16(data);
  digitalWrite(D_CHIP_SELECT_PIN, HIGH);

  SPI.endTransaction();

  return result;
}

bool waitForDataReady(unsigned long timeout_ms = 5) {
  unsigned long start = millis();

  while (digitalRead(D_DR_PIN) == LOW) {
    if (millis() - start > timeout_ms) {
      return false;
    }
  }

  return true;
}

float rawToMilliTesla(uint16_t raw, int axis) {
  // For digital mode, +/-300 mT range.
  // Uses typical datasheet sensitivity:
  // X/Y = 91.2 LSB/mT, Z = 98.5 LSB/mT.
  // Assumes zero-field code is 32768; actual zero may differ due to digital offset.
  // Approximate scaling: 0 -> -300 mT, 32768 -> 0 mT, 65535 -> +300 mT.
  float sensitivity;

  if (axis == 0 || axis == 1) {
    sensitivity = 27.6f;  
  } else if (axis == 2) {
    sensitivity = 29.5f;  
  } else {
    return 0.0f;
  }
  return ((float)raw - 32768.0f) / sensitivity;
}

void flushMV2Sample() {
  if (!waitForDataReady(20)) return;

  spiWriteRead16(0x2C09); // Bx returned, select By
  spiWriteRead16(0x2C0A); // By returned, select Bz
  spiWriteRead16(0x2C0B); // Bz returned, select Temp
  spiWriteRead16(0x2C08); // Temp returned, select Bx
}

void configureMV2Digital() {
  pinMode(NDIGITAL_ANALOG_PIN, OUTPUT);
  pinMode(D_DR_PIN, INPUT);
  pinMode(D_INIT_PIN, OUTPUT);
  pinMode(D_CHIP_SELECT_PIN, OUTPUT);

  // Digital mode: nDIGITAL_ANALOG = LOW
  digitalWrite(NDIGITAL_ANALOG_PIN, LOW);

  // CS inactive
  digitalWrite(D_CHIP_SELECT_PIN, HIGH);

  // INIT low
  digitalWrite(D_INIT_PIN, LOW);

  SPI.begin();
  delay(10);

  // These commands are from MV2DigitalScript.xml
  spiWriteRead16(0x2C08); // Register 0: 3-axis, 14-bit, +/-1000 mT, select Bx
  spiWriteRead16(0x2D02); // Register 1
  spiWriteRead16(0x2E08); // Register 2

  delay(10);
}

void setup() {
  Serial.begin(115200);
  configureMV2Digital();

  Serial.println("t_ms,Bx_raw,By_raw,Bz_raw,T_raw,Bx_mT,By_mT,Bz_mT,Bmag_mT");
}

void loop() {
  if (!waitForDataReady()) {
    Serial.println("# DR_TIMEOUT");
    return;
  }

  // Pipelined reads from MV2DigitalScript.xml:
  // send select next channel, receive previous channel.
  uint16_t rawBx = spiWriteRead16(0x2C09); // read Bx, select By
  uint16_t rawBy = spiWriteRead16(0x2C0A); // read By, select Bz
  uint16_t rawBz = spiWriteRead16(0x2C0B); // read Bz, select Temp
  uint16_t rawT  = spiWriteRead16(0x2C08); // read Temp, select Bx

  float bx = rawToMilliTesla(rawBx, 0);
  float by = rawToMilliTesla(rawBy, 1);
  float bz = rawToMilliTesla(rawBz, 2);
  float bmag = sqrt(bx * bx + by * by + bz * bz);

  Serial.print(millis());
  Serial.print(",");
  Serial.print(rawBx);
  Serial.print(",");
  Serial.print(rawBy);
  Serial.print(",");
  Serial.print(rawBz);
  Serial.print(",");
  Serial.print(rawT);
  Serial.print(",");
  Serial.print(bx, 4);
  Serial.print(",");
  Serial.print(by, 4);
  Serial.print(",");
  Serial.print(bz, 4);
  Serial.print(",");
  Serial.println(bmag, 4);

  delay(10);
}