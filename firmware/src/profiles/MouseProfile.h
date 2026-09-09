#pragma once
#include <Arduino.h>
#include "Profile.h"

class MouseProfile : public ProfileLifecycle {
public:
    bool enter() override;
    void exit() override;
    void handleButtonA(bool pressed);
    void handleButtonB(bool pressed, uint32_t now);
    void handleButtonC(bool pressed);
};
