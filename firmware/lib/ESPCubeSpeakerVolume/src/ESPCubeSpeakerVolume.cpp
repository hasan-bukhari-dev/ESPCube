#include "ESPCubeSpeakerVolume.h"

#include <math.h>

void ESPCubeSpeakerVolume::begin(uint8_t percent)
{
    _percent = constrain(percent, 0, 100);
    _muted = false;
    _currentQ15 = targetQ15();
}

void ESPCubeSpeakerVolume::setPercent(uint8_t percent)
{
    _percent = constrain(percent, 0, 100);
}

uint8_t ESPCubeSpeakerVolume::percent() const
{
    return _percent;
}

void ESPCubeSpeakerVolume::setMuted(bool muted)
{
    _muted = muted;
}

bool ESPCubeSpeakerVolume::muted() const
{
    return _muted;
}

int32_t ESPCubeSpeakerVolume::targetQ15() const
{
    if (_muted || _percent == 0)
        return 0;

    if (_percent >= 100)
        return 32768;

    const float t =
        static_cast<float>(_percent) / 100.0f;

    const float oneMinus =
        1.0f - t;

    // Perceptual attenuation:
    // 25% ~= -22.5 dB
    // 50% ~= -10 dB
    // 75% ~= -2.5 dB
    // 100% = 0 dB
    const float db =
        -40.0f * oneMinus * oneMinus;

    const float gain =
        powf(10.0f, db / 20.0f);

    int32_t q15 =
        static_cast<int32_t>(
            lroundf(gain * 32768.0f)
        );

    if (q15 < 0)
        q15 = 0;
    else if (q15 > 32768)
        q15 = 32768;

    return q15;
}

void ESPCubeSpeakerVolume::apply(
    int16_t *samples,
    size_t count
)
{
    if (
        samples == nullptr ||
        count == 0
    )
    {
        return;
    }

    const int32_t target =
        targetQ15();

    const int32_t start =
        _currentQ15;

    for (size_t i = 0; i < count; ++i)
    {
        const int64_t delta =
            static_cast<int64_t>(
                target - start
            ) *
            static_cast<int64_t>(
                i + 1
            );

        const int32_t gainQ15 =
            start +
            static_cast<int32_t>(
                delta /
                static_cast<int64_t>(count)
            );

        int32_t v =
            static_cast<int32_t>(
                (
                    static_cast<int64_t>(samples[i]) *
                    static_cast<int64_t>(gainQ15)
                ) >> 15
            );

        if (v > 32767)
            v = 32767;
        else if (v < -32768)
            v = -32768;

        samples[i] =
            static_cast<int16_t>(v);
    }

    _currentQ15 =
        target;
}
