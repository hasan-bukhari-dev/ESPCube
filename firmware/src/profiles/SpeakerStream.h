#pragma once
#include <Arduino.h>
#include <ESPCubeSpeakerPcmRingBuffer.h>

class SpeakerStream {
public:
    static constexpr size_t BufferCapacity = 128 * 1024;
    static constexpr size_t PrebufferBytes = 12 * 320 * sizeof(int16_t);
    static constexpr uint32_t UnderrunGraceMs = 14;
    static constexpr uint16_t TcpPort = 47821;
    static constexpr size_t PcmFrames = 320;
    static constexpr size_t PcmBytes = PcmFrames * sizeof(int16_t);

    void serviceControl();
    void serviceTcp();
    void serviceD2A();
    void stopPlayback();

    // Runtime metrics intentionally remain inspectable for diagnostics.
    uint32_t d2aTcpBytesRx = 0;
    uint32_t d2aPcmBytesPlayed = 0;
    uint32_t d2aChunksPlayed = 0;
    uint32_t d2aBufferUnderruns = 0;
    uint32_t d2aBufferOverflows = 0;
    uint32_t speakerTcpBytesPlayed = 0;
    uint32_t speakerTcpChunksPlayed = 0;

private:
    ESPCubeSpeakerPcmRingBuffer speakerPcmBuffer;
    bool d2aInputEnded = false;
    bool d2aPlaybackStarted = false;
    bool d2aUnderrunLatched = false;
    uint32_t d2aLastWriteMs = 0;

    int speakerTcpServer = -1;
    int speakerTcpClient = -1;
    bool speakerTcpListening = false;
    uint8_t speakerTcpRx[PcmBytes] = {};
    size_t speakerTcpRxFill = 0;

    void closeSpeakerTcpClient(bool sendResult);
    void stopSpeakerTcpServer();
    bool startSpeakerTcpServer();
    void acceptSpeakerTcpClient();
    bool playSpeakerTcpChunk();
    void receiveSpeakerTcpAudio();
    void resetSpeakerD2ASession();
    bool ensureSpeakerD2ABuffer();
    void closeSpeakerD2AClientOnly();
    void finishSpeakerD2AStream();
    bool acceptSpeakerD2AClient();
    void drainSpeakerD2ATcpIntoRing();
    bool maybeStartSpeakerD2APlayback();
    void playOneSpeakerD2AChunk();
};
extern SpeakerStream speakerStream;
