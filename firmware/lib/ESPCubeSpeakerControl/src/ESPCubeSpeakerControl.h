#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

enum class SpeakerState : uint8_t {
    Off = 0,
    Entering,
    Ready,
    Error
};

enum class SpeakerCommand : uint8_t {
    None = 0,
    Enter,
    Exit,
    Status
};

enum class SpeakerWifiState : uint8_t {
    Off = 0,
    Configured,
    Connecting,
    Connected,
    Failed,
    Timeout,
    NoCredentials,
    NotReady
};

class ESPCubeSpeakerControl : private NimBLECharacteristicCallbacks {
public:
    static constexpr const char *SERVICE_UUID =
        "45535043-5542-4c45-8100-000000000001";

    static constexpr const char *COMMAND_UUID =
        "45535043-5542-4c45-8100-000000000002";

    static constexpr const char *STATUS_UUID =
        "45535043-5542-4c45-8100-000000000003";

    bool begin(NimBLEServer *server);
    void maintain();

    bool enter();
    void exitSafe();

    SpeakerCommand consumeCommand();

    SpeakerState state() const {
        return _state;
    }

    bool isReady() const {
        return _state == SpeakerState::Ready;
    }

    const char *stateName() const;
    const char *wifiStateName() const;

    void publishStatus();

private:
    static constexpr int PA_CTRL = 7;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

    // S2-C1 UDP receive/count/discard proof.
    static constexpr uint16_t UDP_PORT = 47820;
    static constexpr size_t UDP_HEADER_BYTES = 12;
    static constexpr uint8_t UDP_PROTOCOL_VERSION = 1;
    static constexpr uint8_t UDP_PACKET_TYPE_TEST = 1;

    NimBLEService *_service = nullptr;
    NimBLECharacteristic *_command = nullptr;
    NimBLECharacteristic *_status = nullptr;

    volatile uint8_t _pending =
        static_cast<uint8_t>(SpeakerCommand::None);

    SpeakerState _state =
        SpeakerState::Off;

    SpeakerWifiState _wifiState =
        SpeakerWifiState::Off;

    String _wifiSsid;
    String _wifiPassword;

    bool _wifiConnectRequested = false;
    bool _wifiClearRequested = false;
    bool _statusDirty = false;

    uint32_t _wifiConnectStartedMs = 0;

    int _udpSocket = -1;
    bool _udpActive = false;

    static constexpr int UDP_RX_BUFFER_BYTES = 32768;
    static constexpr size_t UDP_MAX_DATAGRAM_BYTES = 1536;

    // S2-C4: dedicated UDP receive task.
    TaskHandle_t _udpTaskHandle = nullptr;
    volatile bool _udpTaskRun = false;

    uint32_t _udpRxPackets = 0;
    uint32_t _udpRxBytes = 0;
    uint32_t _udpInvalidPackets = 0;
    uint32_t _udpSequenceErrors = 0;
    uint32_t _udpLastSequence = 0;
    uint32_t _udpExpectedSequence = 0;
    bool _udpHaveSequence = false;

    // S2-C2: throttle BLE status publication during UDP receive.
    uint32_t _udpLastStatusPublishMs = 0;
    static constexpr uint32_t UDP_STATUS_INTERVAL_MS = 250;
void forceHardwareSafe();
    void setState(SpeakerState next);

    String buildStatus() const;

    void startWifiConnection();
    void shutdownWifi(bool clearCredentials);
    void clearWifiCredentials();
    void setWifiState(SpeakerWifiState next);

    bool startUdp();
    void stopUdp();
    void pollUdp();
    static void udpTaskThunk(void *arg);
    void udpTaskLoop();
    bool startUdpTask();
    void stopUdpTask();
    void resetUdpCounters();


    void handleJsonCommand(const String &raw);
    void markStatusDirty();

    void onWrite(
        NimBLECharacteristic *characteristic,
        NimBLEConnInfo &connInfo
    ) override;
};