#pragma once
#include <Arduino.h>
#include "../hardware/BoardConfig.h"
struct TouchPoint { int16_t x; int16_t y; };
class Touch {
public:
    static constexpr uint32_t ActionCooldownMs = 220;
    void begin();
    bool poll(TouchPoint &point);
    static void IRAM_ATTR onInterrupt();
private:
    bool readRegister(uint8_t reg, uint8_t *data, size_t len);
    bool readPoint(uint16_t &rawX, uint16_t &rawY, uint8_t &points);
    static void transform(uint16_t rawX, uint16_t rawY, int16_t &screenX, int16_t &screenY);
    static volatile bool pending_;
    uint32_t lastActionMs_ = 0;
};
