#include "MouseProfile.h"
#include "../app/RuntimeGlobals.h"

namespace
{
constexpr uint8_t HID_KEY_C = 0x06;
constexpr uint8_t HID_KEY_V = 0x19;
constexpr uint8_t HID_MOD_LEFT_CTRL = 0x01;
}

bool MouseProfile::enter() {
    profiles.profile = Profile::MOUSE;
    profiles.screen = UIScreen::MOUSE;

    aPending = false;
    cPending = false;
    aMouseDown = false;
    cMouseDown = false;
    copyLatched = false;
    pasteLatched = false;
    consumeARelease = false;
    consumeBRelease = false;
    consumeCRelease = false;

    motion.clearPointerMotion();
    return true;
}

void MouseProfile::exit() {
    hid.releaseMouseButtons();
    hid.releaseKeyboard();

    aPending = false;
    cPending = false;
    aMouseDown = false;
    cMouseDown = false;
    copyLatched = false;
    pasteLatched = false;
    consumeARelease = false;
    consumeBRelease = false;
    consumeCRelease = false;

    cancelScroll();
}

void MouseProfile::cancelScroll() {
    motion.mouseScrollGesture = false;
    motion.mouseScrollAccumulator = 0.0f;
    motion.mouseScrollTilt = 0.0f;
    motion.mouseScrollBPressedAt = 0;
    motion.clearPointerMotion();
}

void MouseProfile::releaseOwnedMouseButtons() {
    hid.releaseMouseButtons();
    aMouseDown = false;
    cMouseDown = false;
}

void MouseProfile::updateChords(
    bool aDown,
    bool bDown,
    bool cDown,
    uint32_t now
) {
    const bool copyChord =
        aDown && bDown && !cDown;

    const bool pasteChord =
        bDown && cDown && !aDown;

    if (copyChord && !copyLatched) {
        copyLatched = true;
        consumeARelease = true;
        consumeBRelease = true;
        aPending = false;

        releaseOwnedMouseButtons();
        cancelScroll();

        hid.keyTap(0x06, 0x01);

        Serial.println("[MOUSE] A+B -> COPY");
    }

    if (!aDown || !bDown) {
        copyLatched = false;
    }

    if (pasteChord && !pasteLatched) {
        pasteLatched = true;
        consumeBRelease = true;
        consumeCRelease = true;
        cPending = false;

        releaseOwnedMouseButtons();
        cancelScroll();

        hid.keyTap(0x19, 0x01);

        Serial.println("[MOUSE] B+C -> PASTE");
    }

    if (!bDown || !cDown) {
        pasteLatched = false;
    }

    if (copyChord || pasteChord) {
        cancelScroll();
    }

    if (
        aPending &&
        !consumeARelease &&
        aDown &&
        !bDown &&
        !cDown &&
        now - aPendingAt >= ChordGraceMs
    ) {
        aPending = false;
        hid.setMouseButton(0x01, true);
        aMouseDown = true;
    }

    if (
        cPending &&
        !consumeCRelease &&
        cDown &&
        !aDown &&
        !bDown &&
        now - cPendingAt >= ChordGraceMs
    ) {
        cPending = false;
        hid.setMouseButton(0x02, true);
        cMouseDown = true;
    }
}

void MouseProfile::handleButtonA(bool pressed, uint32_t now) {
    if (consumeARelease) {
        if (!pressed) {
            consumeARelease = false;
            aPending = false;
        }
        return;
    }

    if (pressed) {
        aPending = true;
        aPendingAt = now;
        return;
    }

    if (aMouseDown) {
        hid.setMouseButton(0x01, false);
        aMouseDown = false;
    }
    else if (aPending) {
        hid.setMouseButton(0x01, true);
        delay(12);
        hid.setMouseButton(0x01, false);
    }

    aPending = false;
}

void MouseProfile::handleButtonB(bool pressed, uint32_t now) {
    if (consumeBRelease) {
        if (!pressed) {
            consumeBRelease = false;
            cancelScroll();
        }
        return;
    }

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
    }
    else {
        hid.releaseMouseButtons();
    }

    cancelScroll();
}

void MouseProfile::handleButtonC(bool pressed, uint32_t now) {
    if (consumeCRelease) {
        if (!pressed) {
            consumeCRelease = false;
            cPending = false;
        }
        return;
    }

    if (pressed) {
        cPending = true;
        cPendingAt = now;
        return;
    }

    if (cMouseDown) {
        hid.setMouseButton(0x02, false);
        cMouseDown = false;
    }
    else if (cPending) {
        hid.setMouseButton(0x02, true);
        delay(12);
        hid.setMouseButton(0x02, false);
    }

    cPending = false;
}
