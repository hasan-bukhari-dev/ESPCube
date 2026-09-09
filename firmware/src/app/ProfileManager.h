#pragma once
#include <Arduino.h>

enum class UIScreen : uint8_t {
    MOUSE, MENU, TEXT_ALPHA, TEXT_GROUP, TEXT_NUMBERS, TEXT_SYMBOLS, SETTINGS
};
enum class Profile : uint8_t { MOUSE, SPEAKER };

class ProfileManager {
public:
    UIScreen screen = UIScreen::MOUSE;
    Profile profile = Profile::MOUSE;

    void updateButtons();
    void updateTouch();
    void handleTouchAction(int16_t x, int16_t y);

    void goHome();
    void openMenu();
    void openTextAlpha();
    void openTextGroup(const char *group);
    void openNumbers();
    void openSymbols();

private:
    bool hit(int16_t px,int16_t py,int16_t x,int16_t y,int16_t w,int16_t h) const;
    bool isTextScreen() const;
    bool isAlphabetScreen() const;
    void shiftTap();
    bool handleTextTopBar(int16_t x,int16_t y);
    bool handleTextDock(int16_t x,int16_t y);
};
