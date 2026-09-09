#include "SpeakerProfile.h"
#include "../app/RuntimeGlobals.h"
#include "../ui/ScreenRenderer.h"
#include "SpeakerStream.h"

static bool speakerHit(int16_t px,int16_t py,int16_t x,int16_t y,int16_t w,int16_t h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

void SpeakerProfile::begin() {
    speakerVolume.begin(100);
    volumePercent = 100;
    muted = false;
}

bool SpeakerProfile::enter() {
    if (!speakerControl.enter()) {
        speakerControl.exitSafe();
        return false;
    }
    profiles.profile = Profile::SPEAKER;
    profiles.screen = UIScreen::MOUSE;
    motion.clearPointerMotion();
    return true;
}

void SpeakerProfile::exit() {
    speakerControl.exitSafe();
    comboSeen = false;
}

bool SpeakerProfile::handleTouch(int16_t x, int16_t y) {
    if (speakerHit(x,y,0,0,74,48)) {
        profiles.goHome();
        return true;
    }
    if (y >= 88 && y <= 138) {
        if (x <= 20) volumePercent = 0;
        else if (x >= 219) volumePercent = 100;
        else {
            volumePercent = static_cast<uint8_t>(constrain(map(x,20,219,0,100),0,100));
        }
        speakerVolume.setPercent(volumePercent);
        renderer.drawSpeakerProfileUI();
        return true;
    }
    if (speakerHit(x,y,20,140,200,82)) {
        muted = !muted;
        speakerVolume.setMuted(muted);
        renderer.drawSpeakerProfileUI();
        return true;
    }
    return false;
}


bool SpeakerProfile::handleButtons(bool leftState, bool middleState, bool rightState, uint32_t now) {
    const bool comboNow = leftState == LOW && rightState == LOW;
    if (comboNow) comboSeen = true;

    if (homeGesture.update(leftState == LOW, rightState == LOW, now)) {
        hid.releaseMouseButtons();
        profiles.goHome();
        Serial.println("[UI] HARD HOME A+C");
        return true;
    }

    if (leftState != buttons.lastLeft && now - buttons.lastLeftChange >= Buttons::DebounceMs) {
        buttons.lastLeftChange = now;
        buttons.lastLeft = leftState;
        if (leftState == HIGH && !comboSeen) {
            volumePercent = volumePercent >= 10 ? volumePercent - 10 : 0;
            speakerVolume.setPercent(volumePercent);
            renderer.drawSpeakerProfileUI();
        }
    }

    if (middleState != buttons.lastMiddle && now - buttons.lastMiddleChange >= Buttons::DebounceMs) {
        buttons.lastMiddleChange = now;
        buttons.lastMiddle = middleState;
        if (middleState == LOW) {
            muted = !muted;
            speakerVolume.setMuted(muted);
            renderer.drawSpeakerProfileUI();
        }
    }

    if (rightState != buttons.lastRight && now - buttons.lastRightChange >= Buttons::DebounceMs) {
        buttons.lastRightChange = now;
        buttons.lastRight = rightState;
        if (rightState == HIGH && !comboSeen) {
            volumePercent = volumePercent <= 90 ? volumePercent + 10 : 100;
            speakerVolume.setPercent(volumePercent);
            renderer.drawSpeakerProfileUI();
        }
    }

    if (leftState == HIGH && rightState == HIGH) comboSeen = false;
    return true;
}
