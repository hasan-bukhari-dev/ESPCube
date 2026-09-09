#pragma once
#include <Arduino.h>
namespace Board {
constexpr uint8_t ButtonA = 0; constexpr uint8_t ButtonB = 5; constexpr uint8_t ButtonC = 4;
constexpr uint8_t I2cSda = 42; constexpr uint8_t I2cScl = 41;
constexpr uint8_t TouchReset = 47; constexpr uint8_t TouchInterrupt = 48; constexpr uint8_t TouchAddress = 0x15;
constexpr uint8_t LcdCs = 21; constexpr uint8_t LcdClock = 38; constexpr uint8_t LcdMosi = 39;
constexpr uint8_t LcdReset = 40; constexpr uint8_t LcdDc = 45; constexpr uint8_t LcdBacklight = 46;
constexpr uint16_t DisplayWidth = 240; constexpr uint16_t DisplayHeight = 240;
}
