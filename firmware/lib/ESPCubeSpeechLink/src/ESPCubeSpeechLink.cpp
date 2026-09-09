#include "ESPCubeSpeechLink.h"

// ============================================================
// ESPCube multi-connection BLE callback
//
// Connection #1:
//   operating system HID connection
//
// Connection #2:
//   ESPCube Companion / Speech Protocol
//
// BLE advertising normally stops after the first connection.
// Restart it while only one client is connected so the speech
// companion remains discoverable.
//
// Existing HID report behavior is untouched.
// ============================================================

static uint32_t speechLastAdvAttempt =
    0;
class ESPCubeSpeechServerCallbacks :
    public NimBLEServerCallbacks
{
    void onConnect(
        NimBLEServer *server,
        NimBLEConnInfo &connInfo
    ) override
    {
        uint32_t count =
            server->getConnectedCount();

        Serial.printf(
            "[SPEECH-LINK] BLE client connected. count=%lu\n",
            (unsigned long)count
        );

        // First connection is normally Windows HID.
        // Continue advertising for the companion.
        if (count < 2)
        {
            Serial.println(
                "[SPEECH-LINK] Continuing advertising for companion."
            );

            NimBLEDevice::startAdvertising();
        }
        else
        {
            Serial.println(
                "[SPEECH-LINK] HID + companion connected."
            );
        }
    }

    void onDisconnect(
        NimBLEServer *server,
        NimBLEConnInfo &connInfo,
        int reason
    ) override
    {
        Serial.printf(
            "[SPEECH-LINK] BLE client disconnected. "
            "count=%lu reason=%d\n",
            (unsigned long)server->getConnectedCount(),
            reason
        );
    }

    void onAuthenticationComplete(
        NimBLEConnInfo &connInfo
    ) override
    {
        Serial.printf(
            "[BLE] Authentication complete. "
            "encrypted=%d bonded=%d bonds=%d\n",
            connInfo.isEncrypted() ? 1 : 0,
            connInfo.isBonded() ? 1 : 0,
            NimBLEDevice::getNumBonds()
        );

        if (!connInfo.isEncrypted())
        {
            Serial.println(
                "[BLE] Authentication failed."
            );

            return;
        }
        // Pairing completed. No controller reboot is scheduled.
        // Standard HID stability takes priority.
    }
};


static ESPCubeSpeechServerCallbacks
    speechServerCallbacks;

// ============================================================
// IMA ADPCM
//
// ESPCube Speech Protocol v1 sends independent blocks:
//
// byte 0      = 0xA1 audio block
// byte 1      = protocol version
// byte 2..3   = sequence number, little endian
// byte 4..5   = decoded PCM sample count
// byte 6..7   = initial predictor, int16 little endian
// byte 8      = initial IMA index
// byte 9      = reserved
// byte 10..   = 4-bit IMA ADPCM, low nibble first
//
// Every block contains its own decoder state.
// A missing block therefore does NOT corrupt later blocks.
// ============================================================

static const int INDEX_TABLE[16] =
{
    -1, -1, -1, -1,
     2,  4,  6,  8,
    -1, -1, -1, -1,
     2,  4,  6,  8
};

static const int STEP_TABLE[89] =
{
       7,     8,     9,    10,    11,    12,    13,    14,
      16,    17,    19,    21,    23,    25,    28,    31,
      34,    37,    41,    45,    50,    55,    60,    66,
      73,    80,    88,    97,   107,   118,   130,   143,
     157,   173,   190,   209,   230,   253,   279,   307,
     337,   371,   408,   449,   494,   544,   598,   658,
     724,   796,   876,   963,  1060,  1166,  1282,  1411,
    1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
   15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
   32767
};

static uint8_t encodeNibble(
    int16_t sample,
    int &predictor,
    int &index
)
{
    int step =
        STEP_TABLE[index];

    int diff =
        (int)sample -
        predictor;

    uint8_t code =
        0;

    if (diff < 0)
    {
        code |= 8;
        diff = -diff;
    }

    int delta =
        step >> 3;

    if (diff >= step)
    {
        code |= 4;
        diff -= step;
        delta += step;
    }

    if (
        diff >=
        (step >> 1)
    )
    {
        code |= 2;
        diff -=
            step >> 1;

        delta +=
            step >> 1;
    }

    if (
        diff >=
        (step >> 2)
    )
    {
        code |= 1;

        delta +=
            step >> 2;
    }

    if (code & 8)
        predictor -= delta;
    else
        predictor += delta;

    if (predictor > 32767)
        predictor = 32767;

    if (predictor < -32768)
        predictor = -32768;

    index +=
        INDEX_TABLE[code & 0x0F];

    if (index < 0)
        index = 0;

    if (index > 88)
        index = 88;

    return code;
}

bool ESPCubeSpeechLink::begin(
    NimBLEServer *server
)
{
    if (!server)
    {
        Serial.println(
            "[SPEECH-LINK] No NimBLE server."
        );

        return false;
    }

    // Attach only connection lifecycle callbacks to the SAME
    // server already created by ESPCubeHID.
    server->setCallbacks(
        &speechServerCallbacks
    );

    // Enough room for one complete 20ms ADPCM packet.
    // The final negotiated MTU is still controlled by both peers.
    NimBLEDevice::setMTU(247);

    _service =
        server->createService(
            SERVICE_UUID
        );

    if (!_service)
    {
        Serial.println(
            "[SPEECH-LINK] Service create failed."
        );

        return false;
    }

    _control =
        _service->createCharacteristic(
            CONTROL_UUID,
            NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::NOTIFY
        );

    _status =
        _service->createCharacteristic(
            STATUS_UUID,
            NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::NOTIFY
        );

    _audio =
        _service->createCharacteristic(
            AUDIO_UUID,
            NIMBLE_PROPERTY::NOTIFY
        );

    if (
        !_control ||
        !_status ||
        !_audio
    )
    {
        Serial.println(
            "[SPEECH-LINK] Characteristic create failed."
        );

        return false;
    }

    // Do NOT start the service or advertising here.
    //
    // ESPCubeHID owns the single server startup sequence.
    // Because this begin() is now called from HID's pre-start
    // hook, the Speech service will be present when NimBLE
    // assigns the GATT handles for the first time.

    sendStatus();

    Serial.println(
        "[SPEECH-LINK] ESPCube Speech Protocol v1 registered."
    );

    Serial.println(
        "[SPEECH-LINK] IMA-ADPCM mono 16kHz, 320 samples/block."
    );

    return true;
}

void ESPCubeSpeechLink::sendControl(
    const String &message
)
{
    if (!_control)
        return;

    _control->setValue(
        message.c_str()
    );

    _control->notify();
}

void ESPCubeSpeechLink::sendStatus()
{
    if (!_status)
        return;

    String value =
        "protocol=1;"
        "codec=ima_adpcm;"
        "source=pcm16;"
        "rate=16000;"
        "channels=1;"
        "block_samples=320";

    _status->setValue(
        value.c_str()
    );
}

void ESPCubeSpeechLink::startSession()
{
    if (!_audio)
        return;

    _active =
        true;

    _frameFill =
        0;

    _sequence =
        0;

    _adpcmIndex =
        0;

    _sentFrames =
        0;

    _droppedFrames =
        0;

    _totalSamples =
        0;

    sendControl(
        "START;protocol=1;codec=ima_adpcm;rate=16000;channels=1;block_samples=320"
    );

    Serial.println(
        "[SPEECH-LINK] START"
    );
}

void ESPCubeSpeechLink::pushPcm(
    const int16_t *samples,
    size_t count
)
{
    if (
        !_active ||
        !samples ||
        count == 0
    )
        return;

    size_t offset =
        0;

    while (offset < count)
    {
        size_t room =
            FRAME_SAMPLES -
            _frameFill;

        size_t remaining =
            count -
            offset;

        size_t take =
            remaining < room
            ? remaining
            : room;

        memcpy(
            &_frame[_frameFill],
            &samples[offset],
            take *
            sizeof(int16_t)
        );

        _frameFill +=
            take;

        offset +=
            take;

        _totalSamples +=
            take;

        if (
            _frameFill ==
            FRAME_SAMPLES
        )
        {
            sendFrame(
                _frame,
                _frameFill
            );

            _frameFill =
                0;
        }
    }
}

void ESPCubeSpeechLink::sendFrame(
    const int16_t *samples,
    size_t count
)
{
    if (
        !_audio ||
        !samples ||
        count == 0
    )
        return;

    // Maximum:
    // 10 byte header +
    // ceil((320 - 1) / 2) = 160 ADPCM bytes.
    uint8_t packet[170];

    packet[0] =
        0xA1;

    packet[1] =
        PROTOCOL_VERSION;

    packet[2] =
        (uint8_t)(
            _sequence &
            0xFF
        );

    packet[3] =
        (uint8_t)(
            _sequence >>
            8
        );

    packet[4] =
        (uint8_t)(
            count &
            0xFF
        );

    packet[5] =
        (uint8_t)(
            count >>
            8
        );

    int predictor =
        samples[0];

    int initialIndex =
        _adpcmIndex;

    packet[6] =
        (uint8_t)(
            predictor &
            0xFF
        );

    packet[7] =
        (uint8_t)(
            (predictor >> 8) &
            0xFF
        );

    packet[8] =
        (uint8_t)initialIndex;

    packet[9] =
        0;

    size_t packetPos =
        10;

    bool lowNibble =
        true;

    uint8_t packed =
        0;

    int workingIndex =
        initialIndex;

    for (
        size_t i = 1;
        i < count;
        i++
    )
    {
        uint8_t code =
            encodeNibble(
                samples[i],
                predictor,
                workingIndex
            );

        if (lowNibble)
        {
            packed =
                code &
                0x0F;

            lowNibble =
                false;
        }
        else
        {
            packed |=
                (uint8_t)(
                    (code & 0x0F)
                    << 4
                );

            packet[packetPos++] =
                packed;

            lowNibble =
                true;

            packed =
                0;
        }
    }

    if (!lowNibble)
    {
        packet[packetPos++] =
            packed;
    }

    _adpcmIndex =
        workingIndex;

    bool ok =
        _audio->notify(
            packet,
            packetPos
        );

    if (ok)
        _sentFrames++;
    else
        _droppedFrames++;

    _sequence++;
}

void ESPCubeSpeechLink::endSession()
{
    if (!_active)
        return;

    if (_frameFill)
    {
        sendFrame(
            _frame,
            _frameFill
        );

        _frameFill =
            0;
    }

    _active =
        false;

    String message =
        "END;samples=" +
        String(_totalSamples) +
        ";frames=" +
        String(_sentFrames) +
        ";notify_failures=" +
        String(_droppedFrames);

    sendControl(
        message
    );

    Serial.printf(
        "[SPEECH-LINK] END samples=%lu frames=%lu notify_failures=%lu\n",
        (unsigned long)_totalSamples,
        (unsigned long)_sentFrames,
        (unsigned long)_droppedFrames
    );
}

void ESPCubeSpeechLink::maintain()
{

    NimBLEServer *server =
        NimBLEDevice::getServer();

    if (!server)
        return;

    uint32_t connected =
        server->getConnectedCount();

    // --------------------------------------------------------
    // CONTINUOUS ADVERTISING WATCHDOG
    //
    // 0 clients:
    //   advertise for HID / pairing
    //
    // 1 client:
    //   normally Windows HID is connected,
    //   so keep advertising for ESPCube Companion
    //
    // 2+ clients:
    //   HID + companion are already connected
    // --------------------------------------------------------

    if (connected < 2)
    {
        NimBLEAdvertising *adv =
            NimBLEDevice::getAdvertising();

        if (
            adv &&
            !adv->isAdvertising() &&
            millis() -
            speechLastAdvAttempt >=
            1000
        )
        {
            speechLastAdvAttempt =
                millis();

            bool ok =
                NimBLEDevice::startAdvertising();

            Serial.printf(
                "[BLE] Advertising watchdog: "
                "clients=%lu restart=%s\n",
                (unsigned long)connected,
                ok ? "OK" : "FAILED"
            );
        }
    }
}

bool ESPCubeSpeechLink::isSessionActive() const
{
    return _active;
}

uint32_t ESPCubeSpeechLink::sentFrames() const
{
    return _sentFrames;
}

uint32_t ESPCubeSpeechLink::droppedFrames() const
{
    return _droppedFrames;
}