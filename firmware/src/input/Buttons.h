#pragma once
#include <Arduino.h>
#include "../hardware/BoardConfig.h"
struct ButtonSnapshot { bool left; bool middle; bool right; };
class Buttons {
public:
    static constexpr uint32_t DebounceMs = 18;
    void begin();
    ButtonSnapshot sample() const;
    bool lastLeft = HIGH, lastMiddle = HIGH, lastRight = HIGH;
    uint32_t lastLeftChange = 0, lastMiddleChange = 0, lastRightChange = 0;
};
