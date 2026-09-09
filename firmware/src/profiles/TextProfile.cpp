#include "TextProfile.h"
#include "../app/RuntimeGlobals.h"
#include "../ui/ScreenRenderer.h"
#include <cstring>

void TextProfile::previewPush(char c)
{
    if (
        textPreviewLen >=
        21
    )
    {
        memmove(
            textPreview,
            textPreview + 1,
            20
        );

        textPreviewLen =
            20;
    }

    textPreview[
        textPreviewLen++
    ] = c;

    textPreview[
        textPreviewLen
    ] = '\0';
}

void TextProfile::previewDelete()
{
    if (!textPreviewLen)
        return;

    textPreviewLen--;

    textPreview[
        textPreviewLen
    ] = '\0';
}

void TextProfile::sendCharacter(char c)
{
    if (
        c >= 'a' &&
        c <= 'z'
    )
    {
        bool upper =
            capsLock ^
            shiftOnce;

        if (upper)
        {
            c =
                c -
                'a' +
                'A';
        }
    }

    if (hid.typeChar(c))
    {
        previewPush(c);
    }

    shiftOnce =
        false;

    renderer.renderCurrentScreen();
}

void TextProfile::sendBackspace()
{
    hid.backspace();

    previewDelete();

    renderer.renderCurrentScreen();
}

void TextProfile::sendSpace()
{
    hid.space();

    previewPush(' ');

    renderer.renderCurrentScreen();
}

void TextProfile::sendEnter()
{
    hid.enter();

    textPreviewLen =
        0;

    textPreview[0] =
        '\0';

    renderer.renderCurrentScreen();
}

void TextProfile::typeSpeechTranscript(
    const String &text
)
{
    if (!hid.isConnected())
    {
        Serial.println(
            "[STT] BLE not connected; transcript not typed."
        );

        return;
    }

    // Wi-Fi and BLE share the ESP32-S3 2.4 GHz radio.
    // Give BLE time to recover after Groq finishes.
    Serial.println(
        "[STT] Waiting for BLE radio recovery..."
    );

    delay(700);

    if (!hid.isConnected())
    {
        Serial.println(
            "[STT] BLE dropped during Wi-Fi recovery."
        );

        return;
    }

    Serial.println(
        "[STT] Typing transcript..."
    );

    for (
        size_t i = 0;
        i < text.length();
        i++
    )
    {
        unsigned char c =
            (unsigned char)text[i];

        // Current ESPCube HID character path is ASCII-oriented.
        if (c >= 128)
            continue;

        if (
            c == '\n' ||
            c == '\r'
        )
        {
            hid.enter();

            delay(35);

            continue;
        }

        if (
            hid.typeChar(
                (char)c
            )
        )
        {
            previewPush(
                (char)c
            );
        }

        delay(35);
    }

    Serial.println(
        "[STT] Transcript typing complete."
    );

    renderer.renderCurrentScreen();
}

