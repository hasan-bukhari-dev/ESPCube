#include "MouseProfile.h"
#include "../app/RuntimeGlobals.h"

bool MouseProfile::enter() {
    profiles.profile = Profile::MOUSE;
    profiles.screen = UIScreen::MOUSE;
    motion.clearPointerMotion();
    return true;
}

void MouseProfile::exit() {
    hid.releaseMouseButtons();
    motion.clearPointerMotion();
}

void MouseProfile::handleButtonA(bool pressed) {
    hid.setMouseButton(0x01, pressed);
}

void MouseProfile::handleButtonB(bool pressed, uint32_t now) {
    if (pressed) {
        motion.mouseScrollBPressedAt = now;
        motion.mouseScrollGesture = false;
        motion.mouseScrollAccumulator = 0.0f;
        motion.mouseScrollTilt = 0.0f;
        motion.clearPointerMotion();
        return;
    }

    if (!motion.mouseScrollGesture) {
        hid.setMouseButton(0x04, true);
        delay(12);
        hid.setMouseButton(0x04, false);
    } else {
        hid.releaseMouseButtons();
    }

    motion.mouseScrollGesture = false;
    motion.mouseScrollAccumulator = 0.0f;
    motion.mouseScrollTilt = 0.0f;
    motion.mouseScrollBPressedAt = 0;
    motion.clearPointerMotion();
}

void MouseProfile::handleButtonC(bool pressed) {
    hid.setMouseButton(0x02, pressed);
}
