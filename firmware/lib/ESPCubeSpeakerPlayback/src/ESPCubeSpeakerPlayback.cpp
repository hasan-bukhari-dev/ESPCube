#include "ESPCubeSpeakerPlayback.h"

#include <ESPCubeSpeech.h>
#include <Wire.h>

namespace
{
static constexpr uint32_t MCLK_HZ =
    ESPCubeSpeakerPlayback::SAMPLE_RATE * 256;
}

uint8_t ESPCubeSpeakerPlayback::probeCodecAddress()
{
    // main.cpp remains sole owner of Wire.begin().
    Wire.beginTransmission(
        CODEC_ADDR_0
    );

    if (Wire.endTransmission() == 0)
        return CODEC_ADDR_0;

    Wire.beginTransmission(
        CODEC_ADDR_1
    );

    if (Wire.endTransmission() == 0)
        return CODEC_ADDR_1;

    return 0;
}

es8311_handle_t ESPCubeSpeakerPlayback::createCodec(
    uint8_t address
)
{
    if (address == CODEC_ADDR_0)
    {
        return es8311_create(
            I2C_NUM_0,
            ES8311_ADDRESS_0
        );
    }

    if (address == CODEC_ADDR_1)
    {
        return es8311_create(
            I2C_NUM_0,
            ES8311_ADDRESS_1
        );
    }

    return nullptr;
}

bool ESPCubeSpeakerPlayback::configureCodec()
{
    if (!_codec)
        return false;

    const es8311_clock_config_t clockConfig =
    {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = (int)MCLK_HZ,
        .sample_frequency = (int)SAMPLE_RATE
    };

    if (
        es8311_init(
            _codec,
            &clockConfig,
            ES8311_RESOLUTION_16,
            ES8311_RESOLUTION_16
        ) != ESP_OK
    ) {
        return false;
    }

    if (
        es8311_sample_frequency_config(
            _codec,
            (int)MCLK_HZ,
            (int)SAMPLE_RATE
        ) != ESP_OK
    ) {
        return false;
    }

    if (
        es8311_voice_volume_set(
            _codec,
            CODEC_VOLUME,
            nullptr
        ) != ESP_OK
    ) {
        return false;
    }

    if (
        es8311_microphone_config(
            _codec,
            false
        ) != ESP_OK
    ) {
        return false;
    }

    if (
        es8311_voice_mute(
            _codec,
            true
        ) != ESP_OK
    ) {
        return false;
    }

    return true;
}

void ESPCubeSpeakerPlayback::stopCodecHardware()
{
    if (_codec)
    {
        es8311_voice_mute(
            _codec,
            true
        );
    }

    delay(8);

    digitalWrite(
        PA_CTRL,
        LOW
    );

    if (_codec)
    {
        es8311_delete(
            _codec
        );

        _codec = nullptr;
    }
}

bool ESPCubeSpeakerPlayback::begin(
    ESPCubeSpeech &speech
)
{
    if (_active)
        return true;

    _speech =
        &speech;

    _lastRestoreOk =
        false;

    pinMode(
        PA_CTRL,
        OUTPUT
    );

    digitalWrite(
        PA_CTRL,
        LOW
    );

    const uint8_t codecAddress =
        probeCodecAddress();

    if (codecAddress == 0)
    {
        _speech = nullptr;
        return false;
    }

    if (
        !_speech->enterSpeakerPlaybackMode(
            SAMPLE_RATE
        )
    ) {
        _lastRestoreOk =
            _speech->restoreSpeechMode();

        _speech = nullptr;

        return false;
    }

    _codec =
        createCodec(
            codecAddress
        );

    if (!_codec)
    {
        _lastRestoreOk =
            _speech->restoreSpeechMode();

        _speech = nullptr;

        return false;
    }

    if (!configureCodec())
    {
        stopCodecHardware();

        _lastRestoreOk =
            _speech->restoreSpeechMode();

        _speech = nullptr;

        return false;
    }

    // Same 10 ms stereo silence prefill proven by S1-B1.
    int16_t silence[
        CHUNK_FRAMES * 2
    ] = {};

    const size_t sampleCount =
        CHUNK_FRAMES * 2;

    const size_t expectedBytes =
        sampleCount *
        sizeof(int16_t);

    const size_t acceptedBytes =
        _speech->writeSpeakerPcm(
            silence,
            sampleCount
        );

    if (
        acceptedBytes !=
        expectedBytes
    ) {
        stopCodecHardware();

        _lastRestoreOk =
            _speech->restoreSpeechMode();

        _speech = nullptr;

        return false;
    }

    digitalWrite(
        PA_CTRL,
        HIGH
    );

    delay(10);

    if (
        es8311_voice_mute(
            _codec,
            false
        ) != ESP_OK
    ) {
        stopCodecHardware();

        _lastRestoreOk =
            _speech->restoreSpeechMode();

        _speech = nullptr;

        return false;
    }

    _active =
        true;

    return true;
}

size_t ESPCubeSpeakerPlayback::writeMono(
    const int16_t *samples,
    size_t sampleCount
)
{
    if (
        !_active ||
        !_speech ||
        !_codec ||
        !samples ||
        sampleCount == 0
    ) {
        return 0;
    }

    int16_t stereo[
        CHUNK_FRAMES * 2
    ];

    size_t consumed =
        0;

    while (
        consumed <
        sampleCount
    ) {
        size_t frames =
            sampleCount -
            consumed;

        if (
            frames >
            CHUNK_FRAMES
        ) {
            frames =
                CHUNK_FRAMES;
        }

        for (
            size_t i = 0;
            i < frames;
            ++i
        ) {
            const int16_t sample =
                samples[
                    consumed + i
                ];

            stereo[
                i * 2
            ] = sample;

            stereo[
                i * 2 + 1
            ] = sample;
        }

        const size_t stereoSamples =
            frames * 2;

        const size_t expectedBytes =
            stereoSamples *
            sizeof(int16_t);

        const size_t acceptedBytes =
            _speech->writeSpeakerPcm(
                stereo,
                stereoSamples
            );

        if (
            acceptedBytes !=
            expectedBytes
        ) {
            break;
        }

        consumed +=
            frames;
    }

    return consumed;
}

bool ESPCubeSpeakerPlayback::end()
{
    if (!_speech)
    {
        stopCodecHardware();

        _active =
            false;

        return false;
    }

    // Flush a final 10 ms of silence before muting the codec.
    if (_active)
    {
        int16_t silence[
            CHUNK_FRAMES * 2
        ] = {};

        _speech->writeSpeakerPcm(
            silence,
            CHUNK_FRAMES * 2
        );
    }

    stopCodecHardware();

    _active =
        false;

    // Preserve the exact defining S1-B1 invariant.
    _lastRestoreOk =
        _speech->restoreSpeechMode();

    _speech =
        nullptr;

    return _lastRestoreOk;
}