#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

class ESPCubeSpeechLink
{
public:
    static constexpr uint8_t PROTOCOL_VERSION = 1;
    static constexpr uint32_t SAMPLE_RATE = 16000;

    // 20 ms of 16 kHz audio.
    static constexpr size_t FRAME_SAMPLES = 320;

    static constexpr const char *SERVICE_UUID =
        "45535043-5542-4c45-8000-000000000001";

    static constexpr const char *CONTROL_UUID =
        "45535043-5542-4c45-8000-000000000002";

    static constexpr const char *STATUS_UUID =
        "45535043-5542-4c45-8000-000000000003";

    static constexpr const char *AUDIO_UUID =
        "45535043-5542-4c45-8000-000000000004";

    bool begin(
        NimBLEServer *server
    );

    void startSession();

    void pushPcm(
        const int16_t *samples,
        size_t count
    );

    void endSession();

    // Keep companion discoverability alive after HID connects.
    // Also handles the one-time reboot requested after a brand
    // new Windows pairing/bond completes.
    void maintain();

    bool isSessionActive() const;

    uint32_t sentFrames() const;
    uint32_t droppedFrames() const;

private:
    NimBLEService *_service =
        nullptr;

    NimBLECharacteristic *_control =
        nullptr;

    NimBLECharacteristic *_status =
        nullptr;

    NimBLECharacteristic *_audio =
        nullptr;

    bool _active =
        false;

    int16_t _frame[
        FRAME_SAMPLES
    ];

    size_t _frameFill =
        0;

    uint16_t _sequence =
        0;

    int _adpcmIndex =
        0;

    uint32_t _sentFrames =
        0;

    uint32_t _droppedFrames =
        0;

    uint32_t _totalSamples =
        0;

    void sendControl(
        const String &message
    );

    void sendStatus();

    void sendFrame(
        const int16_t *samples,
        size_t count
    );
};