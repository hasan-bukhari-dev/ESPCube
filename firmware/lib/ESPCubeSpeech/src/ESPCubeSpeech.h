#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

class ESPCubeSpeech
{
public:
    bool begin();

    // S1-B1: ESPCubeSpeech remains sole owner of shared I2S lifecycle.
    bool enterSpeakerPlaybackMode(uint32_t sampleRate);
    bool restoreSpeechMode();
    size_t writeSpeakerPcm(const int16_t *samples, size_t sampleCount);

    bool startRecording();

    // Existing buffer-only capture path.
    void captureTick();

    // Capture one I2S chunk, store it in the existing PSRAM
    // recording buffer, and optionally copy the newly captured
    // mono PCM into dst for live transport.
    size_t captureMono(
        int16_t *dst,
        size_t capacity
    );

    void stopRecording();

    bool isRecording() const;
    size_t sampleCount() const;

    // Read-only pointer used to transmit the 250ms preroll when
    // a B press becomes a confirmed speech hold.
    const int16_t *audioData() const;

    String transcribe();

private:
    static constexpr uint32_t SAMPLE_RATE = 16000;
    static constexpr uint32_t MAX_SECONDS = 15;
    static constexpr size_t MAX_SAMPLES =
        SAMPLE_RATE * MAX_SECONDS;

    I2SClass _i2s;

    int16_t *_audio = nullptr;
    size_t _samples = 0;

    bool _ready = false;
    bool _recording = false;
    bool _speakerMode = false;

    bool connectWifi();
    void disconnectWifi();

    static void writeWavHeader(
        uint8_t *dst,
        uint32_t pcmBytes
    );
};