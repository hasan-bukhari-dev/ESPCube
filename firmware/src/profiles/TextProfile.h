#pragma once
#include <Arduino.h>

class TextProfile {
public:
    static constexpr uint32_t SpeechHoldMs = 250;
    static constexpr uint32_t ShiftDoubleTapMs = 450;

    const char *selectedGroup = nullptr;
    bool shiftOnce = false;
    bool capsLock = false;
    bool t9SecondPage = false;
    uint32_t speechBPressedAt = 0;
    bool speechBPending = false;
    bool speechMicActive = false;
    bool speechCaptureStarted = false;
    uint32_t lastShiftTapMs = 0;
    char textPreview[22] = {0};
    uint8_t textPreviewLen = 0;

    void previewPush(char c);
    void previewDelete();
    void sendCharacter(char c);
    void sendBackspace();
    void sendSpace();
    void sendEnter();
    void typeSpeechTranscript(const String &text);
};
