#include "ESPCubeSpeakerPcmRingBuffer.h"

#include <esp_heap_caps.h>
#include <string.h>

ESPCubeSpeakerPcmRingBuffer::~ESPCubeSpeakerPcmRingBuffer()
{
    release();
}

bool ESPCubeSpeakerPcmRingBuffer::begin(
    size_t capacityBytes
)
{
    if (
        _data != nullptr &&
        _capacity == capacityBytes
    ) {
        clear();
        return true;
    }

    release();

    if (capacityBytes == 0) {
        return false;
    }

    _data = static_cast<uint8_t *>(
        heap_caps_malloc(
            capacityBytes,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        )
    );

    if (_data == nullptr) {
        _capacity = 0;
        return false;
    }

    _capacity = capacityBytes;
    clear();

    return true;
}

void ESPCubeSpeakerPcmRingBuffer::clear()
{
    _head = 0;
    _tail = 0;
    _size = 0;
    _highWater = 0;
}

void ESPCubeSpeakerPcmRingBuffer::release()
{
    if (_data != nullptr) {
        heap_caps_free(_data);
        _data = nullptr;
    }

    _capacity = 0;
    clear();
}

size_t ESPCubeSpeakerPcmRingBuffer::push(
    const uint8_t *src,
    size_t count
)
{
    if (
        _data == nullptr ||
        src == nullptr ||
        count == 0
    ) {
        return 0;
    }

    if (count > freeBytes()) {
        count = freeBytes();
    }

    size_t written = 0;

    while (written < count) {
        const size_t untilEnd =
            _capacity - _head;

        const size_t chunk =
            min(
                count - written,
                untilEnd
            );

        memcpy(
            _data + _head,
            src + written,
            chunk
        );

        _head =
            (_head + chunk) %
            _capacity;

        _size += chunk;
        written += chunk;
    }

    if (_size > _highWater) {
        _highWater = _size;
    }

    return written;
}

size_t ESPCubeSpeakerPcmRingBuffer::pop(
    uint8_t *dst,
    size_t count
)
{
    if (
        _data == nullptr ||
        dst == nullptr ||
        count == 0
    ) {
        return 0;
    }

    if (count > _size) {
        count = _size;
    }

    size_t read = 0;

    while (read < count) {
        const size_t untilEnd =
            _capacity - _tail;

        const size_t chunk =
            min(
                count - read,
                untilEnd
            );

        memcpy(
            dst + read,
            _data + _tail,
            chunk
        );

        _tail =
            (_tail + chunk) %
            _capacity;

        _size -= chunk;
        read += chunk;
    }

    return read;
}
