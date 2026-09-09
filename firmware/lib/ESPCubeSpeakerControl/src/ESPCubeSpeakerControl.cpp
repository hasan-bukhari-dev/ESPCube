#include "ESPCubeSpeakerControl.h"

#include <ArduinoJson.h>

namespace {

String rawString(const NimBLEAttValue &value) {
    String s;
    s.reserve(value.size());

    const uint8_t *p = value.data();

    for (size_t i = 0; i < value.size(); ++i) {
        s += static_cast<char>(p[i]);
    }

    s.trim();
    return s;
}

String upperCommand(const String &raw) {
    String s = raw;
    s.toUpperCase();
    return s;
}

}

bool ESPCubeSpeakerControl::begin(NimBLEServer *server) {
    forceHardwareSafe();

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    shutdownWifi(true);

    _state = SpeakerState::Off;
    _wifiState = SpeakerWifiState::Off;

    _pending =
        static_cast<uint8_t>(SpeakerCommand::None);

    _wifiConnectRequested = false;
    _wifiClearRequested = false;
    _statusDirty = false;

    if (!server) {
        _state = SpeakerState::Error;
        return false;
    }

    _service =
        server->createService(SERVICE_UUID);

    if (!_service) {
        _state = SpeakerState::Error;
        return false;
    }

    _command =
        _service->createCharacteristic(
            COMMAND_UUID,
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::WRITE_NR
        );

    _status =
        _service->createCharacteristic(
            STATUS_UUID,
            NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::NOTIFY
        );

    if (!_command || !_status) {
        _state = SpeakerState::Error;
        forceHardwareSafe();
        shutdownWifi(true);
        return false;
    }

    _command->setCallbacks(this);

    publishStatus();

    return true;
}

void ESPCubeSpeakerControl::maintain() {
    // --------------------------------------------------------
    // S2-B1:
    // Wi-Fi is permitted only while Speaker is READY.
    // PA / UDP / stream / PCM playback remain disabled.
    // --------------------------------------------------------

    if (_state != SpeakerState::Ready) {
        if (
            _wifiState != SpeakerWifiState::Off ||
            !_wifiSsid.isEmpty() ||
            !_wifiPassword.isEmpty()
        ) {
            shutdownWifi(true);
            _wifiState = SpeakerWifiState::Off;
            markStatusDirty();
        }

        _wifiConnectRequested = false;
        _wifiClearRequested = false;
    }

    if (_wifiClearRequested) {
        _wifiClearRequested = false;

        shutdownWifi(true);
        setWifiState(SpeakerWifiState::Off);
    }

    if (_wifiConnectRequested) {
        _wifiConnectRequested = false;
        startWifiConnection();
    }

    if (_wifiState == SpeakerWifiState::Connecting) {
        const wl_status_t status =
            WiFi.status();

        if (status == WL_CONNECTED) {
            setWifiState(
                SpeakerWifiState::Connected
            );

            // S2-D1: UDP transport retired.
            // TCP playback is serviced from main.cpp.
        }
        else if (
            status == WL_CONNECT_FAILED ||
            status == WL_NO_SSID_AVAIL
        ) {
            shutdownWifi(false);
            setWifiState(
                SpeakerWifiState::Failed
            );
        }
        else if (
            millis() - _wifiConnectStartedMs >=
            WIFI_CONNECT_TIMEOUT_MS
        ) {
            shutdownWifi(false);
            setWifiState(
                SpeakerWifiState::Timeout
            );
        }
    }
    else if (
        _wifiState == SpeakerWifiState::Connected &&
        WiFi.status() != WL_CONNECTED
    ) {
        shutdownWifi(false);
        setWifiState(
            SpeakerWifiState::Failed
        );
    }

    if (
        _state == SpeakerState::Ready &&
        _wifiState == SpeakerWifiState::Connected &&
        WiFi.status() == WL_CONNECTED
    ) {
        if (!_udpActive) {
            // S2-D1: UDP transport retired.
            // TCP playback is serviced from main.cpp.
        }

        if (_udpActive) {
            const uint32_t now = millis();

            if (
                now - _udpLastStatusPublishMs >=
                UDP_STATUS_INTERVAL_MS
            ) {
                _udpLastStatusPublishMs = now;
                markStatusDirty();
            }
        }
    }
    else if (_udpActive) {
        stopUdp();
    }

    if (_statusDirty) {
        _statusDirty = false;
        publishStatus();
    }
}

bool ESPCubeSpeakerControl::enter() {
    forceHardwareSafe();

    // Every Speaker entry begins with networking disabled.
    shutdownWifi(true);

    _wifiState =
        SpeakerWifiState::Off;

    _wifiConnectRequested = false;
    _wifiClearRequested = false;

    setState(
        SpeakerState::Entering
    );

    setState(
        SpeakerState::Ready
    );

    return true;
}

void ESPCubeSpeakerControl::exitSafe() {
    // HARD TEARDOWN ORDER:
    // 1. amplifier safe
    // 2. Wi-Fi off
    // 3. credentials gone
    // 4. pending work gone
    // 5. state OFF

    forceHardwareSafe();

    shutdownWifi(true);

    _wifiState =
        SpeakerWifiState::Off;

    _wifiConnectRequested = false;
    _wifiClearRequested = false;

    _pending =
        static_cast<uint8_t>(
            SpeakerCommand::None
        );

    setState(
        SpeakerState::Off
    );
}

SpeakerCommand ESPCubeSpeakerControl::consumeCommand() {
    const uint8_t value =
        _pending;

    _pending =
        static_cast<uint8_t>(
            SpeakerCommand::None
        );

    return static_cast<SpeakerCommand>(
        value
    );
}

const char *ESPCubeSpeakerControl::stateName() const {
    switch (_state) {
        case SpeakerState::Off:
            return "OFF";

        case SpeakerState::Entering:
            return "ENTERING";

        case SpeakerState::Ready:
            return "READY";

        case SpeakerState::Error:
            return "ERROR";
    }

    return "ERROR";
}

const char *ESPCubeSpeakerControl::wifiStateName() const {
    switch (_wifiState) {
        case SpeakerWifiState::Off:
            return "OFF";

        case SpeakerWifiState::Configured:
            return "CONFIGURED";

        case SpeakerWifiState::Connecting:
            return "CONNECTING";

        case SpeakerWifiState::Connected:
            return "CONNECTED";

        case SpeakerWifiState::Failed:
            return "FAILED";

        case SpeakerWifiState::Timeout:
            return "TIMEOUT";

        case SpeakerWifiState::NoCredentials:
            return "NO_CREDENTIALS";

        case SpeakerWifiState::NotReady:
            return "NOT_READY";
    }

    return "OFF";
}

void ESPCubeSpeakerControl::publishStatus() {
    if (!_status) {
        return;
    }

    const String s =
        buildStatus();

    _status->setValue(
        s.c_str()
    );

    _status->notify();
}

void ESPCubeSpeakerControl::forceHardwareSafe() {
    pinMode(
        PA_CTRL,
        OUTPUT
    );

    digitalWrite(
        PA_CTRL,
        LOW
    );
}

void ESPCubeSpeakerControl::setState(
    SpeakerState next
) {
    _state = next;
    publishStatus();
}

void ESPCubeSpeakerControl::setWifiState(
    SpeakerWifiState next
) {
    if (_wifiState == next) {
        return;
    }

    _wifiState = next;
    markStatusDirty();
}

void ESPCubeSpeakerControl::markStatusDirty() {
    _statusDirty = true;
}

void ESPCubeSpeakerControl::clearWifiCredentials() {
    _wifiSsid = "";
    _wifiPassword = "";
}

void ESPCubeSpeakerControl::shutdownWifi(
    bool clearCredentials
) {
    // Speaker teardown ordering:
    // PA is already forced LOW by exitSafe/start paths.
    // Stop UDP before disabling the Wi-Fi radio.
    stopUdp();

    WiFi.setAutoReconnect(false);

    WiFi.disconnect(
        true,
        false
    );

    WiFi.mode(
        WIFI_OFF
    );

    _wifiConnectStartedMs = 0;

    if (clearCredentials) {
        clearWifiCredentials();
    }
}

void ESPCubeSpeakerControl::startWifiConnection() {
    forceHardwareSafe();

    if (_state != SpeakerState::Ready) {
        shutdownWifi(true);

        setWifiState(
            SpeakerWifiState::NotReady
        );

        return;
    }

    if (_wifiSsid.isEmpty()) {
        shutdownWifi(false);

        setWifiState(
            SpeakerWifiState::NoCredentials
        );

        return;
    }

    // Start from a known radio-off state.
    shutdownWifi(false);

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    WiFi.mode(
        WIFI_STA
    );


    WiFi.begin(
        _wifiSsid.c_str(),
        _wifiPassword.c_str()
    );

    _wifiConnectStartedMs =
        millis();

    setWifiState(
        SpeakerWifiState::Connecting
    );
}

void ESPCubeSpeakerControl::resetUdpCounters() {
    _udpRxPackets = 0;
    _udpRxBytes = 0;
    _udpInvalidPackets = 0;
    _udpSequenceErrors = 0;
    _udpLastSequence = 0;
    _udpExpectedSequence = 0;
    _udpHaveSequence = false;
}

bool ESPCubeSpeakerControl::startUdp() {
    if (_udpActive) {
        return true;
    }

    if (
        _state != SpeakerState::Ready ||
        _wifiState != SpeakerWifiState::Connected ||
        WiFi.status() != WL_CONNECTED
    ) {
        return false;
    }

    resetUdpCounters();
    _udpLastStatusPublishMs = millis();

    _udpSocket =
        ::socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_IP
        );

    if (_udpSocket < 0) {
        _udpSocket = -1;
        _udpActive = false;
        markStatusDirty();
        return false;
    }

    const int rxBufferBytes =
        UDP_RX_BUFFER_BYTES;

    if (
        setsockopt(
            _udpSocket,
            SOL_SOCKET,
            SO_RCVBUF,
            &rxBufferBytes,
            sizeof(rxBufferBytes)
        ) != 0
    ) {
        ::close(_udpSocket);
        _udpSocket = -1;
        _udpActive = false;
        markStatusDirty();
        return false;
    }

    // Never allow recvfrom() to block teardown indefinitely.
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    if (
        setsockopt(
            _udpSocket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0
    ) {
        ::close(_udpSocket);
        _udpSocket = -1;
        _udpActive = false;
        markStatusDirty();
        return false;
    }

    struct sockaddr_in localAddress;
    memset(
        &localAddress,
        0,
        sizeof(localAddress)
    );

    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddress.sin_port = htons(UDP_PORT);

    if (
        ::bind(
            _udpSocket,
            reinterpret_cast<struct sockaddr *>(
                &localAddress
            ),
            sizeof(localAddress)
        ) != 0
    ) {
        ::close(_udpSocket);
        _udpSocket = -1;
        _udpActive = false;
        markStatusDirty();
        return false;
    }

    _udpActive = true;

    if (!startUdpTask()) {
        _udpActive = false;

        ::close(_udpSocket);
        _udpSocket = -1;

        markStatusDirty();
        return false;
    }

    markStatusDirty();

    return true;
}
void ESPCubeSpeakerControl::stopUdp() {
    stopUdpTask();

    if (_udpSocket >= 0) {
        ::close(_udpSocket);
        _udpSocket = -1;
    }

    _udpActive = false;
}
void ESPCubeSpeakerControl::udpTaskThunk(
    void *arg
) {
    ESPCubeSpeakerControl *self =
        static_cast<ESPCubeSpeakerControl *>(arg);

    self->udpTaskLoop();
}

bool ESPCubeSpeakerControl::startUdpTask() {
    if (_udpTaskHandle != nullptr) {
        return true;
    }

    _udpTaskRun = true;

    const BaseType_t created =
        xTaskCreate(
            udpTaskThunk,
            "ESPCubeUdpRx",
            4096,
            this,
            2,
            &_udpTaskHandle
        );

    if (created != pdPASS) {
        _udpTaskRun = false;
        _udpTaskHandle = nullptr;
        return false;
    }

    return true;
}

void ESPCubeSpeakerControl::stopUdpTask() {
    if (_udpTaskHandle == nullptr) {
        _udpTaskRun = false;
        return;
    }

    _udpTaskRun = false;

    const uint32_t started =
        millis();

    while (
        _udpTaskHandle != nullptr &&
        millis() - started < 250
    ) {
        delay(1);
    }

    // Defensive fallback. Normally the task exits itself within ~1 tick.
    if (_udpTaskHandle != nullptr) {
        vTaskDelete(_udpTaskHandle);
        _udpTaskHandle = nullptr;
    }
}

void ESPCubeSpeakerControl::udpTaskLoop() {
    while (_udpTaskRun) {
        if (
            _udpActive &&
            _state == SpeakerState::Ready &&
            _wifiState == SpeakerWifiState::Connected &&
            WiFi.status() == WL_CONNECTED
        ) {
            pollUdp();
        }

        // Yield when no more datagrams are immediately available.
        vTaskDelay(1);
    }

    _udpTaskHandle = nullptr;

    vTaskDelete(nullptr);
}
void ESPCubeSpeakerControl::pollUdp() {
    if (
        !_udpTaskRun ||
        !_udpActive ||
        _udpSocket < 0
    ) {
        return;
    }

    uint8_t packet[
        UDP_MAX_DATAGRAM_BYTES
    ];

    struct sockaddr_in remoteAddress;
    socklen_t remoteLength =
        sizeof(remoteAddress);

    const int received =
        ::recvfrom(
            _udpSocket,
            packet,
            sizeof(packet),
            0,
            reinterpret_cast<struct sockaddr *>(
                &remoteAddress
            ),
            &remoteLength
        );

    // Timeout/no packet/error: return to task loop.
    if (received <= 0) {
        return;
    }

    if (
        received <
        static_cast<int>(
            UDP_HEADER_BYTES
        )
    ) {
        ++_udpInvalidPackets;
        return;
    }

    const uint16_t payloadLength =
        static_cast<uint16_t>(
            packet[6]
        ) |
        (
            static_cast<uint16_t>(
                packet[7]
            )
            << 8
        );

    const uint32_t sequence =
        static_cast<uint32_t>(
            packet[8]
        ) |
        (
            static_cast<uint32_t>(
                packet[9]
            )
            << 8
        ) |
        (
            static_cast<uint32_t>(
                packet[10]
            )
            << 16
        ) |
        (
            static_cast<uint32_t>(
                packet[11]
            )
            << 24
        );

    const size_t actualPayloadLength =
        static_cast<size_t>(
            received -
            static_cast<int>(
                UDP_HEADER_BYTES
            )
        );

    const bool valid =
        packet[0] == 'E' &&
        packet[1] == 'S' &&
        packet[2] == 'P' &&
        packet[3] == 'C' &&
        packet[4] ==
            UDP_PROTOCOL_VERSION &&
        packet[5] ==
            UDP_PACKET_TYPE_TEST &&
        payloadLength ==
            actualPayloadLength;

    if (!valid) {
        ++_udpInvalidPackets;
        return;
    }

    ++_udpRxPackets;

    _udpRxBytes +=
        static_cast<uint32_t>(
            actualPayloadLength
        );

    if (!_udpHaveSequence) {
        _udpHaveSequence = true;

        _udpExpectedSequence =
            sequence + 1;
    }
    else {
        if (
            sequence !=
            _udpExpectedSequence
        ) {
            ++_udpSequenceErrors;
        }

        _udpExpectedSequence =
            sequence + 1;
    }

    _udpLastSequence =
        sequence;
}
String ESPCubeSpeakerControl::buildStatus() const {
    String s;
    s.reserve(360);

    const bool connected =
        (
            _wifiState ==
                SpeakerWifiState::Connected &&
            WiFi.status() ==
                WL_CONNECTED
        );

    s += "speaker_protocol=1;state=";
    s += stateName();

    // PA is deliberately hard-coded OFF for S2-B1.
    s += ";pa=0";

    s += ";wifi=";
    s += connected ? "1" : "0";

    s += ";wifi_state=";
    s += wifiStateName();

    s += ";ip=";

    if (connected) {
        s += WiFi.localIP().toString();
    }
    else {
        s += "0.0.0.0";
    }

    s += ";credentials=";
    s += _wifiSsid.isEmpty() ? "0" : "1";

    // S2-C1 UDP transport proof.
    // UDP may exist only while Speaker is READY and Wi-Fi is connected.
    const bool udpReady =
        (
            _udpActive &&
            _state == SpeakerState::Ready &&
            connected
        );

    s += ";udp=";
    s += udpReady ? "1" : "0";

    s += ";udp_port=";
    s += String(UDP_PORT);

    s += ";udp_rx_packets=";
    s += String(_udpRxPackets);

    s += ";udp_rx_bytes=";
    s += String(_udpRxBytes);

    s += ";udp_invalid=";
    s += String(_udpInvalidPackets);

    s += ";udp_seq_errors=";
    s += String(_udpSequenceErrors);

    s += ";udp_last_seq=";
    s += String(_udpLastSequence);

    // Audio streaming/playback remain deliberately absent.
    s += ";stream=0";
    s += ";buffer=0";

    // Shared speech path must remain untouched.
    s += ";audio_mode=speech16";

    return s;
}

void ESPCubeSpeakerControl::handleJsonCommand(
    const String &raw
) {
    JsonDocument doc;

    const DeserializationError error =
        deserializeJson(
            doc,
            raw
        );

    if (error) {
        return;
    }

    String cmd =
        doc["cmd"] | "";

    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "wifi_set") {
        if (_state != SpeakerState::Ready) {
            shutdownWifi(true);

            setWifiState(
                SpeakerWifiState::NotReady
            );

            return;
        }

        const char *ssid =
            doc["ssid"] | "";

        const char *password =
            doc["password"] | "";

        const size_t ssidLength =
            strlen(ssid);

        const size_t passwordLength =
            strlen(password);

        // 802.11 SSID: max 32 bytes.
        // WPA/WPA2 passphrase: normally max 63 characters.
        // Empty password remains permitted for an open dev network.
        if (
            ssidLength == 0 ||
            ssidLength > 32 ||
            passwordLength > 63
        ) {
            return;
        }

        // Stop an existing attempt/connection before replacing creds.
        shutdownWifi(false);

        _wifiSsid =
            String(ssid);

        _wifiPassword =
            String(password);

        setWifiState(
            SpeakerWifiState::Configured
        );

        return;
    }

    if (cmd == "wifi_connect") {
        _wifiConnectRequested = true;
        return;
    }

    if (cmd == "wifi_clear") {
        _wifiClearRequested = true;
        return;
    }
}

void ESPCubeSpeakerControl::onWrite(
    NimBLECharacteristic *characteristic,
    NimBLEConnInfo &connInfo
) {
    (void)connInfo;

    if (
        !characteristic ||
        characteristic != _command
    ) {
        return;
    }

    // Preserve raw case first.
    const String raw =
        rawString(
            characteristic->getValue()
        );

    if (raw.isEmpty()) {
        return;
    }

    // JSON provisioning must NEVER be uppercased because
    // SSIDs and passwords are case-sensitive.
    if (raw.startsWith("{")) {
        handleJsonCommand(raw);
        return;
    }

    // Legacy S2-A commands remain case-insensitive.
    const String command =
        upperCommand(raw);

    if (
        command == "SPEAKER_ENTER" ||
        command == "ENTER"
    ) {
        _pending =
            static_cast<uint8_t>(
                SpeakerCommand::Enter
            );
    }
    else if (
        command == "SPEAKER_EXIT" ||
        command == "EXIT"
    ) {
        _pending =
            static_cast<uint8_t>(
                SpeakerCommand::Exit
            );
    }
    else if (
        command == "SPEAKER_STATUS" ||
        command == "STATUS"
    ) {
        _pending =
            static_cast<uint8_t>(
                SpeakerCommand::Status
            );
    }
}