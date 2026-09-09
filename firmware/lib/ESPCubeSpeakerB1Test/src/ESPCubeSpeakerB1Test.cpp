#include "ESPCubeSpeakerB1Test.h"

#include <ESPCubeSpeech.h>
#include <Wire.h>
#include <math.h>

#include "es8311.h"

namespace
{
static constexpr int PA_CTRL = 7;
static constexpr uint8_t CODEC_ADDR_0 = 0x18;
static constexpr uint8_t CODEC_ADDR_1 = 0x19;

static constexpr uint32_t PLAYBACK_RATE = 32000;
static constexpr uint32_t MCLK_MULTIPLE = 256;
static constexpr uint32_t MCLK_HZ = PLAYBACK_RATE * MCLK_MULTIPLE;

static constexpr float TONE_HZ = 1000.0f;
static constexpr float AMPLITUDE = 0.35f;
static constexpr uint32_t DURATION_MS = 3000;
static constexpr uint32_t FADE_MS = 12;
static constexpr size_t CHUNK_FRAMES = 320; // 10 ms at 32 kHz
static constexpr int CODEC_VOLUME = 55;

static bool probeAddress(uint8_t address)
{
    // Existing main.cpp owns Wire on SDA42/SCL41.
    // Never call Wire.begin() here.
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

static es8311_handle_t createCodec(uint8_t selectedAddress)
{
    if (selectedAddress == CODEC_ADDR_0)
        return es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);

    if (selectedAddress == CODEC_ADDR_1)
        return es8311_create(I2C_NUM_0, ES8311_ADDRESS_1);

    return nullptr;
}

static bool configureCodec(es8311_handle_t handle, ESPCubeSpeakerB1Result &result)
{
    if (!handle)
        return false;

    const es8311_clock_config_t clockConfig = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = (int)MCLK_HZ,
        .sample_frequency = (int)PLAYBACK_RATE
    };

    result.codecInit =
        es8311_init(
            handle,
            &clockConfig,
            ES8311_RESOLUTION_16,
            ES8311_RESOLUTION_16
        ) == ESP_OK;

    if (!result.codecInit)
        return false;

    if (es8311_sample_frequency_config(handle, (int)MCLK_HZ, (int)PLAYBACK_RATE) != ESP_OK)
        return false;

    if (es8311_voice_volume_set(handle, CODEC_VOLUME, nullptr) != ESP_OK)
        return false;

    if (es8311_microphone_config(handle, false) != ESP_OK)
        return false;

    if (es8311_voice_mute(handle, true) != ESP_OK)
        return false;

    result.codecConfigured = true;
    return true;
}

static void safeStopCodec(es8311_handle_t handle)
{
    if (handle)
        es8311_voice_mute(handle, true);

    delay(8);
    digitalWrite(PA_CTRL, LOW);

    if (handle)
        es8311_delete(handle);
}

static float envelopeForFrame(uint32_t frame, uint32_t totalFrames)
{
    const uint32_t fadeFrames = (PLAYBACK_RATE * FADE_MS) / 1000;
    float gain = 1.0f;

    if (fadeFrames > 0 && frame < fadeFrames)
        gain = (float)frame / (float)fadeFrames;

    const uint32_t remaining = totalFrames - frame;
    if (fadeFrames > 0 && remaining <= fadeFrames)
    {
        const float tail = (float)remaining / (float)fadeFrames;
        if (tail < gain)
            gain = tail;
    }

    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    return gain;
}
}

ESPCubeSpeakerB1Result ESPCubeSpeakerB1Test::run(ESPCubeSpeech &speech)
{
    ESPCubeSpeakerB1Result result;

    pinMode(PA_CTRL, OUTPUT);
    digitalWrite(PA_CTRL, LOW);

    result.ack18 = probeAddress(CODEC_ADDR_0);
    result.ack19 = probeAddress(CODEC_ADDR_1);

    if (result.ack18)
        result.selectedAddress = CODEC_ADDR_0;
    else if (result.ack19)
        result.selectedAddress = CODEC_ADDR_1;
    else
        return result;

    // ESPCubeSpeech is the sole owner of I2S mode transitions.
    result.entered32k = speech.enterSpeakerPlaybackMode(PLAYBACK_RATE);
    if (!result.entered32k)
    {
        result.restored16k = speech.restoreSpeechMode();
        return result;
    }

    es8311_handle_t codec = createCodec(result.selectedAddress);
    result.codecCreated = codec != nullptr;

    if (!codec)
    {
        result.restored16k = speech.restoreSpeechMode();
        return result;
    }

    if (!configureCodec(codec, result))
    {
        safeStopCodec(codec);
        result.restored16k = speech.restoreSpeechMode();
        return result;
    }

    int16_t stereo[CHUNK_FRAMES * 2] = {};
    const size_t prefillSamples = CHUNK_FRAMES * 2;
    const size_t prefillExpectedBytes = prefillSamples * sizeof(int16_t);
    const size_t prefillBytes = speech.writeSpeakerPcm(stereo, prefillSamples);

    result.txBytesExpected += prefillExpectedBytes;
    result.txBytesAccepted += prefillBytes;
    result.txPrefillOk = prefillBytes == prefillExpectedBytes;

    if (!result.txPrefillOk)
    {
        safeStopCodec(codec);
        result.restored16k = speech.restoreSpeechMode();
        return result;
    }

    digitalWrite(PA_CTRL, HIGH);
    result.paEnabled = true;
    delay(10);

    if (es8311_voice_mute(codec, false) != ESP_OK)
    {
        safeStopCodec(codec);
        result.restored16k = speech.restoreSpeechMode();
        return result;
    }

    result.toneAttempted = true;

    const uint32_t totalFrames = (PLAYBACK_RATE * DURATION_MS) / 1000;
    const float phaseStep = (2.0f * PI * TONE_HZ) / (float)PLAYBACK_RATE;

    float phase = 0.0f;
    uint32_t producedFrames = 0;
    bool allWritesOk = true;

    while (producedFrames < totalFrames)
    {
        size_t framesThisChunk = CHUNK_FRAMES;
        const uint32_t remaining = totalFrames - producedFrames;
        if (remaining < framesThisChunk)
            framesThisChunk = (size_t)remaining;

        for (size_t i = 0; i < framesThisChunk; ++i)
        {
            const uint32_t absoluteFrame = producedFrames + (uint32_t)i;
            const float env = envelopeForFrame(absoluteFrame, totalFrames);
            const int16_t sample = (int16_t)(sinf(phase) * AMPLITUDE * env * 32767.0f);

            stereo[i * 2] = sample;
            stereo[i * 2 + 1] = sample;

            phase += phaseStep;
            if (phase >= 2.0f * PI)
                phase -= 2.0f * PI;
        }

        const size_t sampleCount = framesThisChunk * 2;
        const size_t expectedBytes = sampleCount * sizeof(int16_t);
        const size_t acceptedBytes = speech.writeSpeakerPcm(stereo, sampleCount);

        result.txBytesExpected += expectedBytes;
        result.txBytesAccepted += acceptedBytes;

        if (acceptedBytes != expectedBytes)
        {
            allWritesOk = false;
            break;
        }

        producedFrames += (uint32_t)framesThisChunk;
    }

    result.txToneOk = allWritesOk && producedFrames == totalFrames;

    memset(stereo, 0, sizeof(stereo));
    speech.writeSpeakerPcm(stereo, prefillSamples);

    safeStopCodec(codec);

    // The defining S1-B1 assertion: return the shared bus to the exact
    // known-good 16 kHz full-duplex speech configuration before setup
    // continues into BLE/HID.
    result.restored16k = speech.restoreSpeechMode();

    return result;
}
