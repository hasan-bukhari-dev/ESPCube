#include "Touch.h"
#include <Wire.h>

volatile bool Touch::pending_ = false;
void IRAM_ATTR Touch::onInterrupt() { pending_ = true; }

bool Touch::readRegister(uint8_t reg, uint8_t *data, size_t len) {
    Wire.beginTransmission(Board::TouchAddress); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    size_t received = Wire.requestFrom((uint8_t)Board::TouchAddress, len, true);
    if (received != len) return false;
    for (size_t i = 0; i < len; ++i) { if (!Wire.available()) return false; data[i] = Wire.read(); }
    return true;
}

bool Touch::readPoint(uint16_t &rawX, uint16_t &rawY, uint8_t &points) {
    uint8_t data[5] = {};
    if (!readRegister(0x02, data, sizeof(data))) return false;
    points = data[0] & 0x0F;
    rawX = ((uint16_t)(data[1] & 0x0F) << 8) | data[2];
    rawY = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];
    return true;
}

void Touch::transform(uint16_t rawX, uint16_t rawY, int16_t &screenX, int16_t &screenY) {
    screenX = 239 - (int16_t)rawY; screenY = (int16_t)rawX;
    screenX = constrain(screenX, 0, 239); screenY = constrain(screenY, 0, 239);
}

void Touch::begin() {
    pinMode(Board::TouchInterrupt, INPUT_PULLUP); pinMode(Board::TouchReset, OUTPUT);
    digitalWrite(Board::TouchReset, HIGH); delay(20); digitalWrite(Board::TouchReset, LOW); delay(5); digitalWrite(Board::TouchReset, HIGH); delay(60);
    attachInterrupt(digitalPinToInterrupt(Board::TouchInterrupt), onInterrupt, FALLING);
    Serial.println("CST816S navigation ready.");
}

bool Touch::poll(TouchPoint &point) {
    if (!pending_) return false;
    noInterrupts(); pending_ = false; interrupts();
    const uint32_t now = millis();
    if (now - lastActionMs_ < ActionCooldownMs) return false;
    uint16_t rawX = 0, rawY = 0; uint8_t points = 0;
    if (!readPoint(rawX, rawY, points) || !points) return false;
    transform(rawX, rawY, point.x, point.y); lastActionMs_ = now; return true;
}
