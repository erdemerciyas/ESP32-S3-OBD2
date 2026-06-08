#pragma once
#include <Wire.h>

#define TCA9554_ADDRESS       0x20
#define TCA9554_INPUT_REG     0x00
#define TCA9554_OUTPUT_REG    0x01
#define TCA9554_POLARITY_REG  0x02
#define TCA9554_CONFIG_REG    0x03

#define EXIO_LOW              0
#define EXIO_HIGH             1
#define EXIO_PIN1             1   // LCD Reset
#define EXIO_PIN2             2   // Touch Reset
#define EXIO_PIN3             3   // LCD CS (SPI config)
#define EXIO_PIN4             4
#define EXIO_PIN5             5
#define EXIO_PIN6             6
#define EXIO_PIN7             7
#define EXIO_PIN8             8   // LCD Power Enable

inline uint8_t TCA9554_ReadReg(uint8_t reg) {
    Wire.beginTransmission(TCA9554_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)TCA9554_ADDRESS, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

inline void TCA9554_WriteReg(uint8_t reg, uint8_t data) {
    Wire.beginTransmission(TCA9554_ADDRESS);
    Wire.write(reg);
    Wire.write(data);
    Wire.endTransmission();
}

inline void TCA9554_Init() {
    TCA9554_WriteReg(TCA9554_CONFIG_REG, 0x00);  // All pins as output
}

inline void TCA9554_SetPin(uint8_t pin, uint8_t state) {
    if (pin < 1 || pin > 8) return;
    uint8_t current = TCA9554_ReadReg(TCA9554_OUTPUT_REG);
    if (state == EXIO_HIGH)
        current |= (1 << (pin - 1));
    else
        current &= ~(1 << (pin - 1));
    TCA9554_WriteReg(TCA9554_OUTPUT_REG, current);
}
