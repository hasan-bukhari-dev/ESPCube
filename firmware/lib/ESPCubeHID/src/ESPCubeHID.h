#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

class ESPCubeHID
{
public:
    using PreStartHook =
        void (*)(
            NimBLEServer *server
        );

    void begin(
        PreStartHook preStartHook = nullptr,
        const char *extraServiceUUID = nullptr
    );

    bool isConnected() const;

    // ESPCube extensions may attach additional GATT services
    // to the SAME NimBLE server used by HID.
    //
    // This does not transfer ownership.
    NimBLEServer *server() const
    {
        return _server;
    }

    void move(
        int16_t dx,
        int16_t dy,
        int8_t wheel = 0
    );

    void setMouseButton(
        uint8_t mask,
        bool pressed
    );

    void releaseMouseButtons();
    void releaseKeyboard();

    void keyTap(
        uint8_t usage,
        uint8_t modifiers = 0
    );

    bool typeChar(char c);

    void enter();
    void backspace();
    void space();
    void tab();
    void escape();

    void consumerTap(uint16_t usage);

private:
    NimBLEServer *_server = nullptr;
    NimBLEHIDDevice *_hid = nullptr;

    NimBLECharacteristic *_mouseInput = nullptr;
    NimBLECharacteristic *_keyboardInput = nullptr;
    NimBLECharacteristic *_keyboardOutput = nullptr;
    NimBLECharacteristic *_consumerInput = nullptr;

    uint8_t _mouseButtons = 0;

    bool asciiToHid(
        char c,
        uint8_t &usage,
        uint8_t &mods
    );
};