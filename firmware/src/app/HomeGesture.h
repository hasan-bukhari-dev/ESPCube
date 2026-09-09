#pragma once
#include <Arduino.h>
class HomeGesture {
public:
    static constexpr uint32_t HoldMs = 800;
    bool update(bool aPressed, bool cPressed, uint32_t now);
    bool triggered() const { return triggeredFlag; }
    uint32_t since = 0;
    bool triggeredFlag = false;
};
