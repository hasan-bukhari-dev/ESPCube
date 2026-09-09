#include "ESPCubeHID.h"

// ============================================================
// ESPCube HID REPORT MAP
//
// Report 1 = Mouse
// Report 2 = Keyboard
// Report 3 = Consumer / Media
// ============================================================

static uint8_t reportMap[] =
{
    // --------------------------------------------------------
    // REPORT 1: MOUSE
    // --------------------------------------------------------

    0x05, 0x01,
    0x09, 0x02,
    0xA1, 0x01,
    0x85, 0x01,

    0x09, 0x01,
    0xA1, 0x00,

    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x03,

    0x15, 0x00,
    0x25, 0x01,

    0x95, 0x03,
    0x75, 0x01,
    0x81, 0x02,

    0x95, 0x01,
    0x75, 0x05,
    0x81, 0x03,

    0x05, 0x01,

    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x38,

    0x15, 0x81,
    0x25, 0x7F,

    0x75, 0x08,
    0x95, 0x03,
    0x81, 0x06,

    0xC0,
    0xC0,

    // --------------------------------------------------------
    // REPORT 2: KEYBOARD
    // --------------------------------------------------------

    0x05, 0x01,
    0x09, 0x06,
    0xA1, 0x01,
    0x85, 0x02,

    0x05, 0x07,

    0x19, 0xE0,
    0x29, 0xE7,

    0x15, 0x00,
    0x25, 0x01,

    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,

    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,

    // Keyboard LED output
    0x95, 0x05,
    0x75, 0x01,

    0x05, 0x08,

    0x19, 0x01,
    0x29, 0x05,

    0x91, 0x02,

    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,

    // Six key rollover
    0x95, 0x06,
    0x75, 0x08,

    0x15, 0x00,
    0x25, 0x65,

    0x05, 0x07,

    0x19, 0x00,
    0x29, 0x65,

    0x81, 0x00,

    0xC0,

    // --------------------------------------------------------
    // REPORT 3: CONSUMER CONTROL
    // --------------------------------------------------------

    0x05, 0x0C,
    0x09, 0x01,
    0xA1, 0x01,
    0x85, 0x03,

    0x15, 0x00,
    0x26, 0xFF, 0x03,

    0x19, 0x00,
    0x2A, 0xFF, 0x03,

    0x75, 0x10,
    0x95, 0x01,

    0x81, 0x00,

    0xC0
};

void ESPCubeHID::begin(
    PreStartHook preStartHook,
    const char *extraServiceUUID
)
{
    Serial.println(
        "[HID] Initializing direct ESPCube HID..."
    );

    NimBLEDevice::init("ESPCube");

    NimBLEDevice::setSecurityAuth(
        BLE_SM_PAIR_AUTHREQ_BOND |
        BLE_SM_PAIR_AUTHREQ_SC
    );

    NimBLEDevice::setSecurityIOCap(
        BLE_HS_IO_NO_INPUT_OUTPUT
    );

    NimBLEDevice::setPower(9);

    _server =
        NimBLEDevice::createServer();

    _server->advertiseOnDisconnect(true);

    _hid =
        new NimBLEHIDDevice(_server);

    _hid->setManufacturer(
        "ESPCube"
    );

    _hid->setHidInfo(
        0x00,
        0x01
    );

    _hid->setReportMap(
        reportMap,
        sizeof(reportMap)
    );

    _mouseInput =
        _hid->getInputReport(1);

    _keyboardInput =
        _hid->getInputReport(2);

    _keyboardOutput =
        _hid->getOutputReport(2);

    _consumerInput =
        _hid->getInputReport(3);

    _hid->setBatteryLevel(100);

    // --------------------------------------------------------
    // ESPCube extension hook
    //
    // Any custom GATT service must be created BEFORE the
    // NimBLE server starts, otherwise Windows may enumerate
    // stale/invalid characteristic handles.
    // --------------------------------------------------------

    if (preStartHook)
    {
        preStartHook(
            _server
        );
    }

    _hid->startServices();

    NimBLEAdvertising *adv =
        NimBLEDevice::getAdvertising();

    // Generic HID appearance
    adv->setAppearance(0x03C0);

    adv->setName("ESPCube");

    adv->addServiceUUID(
        _hid->getHidService()->getUUID()
    );

    adv->addServiceUUID(
        _hid->getBatteryService()->getUUID()
    );

    if (
        extraServiceUUID &&
        extraServiceUUID[0] != '\0'
    )
    {
        adv->addServiceUUID(
            extraServiceUUID
        );
    }

    adv->enableScanResponse(true);

    adv->start();

    Serial.println(
        "[HID] Mouse + keyboard + media ready."
    );
}

bool ESPCubeHID::isConnected() const
{
    return
        _server != nullptr &&
        _server->getConnectedCount() > 0;
}

// ============================================================
// MOUSE
// ============================================================

void ESPCubeHID::move(
    int16_t dx,
    int16_t dy,
    int8_t wheel
)
{
    if (
        !isConnected() ||
        !_mouseInput
    ) {
        return;
    }

    dx =
        constrain(
            dx,
            -127,
            127
        );

    dy =
        constrain(
            dy,
            -127,
            127
        );

    uint8_t report[4] =
    {
        _mouseButtons,
        (uint8_t)(int8_t)dx,
        (uint8_t)(int8_t)dy,
        (uint8_t)wheel
    };

    _mouseInput->notify(
        report,
        sizeof(report)
    );
}

void ESPCubeHID::setMouseButton(
    uint8_t mask,
    bool pressed
)
{
    if (pressed)
        _mouseButtons |= mask;
    else
        _mouseButtons &= ~mask;

    move(0, 0, 0);
}

void ESPCubeHID::releaseMouseButtons()
{
    _mouseButtons = 0;

    move(0, 0, 0);
}

void ESPCubeHID::releaseKeyboard()
{
    if (!_keyboardInput)
        return;

    uint8_t zero[8] =
    {
        0,0,0,0,0,0,0,0
    };

    // Send multiple all-up reports.
    //
    // This is deliberately redundant: if Windows received a key-down
    // but one BLE notification was lost, subsequent zero reports still
    // give it another chance to clear keyboard state.
    for (
        int attempt = 0;
        attempt < 3;
        attempt++
    )
    {
        _keyboardInput->notify(
            zero,
            sizeof(zero)
        );

        delay(2);
    }
}

// ============================================================
// KEYBOARD
// ============================================================

void ESPCubeHID::keyTap(
    uint8_t usage,
    uint8_t modifiers
)
{
    if (
        !isConnected() ||
        !_keyboardInput
    )
    {
        return;
    }

    uint8_t down[8] =
    {
        modifiers,
        0,
        usage,
        0,
        0,
        0,
        0,
        0
    };

    // Prime / clear any previous keyboard state.
    releaseKeyboard();

    delay(20);

    // ========================================================
    // KEY DOWN
    // ========================================================

    bool downOK =
        false;

    for (
        int attempt = 0;
        attempt < 20;
        attempt++
    )
    {
        if (
            _keyboardInput->notify(
                down,
                sizeof(down)
            )
        )
        {
            downOK =
                true;

            break;
        }

        delay(1);
    }

    // Even if DOWN failed, send all-up reports before returning.
    if (!downOK)
    {
        releaseKeyboard();
        return;
    }

    // Absolutely no Serial/debug calls are allowed between
    // a successful DOWN report and keyboard release.
    delay(25);

    // ========================================================
    // KEY UP / FAIL-SAFE RELEASE
    // ========================================================

    releaseKeyboard();

    delay(20);
}

void ESPCubeHID::enter()
{
    keyTap(0x28);
}

void ESPCubeHID::escape()
{
    keyTap(0x29);
}

void ESPCubeHID::backspace()
{
    keyTap(0x2A);
}

void ESPCubeHID::tab()
{
    keyTap(0x2B);
}

void ESPCubeHID::space()
{
    keyTap(0x2C);
}

// ============================================================
// ASCII -> USB HID
// ============================================================

bool ESPCubeHID::asciiToHid(
    char c,
    uint8_t &usage,
    uint8_t &mods
)
{
    usage = 0;
    mods = 0;

    if (
        c >= 'a' &&
        c <= 'z'
    )
    {
        usage =
            0x04 +
            (c - 'a');

        return true;
    }

    if (
        c >= 'A' &&
        c <= 'Z'
    )
    {
        usage =
            0x04 +
            (c - 'A');

        mods = 0x02;

        return true;
    }

    if (
        c >= '1' &&
        c <= '9'
    )
    {
        usage =
            0x1E +
            (c - '1');

        return true;
    }

    if (c == '0')
    {
        usage = 0x27;
        return true;
    }

    switch (c)
    {
        case ' ':
            usage = 0x2C;
            return true;

        case '-':
            usage = 0x2D;
            return true;

        case '_':
            usage = 0x2D;
            mods = 0x02;
            return true;

        case '=':
            usage = 0x2E;
            return true;

        case '+':
            usage = 0x2E;
            mods = 0x02;
            return true;

        case '[':
            usage = 0x2F;
            return true;

        case '{':
            usage = 0x2F;
            mods = 0x02;
            return true;

        case ']':
            usage = 0x30;
            return true;

        case '}':
            usage = 0x30;
            mods = 0x02;
            return true;

        case '\\':
            usage = 0x31;
            return true;

        case '|':
            usage = 0x31;
            mods = 0x02;
            return true;

        case ';':
            usage = 0x33;
            return true;

        case ':':
            usage = 0x33;
            mods = 0x02;
            return true;

        case '\'':
            usage = 0x34;
            return true;

        case '"':
            usage = 0x34;
            mods = 0x02;
            return true;

        case '`':
            usage = 0x35;
            return true;

        case '~':
            usage = 0x35;
            mods = 0x02;
            return true;

        case ',':
            usage = 0x36;
            return true;

        case '<':
            usage = 0x36;
            mods = 0x02;
            return true;

        case '.':
            usage = 0x37;
            return true;

        case '>':
            usage = 0x37;
            mods = 0x02;
            return true;

        case '/':
            usage = 0x38;
            return true;

        case '?':
            usage = 0x38;
            mods = 0x02;
            return true;

        case '!':
            usage = 0x1E;
            mods = 0x02;
            return true;

        case '@':
            usage = 0x1F;
            mods = 0x02;
            return true;

        case '#':
            usage = 0x20;
            mods = 0x02;
            return true;

        case '$':
            usage = 0x21;
            mods = 0x02;
            return true;

        case '%':
            usage = 0x22;
            mods = 0x02;
            return true;

        case '^':
            usage = 0x23;
            mods = 0x02;
            return true;

        case '&':
            usage = 0x24;
            mods = 0x02;
            return true;

        case '*':
            usage = 0x25;
            mods = 0x02;
            return true;

        case '(':
            usage = 0x26;
            mods = 0x02;
            return true;

        case ')':
            usage = 0x27;
            mods = 0x02;
            return true;
    }

    return false;
}

bool ESPCubeHID::typeChar(char c)
{
    uint8_t usage = 0;
    uint8_t mods = 0;

    if (!asciiToHid(
        c,
        usage,
        mods
    )) {
        return false;
    }

    keyTap(
        usage,
        mods
    );

    return true;
}

// ============================================================
// MEDIA
// ============================================================

void ESPCubeHID::consumerTap(
    uint16_t usage
)
{
    if (
        !isConnected() ||
        !_consumerInput
    ) {
        return;
    }

    uint8_t down[2] =
    {
        (uint8_t)(
            usage &
            0xFF
        ),

        (uint8_t)(
            usage >>
            8
        )
    };

    uint8_t up[2] =
    {
        0,
        0
    };

    _consumerInput->notify(
        down,
        sizeof(down)
    );

    delay(18);

    _consumerInput->notify(
        up,
        sizeof(up)
    );
}