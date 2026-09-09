#pragma once

#include <Arduino.h>

#include "es8311.h"

class ESPCubeSpeech;

class ESPCubeSpeakerPlayback
{
public:
    static constexpr uint32_t SAMPLE_RATE = 32000;

    ESPCubeSpeakerPlayback() = default;

    bool begin(
        ESPCubeSpeech &speech
    );

    size_t writeMono(
        const int16_t *samples,
        size_t sampleCount
    );

    bool end();

    bool active() const
    {
        return _active;
    }

    bool lastRestoreOk() const
    {
        return _lastRestoreOk;
    }

private:
    static constexpr int PA_CTRL = 7;

    static constexpr uint8_t CODEC_ADDR_0 = 0x18;
    static constexpr uint8_t CODEC_ADDR_1 = 0x19;

    static constexpr uint32_t MCLK_MULTIPLE = 256;

    static constexpr size_t CHUNK_FRAMES = 320;

    static constexpr int CODEC_VOLUME = 80;

    ESPCubeSpeech *_speech = nullptr;

    es8311_handle_t _codec = nullptr;

    bool _active = false;
    bool _lastRestoreOk = false;

    uint8_t probeCodecAddress();
    es8311_handle_t createCodec(
        uint8_t address
    );

    bool configureCodec();

    void stopCodecHardware();
};