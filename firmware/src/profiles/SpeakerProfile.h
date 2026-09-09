#pragma once
#include <stdint.h>
#include <Arduino.h>
#include "Profile.h"
class SpeakerProfile : public ProfileLifecycle {
public:
    uint8_t volumePercent = 100;
    bool muted = false;
    bool comboSeen = false;

    void begin();
    bool enter() override;
    void exit() override;
    bool handleTouch(int16_t x, int16_t y);
    bool handleButtons(bool leftState, bool middleState, bool rightState, uint32_t now);
};
