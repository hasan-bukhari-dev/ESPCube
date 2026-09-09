#pragma once

#include <Arduino.h>

class ESPCubeSpeech;

struct ESPCubeSpeakerB1Result
{
    bool ack18 = false;
    bool ack19 = false;
    uint8_t selectedAddress = 0;
    bool entered32k = false;
    bool codecCreated = false;
    bool codecInit = false;
    bool codecConfigured = false;
    bool txPrefillOk = false;
    bool toneAttempted = false;
    bool txToneOk = false;
    bool paEnabled = false;
    bool restored16k = false;
    size_t txBytesAccepted = 0;
    size_t txBytesExpected = 0;
};

class ESPCubeSpeakerB1Test
{
public:
    static ESPCubeSpeakerB1Result run(ESPCubeSpeech &speech);
};
