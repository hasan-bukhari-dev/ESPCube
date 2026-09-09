#include "HomeGesture.h"
bool HomeGesture::update(bool aPressed, bool cPressed, uint32_t now) {
    if (!(aPressed && cPressed)) { since = 0; triggeredFlag = false; return false; }
    if (!since) since = now;
    if (!triggeredFlag && now - since >= HoldMs) { triggeredFlag = true; return true; }
    return false;
}
