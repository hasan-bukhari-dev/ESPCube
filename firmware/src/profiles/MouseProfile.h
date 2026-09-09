#pragma once
#include <Arduino.h>
#include "Profile.h"

class MouseProfile : public ProfileLifecycle {
public:
    static constexpr uint32_t ChordGraceMs = 55;

    bool enter() override;
    void exit() override;

    void updateChords(bool aDown, bool bDown, bool cDown, uint32_t now);
    void handleButtonA(bool pressed, uint32_t now);
    void handleButtonB(bool pressed, uint32_t now);
    void handleButtonC(bool pressed, uint32_t now);

private:
    bool aPending = false;
    bool cPending = false;
    bool aMouseDown = false;
    bool cMouseDown = false;

    bool copyLatched = false;
    bool pasteLatched = false;

    bool consumeARelease = false;
    bool consumeBRelease = false;
    bool consumeCRelease = false;

    uint32_t aPendingAt = 0;
    uint32_t cPendingAt = 0;

    void cancelScroll();
    void releaseOwnedMouseButtons();
};
