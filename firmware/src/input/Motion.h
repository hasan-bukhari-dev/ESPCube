#pragma once

#include <Arduino.h>
#include <QMI8658.h>

class Buttons;
class ProfileManager;
class ESPCubeHID;

class Motion
{
public:
    bool begin();
    void update(const Buttons &buttons, const ProfileManager &profiles, ESPCubeHID &hid);
    void clearPointerMotion();

    bool stationary = false;
    uint32_t goodSamples = 0;
    uint32_t rejectedSamples = 0;
    float sensitivityMultiplier = 1.0f;
    bool gyroEnabled = true;
    bool invertY = false;
    uint32_t mouseScrollBPressedAt = 0;
    bool mouseScrollGesture = false;
    float mouseScrollAccumulator = 0.0f;
    float mouseScrollTilt = 0.0f;

private:
    static float median3(float a, float b, float c);
    void addHistory(float y, float z);
    static float softDeadzone(float value, float dz);
    static float adaptiveFilter(float raw, float previous);
    static float accelerationFor(float speed);
    static bool sampleValid(float gx, float gy, float gz);
    void enterRest(float gy, float gz);
    void exitRest();

    QMI8658 imu;
    float biasY = 0.0f;
    float biasZ = 0.0f;
    float prevRawY = 0.0f;
    float prevRawZ = 0.0f;
    bool havePrevious = false;
    float histY[3] = {0, 0, 0};
    float histZ[3] = {0, 0, 0};
    uint8_t historyIndex = 0;
    uint8_t historyCount = 0;
    int restScore = 0;
    uint32_t quietSinceMs = 0;
    float filteredX = 0.0f;
    float filteredY = 0.0f;
    float remainderX = 0.0f;
    float remainderY = 0.0f;
    uint32_t lastSampleUs = 0;
};
