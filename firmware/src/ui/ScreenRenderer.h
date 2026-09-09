#pragma once
#include <Arduino.h>
class ScreenRenderer {
public:
    void centerText(const char *text, int16_t cx, int16_t y, uint8_t size, uint16_t color);
    void button(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, uint8_t size = 1);
    void drawMicScreen();
    void drawHome();
    const char *profileName();
    void drawSpeakerProfileUI();
    void drawStaticUI();
    void drawStatus();
    void drawMenu();
    void drawKeyboardTop();
    void drawKeyboardDock(bool allowShift);
    void drawErgoChar(int16_t x, int16_t y, int16_t w, int16_t h, char c);
    void drawTextAlpha();
    void drawTextGroup();
    void drawNumbers();
    void drawThreeSymbols(const char *symbols);
    void drawFourSymbols(const char *symbols);
    void drawSymbols();
    void drawSettings();
    void renderCurrentScreen();
    void updateDisplay();
};
extern ScreenRenderer renderer;
