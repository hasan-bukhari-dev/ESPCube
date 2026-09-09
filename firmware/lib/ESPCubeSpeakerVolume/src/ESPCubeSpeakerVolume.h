#pragma once

#include <Arduino.h>

class ESPCubeSpeakerVolume
{
public:
    void begin(uint8_t percent = 100);

    // 100 == frozen D2C9 maximum. Never boosts above unity.
    void setPercent(uint8_t percent);
    uint8_t percent() const;

    void setMuted(bool muted);
    bool muted() const;

    // In-place PCM16 attenuation with a short gain ramp to prevent clicks.
    void apply(int16_t *samples, size_t count);

private:
    uint8_t _percent = 100;
    bool _muted = false;
    int32_t _currentQ15 = 32768;

    int32_t targetQ15() const;
};
