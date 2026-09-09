#pragma once

#include <Arduino.h>

class ESPCubeSpeakerPcmRingBuffer
{
public:
    ESPCubeSpeakerPcmRingBuffer() = default;
    ~ESPCubeSpeakerPcmRingBuffer();

    ESPCubeSpeakerPcmRingBuffer(
        const ESPCubeSpeakerPcmRingBuffer &
    ) = delete;

    ESPCubeSpeakerPcmRingBuffer &operator=(
        const ESPCubeSpeakerPcmRingBuffer &
    ) = delete;

    bool begin(size_t capacityBytes);
    void clear();
    void release();

    size_t push(
        const uint8_t *src,
        size_t count
    );

    size_t pop(
        uint8_t *dst,
        size_t count
    );

    size_t size() const
    {
        return _size;
    }

    size_t capacity() const
    {
        return _capacity;
    }

    size_t freeBytes() const
    {
        return _capacity - _size;
    }

    size_t highWater() const
    {
        return _highWater;
    }

    bool ready() const
    {
        return _data != nullptr;
    }

private:
    uint8_t *_data = nullptr;

    size_t _capacity = 0;
    size_t _head = 0;
    size_t _tail = 0;
    size_t _size = 0;
    size_t _highWater = 0;
};
