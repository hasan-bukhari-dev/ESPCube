#include "SettingsProfile.h"
#include "../app/RuntimeGlobals.h"
#include "../ui/ScreenRenderer.h"

static bool settingsHit(int16_t px,int16_t py,int16_t x,int16_t y,int16_t w,int16_t h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

bool SettingsProfile::handleTouch(int16_t x, int16_t y) {
    if (settingsHit(x,y,18,70,50,42)) {
        motion.sensitivityMultiplier -= 0.10f;
        motion.sensitivityMultiplier = constrain(motion.sensitivityMultiplier,0.50f,2.00f);
        renderer.renderCurrentScreen();
        return true;
    }
    if (settingsHit(x,y,172,70,50,42)) {
        motion.sensitivityMultiplier += 0.10f;
        motion.sensitivityMultiplier = constrain(motion.sensitivityMultiplier,0.50f,2.00f);
        renderer.renderCurrentScreen();
        return true;
    }
    if (settingsHit(x,y,20,126,200,38)) {
        motion.gyroEnabled = !motion.gyroEnabled;
        motion.clearPointerMotion();
        renderer.renderCurrentScreen();
        return true;
    }
    if (settingsHit(x,y,20,173,200,38)) {
        motion.invertY = !motion.invertY;
        motion.clearPointerMotion();
        renderer.renderCurrentScreen();
        return true;
    }
    return false;
}
