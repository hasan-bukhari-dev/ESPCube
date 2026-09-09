#include "AppRuntime.h"
#include "RuntimeGlobals.h"
#include "../hardware/BoardConfig.h"
#include "../ui/ScreenRenderer.h"
#include "../profiles/SpeakerStream.h"

void AppRuntime::begin()
{
    speakerProfile.begin();

    Serial.begin(115200);

    // Debug output must NEVER be able to hold product logic.
    // 1 ms is intentional: timeout=0 has had HWCDC blocking bugs.
    Serial.setTxTimeoutMs(1);
    delay(1200);

    Serial.println();
    Serial.println("================================================");
    Serial.println("              ESPCube v1.0");
    Serial.println("================================================");
    Serial.println();

    // ========================================================
    // BUTTONS
    // ========================================================

    buttons.begin();

    // ========================================================
    // DISPLAY
    // ========================================================

    Serial.println("Initializing display...");

    pinMode(Board::LcdBacklight, OUTPUT);
    digitalWrite(Board::LcdBacklight, LOW);

    bool displayOK =
        gfx->begin();


    if (displayOK)
    {
        gfx->setTextWrap(false);

        digitalWrite(
            Board::LcdBacklight,
            HIGH
        );

        renderer.drawStaticUI();
        renderer.drawStatus();

        Serial.println("Display ready.");
    }
    else
    {
        Serial.println(
            "WARNING: display init failed."
        );
    }

    // ========================================================
    // QMI8658 IMU INITIALIZATION
    // ========================================================

    if (!motion.begin())
    {
        Serial.println(
            "FATAL: QMI8658 NOT FOUND"
        );

        while (true) {
            delay(1000);
        }
    }

    // ========================================================
    // TOUCH
    // ========================================================

    touch.begin();

    // ========================================================
    // MICROPHONE
    // ========================================================

    if (!speech.begin())
    {
        Serial.println(
            "[MIC] WARNING: speech hardware unavailable."
        );
    }
    // ========================================================
    // BLE
    // ========================================================

    // ========================================================
// DIRECT ESPCube HID
// ========================================================

hid.begin(
        [](NimBLEServer *server)
        {
            if (
                !speechLink.begin(
                    server
                )
            )
            {
                Serial.println(
                    "[SPEECH-LINK] WARNING: BLE speech service unavailable."
                );
            }

            if (
                !speakerControl.begin(
                    server
                )
            )
            {
                Serial.println(
                    "[SPEAKER] WARNING: BLE control service unavailable."
                );
            }
        },
        ESPCubeSpeechLink::SERVICE_UUID
    );

    Serial.println(
        "Bluetooth HID ready."
    );

    Serial.println();
    Serial.println("BUTTONS");
    Serial.println("A = LEFT");
    Serial.println("B = MIDDLE");
    Serial.println("C = RIGHT");

    Serial.println();
    Serial.println("DISPLAY");
    Serial.println("Mode = MOUSE");
    Serial.println("BLE = LINK / WAIT");
    Serial.println("Gyro = REST / MOVE");

    Serial.println();
}

// ============================================================
// SPEAKER CONTROL
// ============================================================

// ============================================================
// TCP PCM SESSION MANAGEMENT
// ============================================================

void AppRuntime::update()
{
    speechLink.maintain();
    speakerControl.maintain();
    speakerStream.serviceControl();
    speakerStream.serviceD2A();

    profiles.updateButtons();
    motion.update(buttons, profiles, hid);
    profiles.updateTouch();
    renderer.updateDisplay();
    // Periodic debug STATUS logging disabled:
    // USB CDC writes can block the main loop when no host is reading COM22.


    delay(1);
}
