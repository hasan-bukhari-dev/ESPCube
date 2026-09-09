#include "ScreenRenderer.h"
#include "Theme.h"
#include "../app/RuntimeGlobals.h"
#include "../profiles/SpeakerStream.h"
#include <cstring>

ScreenRenderer renderer;

void ScreenRenderer::centerText(
    const char *text,
    int16_t cx,
    int16_t y,
    uint8_t size,
    uint16_t color
)
{
    gfx->setTextSize(size);

    gfx->setTextColor(color);

    int16_t width =
        strlen(text) *
        6 *
        size;

    gfx->setCursor(
        cx -
        width / 2,
        y
    );

    gfx->print(text);
}

void ScreenRenderer::button(
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    const char *label,
    uint8_t size
)
{
    gfx->drawRoundRect(
        x,
        y,
        w,
        h,
        7,
        UI_BORDER
    );

    centerText(
        label,
        x + w / 2,
        y +
        (h - 8 * size) / 2,
        size,
        UI_WHITE
    );
}

void ScreenRenderer::drawMicScreen()
{
    gfx->fillScreen(
        UI_BG
    );

    centerText(
        "MIC",
        120,
        34,
        3,
        UI_WHITE
    );

    gfx->fillCircle(
        120,
        100,
        13,
        UI_GREEN
    );

    centerText(
        "LISTENING",
        120,
        130,
        2,
        UI_WHITE
    );

    centerText(
        "release B",
        120,
        174,
        1,
        UI_MUTED
    );
}

void ScreenRenderer::drawHome()
{
    button(
        4,
        4,
        54,
        30,
        "HOME",
        1
    );
}

const char *ScreenRenderer::profileName()
{
    switch (
        profiles.profile
    )
    {
        case Profile::MOUSE:
            return "MOUSE";

        case Profile::SPEAKER:
            return "SPEAKER";
    }

    return "MOUSE";
}

// ============================================================
// OPERATING SCREEN
// ============================================================
// ============================================================
// SPEAKER VOLUME UI
// ============================================================
void ScreenRenderer::drawSpeakerProfileUI()
{
    // Speaker profile rendering.
    gfx->fillScreen(UI_BG);

    button(6,6,58,34,"HOME",1);

    centerText(
        "SPEAKER",
        150,
        22,
        2,
        UI_BLUE
    );

    char label[24];

    if (speakerProfile.muted)
    {
        snprintf(
            label,
            sizeof(label),
            "VOLUME  MUTED"
        );
    }
    else
    {
        snprintf(
            label,
            sizeof(label),
            "VOLUME  %u%%",
            static_cast<unsigned>(
                speakerProfile.volumePercent
            )
        );
    }

    centerText(
        label,
        120,
        70,
        2,
        speakerProfile.muted
            ? UI_MUTED
            : UI_WHITE
    );

    gfx->fillRect(
        20,
        108,
        200,
        5,
        UI_MUTED
    );

    const int16_t knobX =
        20 +
        static_cast<int16_t>(
            (
                static_cast<uint32_t>(
                    speakerProfile.volumePercent
                ) *
                200U
            ) /
            100U
        );

    gfx->fillCircle(
        knobX,
        110,
        10,
        speakerProfile.muted
            ? UI_MUTED
            : UI_BLUE
    );

    button(
        28,
        145,
        184,
        62,
        speakerProfile.muted
            ? "UNMUTE"
            : "MUTE",
        2
    );

    centerText(
        "A -   B MUTE   C +",
        120,
        222,
        1,
        UI_MUTED
    );
}

void ScreenRenderer::drawStaticUI()
{
    if (profiles.profile == Profile::SPEAKER)
    {
        drawSpeakerProfileUI();
        return;
    }

    gfx->fillScreen(
        UI_BG
    );

    centerText(
        "ESPCube",
        120,
        23,
        2,
        UI_WHITE
    );

    centerText(
        profileName(),
        120,
        90,
        4,
        UI_WHITE
    );

    gfx->drawFastHLine(
        20,
        165,
        200,
        UI_BORDER
    );

    gfx->setTextSize(1);

    gfx->setTextColor(
        UI_MUTED
    );

    gfx->setCursor(
        24,
        184
    );

    gfx->print("BLE");

    gfx->setCursor(
        151,
        184
    );

    gfx->print("GYRO");
}

void ScreenRenderer::drawStatus()
{
    if (profiles.profile == Profile::SPEAKER)
    {
        return;
    }

    if (
        profiles.screen !=
        UIScreen::MOUSE
    ) {
        return;
    }

    bool paired =
        hid.isConnected();

    if (
        firstDisplayDraw ||
        paired !=
        lastDisplayPaired
    )
    {
        gfx->fillRect(
            20,
            198,
            90,
            25,
            UI_BG
        );

        gfx->setTextSize(2);

        gfx->setTextColor(
            paired
            ? UI_GREEN
            : UI_WARN
        );

        gfx->setCursor(
            24,
            201
        );

        gfx->print(
            paired
            ? "LINK"
            : "WAIT"
        );

        lastDisplayPaired =
            paired;
    }

    if (
        firstDisplayDraw ||
        motion.stationary !=
        lastDisplayStationary
    )
    {
        gfx->fillRect(
            140,
            198,
            80,
            25,
            UI_BG
        );

        gfx->setTextSize(2);

        if (
            !motion.gyroEnabled ||
            profiles.profile ==
            Profile::SPEAKER
        )
        {
            gfx->setTextColor(
                UI_MUTED
            );

            gfx->setCursor(
                145,
                201
            );

            gfx->print("OFF");
        }
        else
        {
            gfx->setTextColor(
                motion.stationary
                ? UI_GREEN
                : UI_BLUE
            );

            gfx->setCursor(
                145,
                201
            );

            gfx->print(
                motion.stationary
                ? "REST"
                : "MOVE"
            );
        }

        lastDisplayStationary =
            motion.stationary;
    }

    firstDisplayDraw =
        false;
}

// ============================================================
// MENU
// ============================================================

void ScreenRenderer::drawMenu()
{
    gfx->fillScreen(UI_BG);

    // Speaker HOME navigation UI.
    centerText("ESPCube",120,24,2,UI_WHITE);

    button(8,44,108,90,"MOUSE",2);
    button(124,44,108,90,"TEXT",2);
    button(8,142,108,90,"SPEAKER",2);
    button(124,142,108,90,"SETTINGS",2);
}

// ============================================================
// TEXT COMMON HEADER
// ============================================================

void ScreenRenderer::drawKeyboardTop()
{
    button(
        0,0,
        60,40,
        "HOME",
        1
    );

    button(
        60,0,
        60,40,
        profiles.screen ==
            UIScreen::TEXT_GROUP
            ? "BACK"
            : (
                textProfile.t9SecondPage
                    ? "ABC<"
                    : "ABC>"
            ),
        1
    );

    button(
        120,0,
        60,40,
        (
            profiles.screen ==
                UIScreen::TEXT_NUMBERS &&
            textProfile.selectedGroup !=
                nullptr
        )
            ? "BACK"
            : "123",
        1
    );

    button(
        180,0,
        60,40,
        (
            profiles.screen ==
                UIScreen::TEXT_SYMBOLS &&
            textProfile.selectedGroup !=
                nullptr
        )
            ? "BACK"
            : "#+=",
        1
    );

    if (
        profiles.screen ==
            UIScreen::TEXT_ALPHA ||
        profiles.screen ==
            UIScreen::TEXT_GROUP
    )
    {
        gfx->drawFastHLine(
            65,38,
            50,
            UI_GREEN
        );
    }
    else if (
        profiles.screen ==
            UIScreen::TEXT_NUMBERS
    )
    {
        gfx->drawFastHLine(
            125,38,
            50,
            UI_GREEN
        );
    }
    else
    {
        gfx->drawFastHLine(
            185,38,
            50,
            UI_GREEN
        );
    }
}

void ScreenRenderer::drawKeyboardDock(
    bool allowShift
)
{
    const char *shiftLabel =
        "SHIFT";

    if (allowShift)
    {
        if (textProfile.capsLock)
        {
            shiftLabel =
                textProfile.shiftOnce
                ? "lower"
                : "CAPS";
        }
        else if (textProfile.shiftOnce)
        {
            shiftLabel =
                "SHIFT*";
        }
    }

    button(
        0,180,
        60,60,
        shiftLabel,
        1
    );

    button(
        60,180,
        60,60,
        "SPACE",
        1
    );

    button(
        120,180,
        60,60,
        "DEL",
        1
    );

    button(
        180,180,
        60,60,
        "ENTER",
        1
    );

    if (!allowShift)
    {
        gfx->drawRoundRect(
            2,182,
            56,56,
            7,
            UI_MUTED
        );
    }
}

void ScreenRenderer::drawErgoChar(
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    char c
)
{
    bool upper =
        textProfile.capsLock ^
        textProfile.shiftOnce;

    if (
        upper &&
        c >= 'a' &&
        c <= 'z'
    )
    {
        c =
            c -
            'a' +
            'A';
    }

    char label[2] =
    {
        c,
        '\0'
    };

    button(
        x,y,
        w,h,
        label,
        3
    );
}

// ============================================================
// ABC ROOT
// ============================================================

void ScreenRenderer::drawTextAlpha()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (!textProfile.t9SecondPage)
    {
        // PAGE 1
        //
        // ABC | DEF
        // GHI | JKL

        button(
            0,40,
            120,70,
            "ABC",
            2
        );

        button(
            120,40,
            120,70,
            "DEF",
            2
        );

        button(
            0,110,
            120,70,
            "GHI",
            2
        );

        button(
            120,110,
            120,70,
            "JKL",
            2
        );
    }
    else
    {
        // PAGE 2
        //
        // MNO  | PQRS
        // TUV  | WXYZ

        button(
            0,40,
            120,70,
            "MNO",
            2
        );

        button(
            120,40,
            120,70,
            "PQRS",
            2
        );

        button(
            0,110,
            120,70,
            "TUV",
            2
        );

        button(
            120,110,
            120,70,
            "WXYZ",
            2
        );
    }

    drawKeyboardDock(
        true
    );
}

// ============================================================
// LETTER SUBGROUP
// ============================================================

void ScreenRenderer::drawTextGroup()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (!textProfile.selectedGroup)
    {
        drawKeyboardDock(
            true
        );

        return;
    }

    size_t n =
        strlen(
            textProfile.selectedGroup
        );

    // Three-letter T9 group:
    //
    // giant 80 x 140 columns
    if (n == 3)
    {
        for (
            int col = 0;
            col < 3;
            col++
        )
        {
            drawErgoChar(
                col * 80,
                40,
                80,
                140,
                textProfile.selectedGroup[
                    col
                ]
            );
        }
    }

    // Four-letter T9 group:
    //
    // giant 120 x 70 quadrants
    else if (n == 4)
    {
        for (
            int row = 0;
            row < 2;
            row++
        )
        {
            for (
                int col = 0;
                col < 2;
                col++
            )
            {
                int index =
                    row * 2 +
                    col;

                drawErgoChar(
                    col * 120,
                    40 +
                    row * 70,
                    120,
                    70,
                    textProfile.selectedGroup[
                        index
                    ]
                );
            }
        }
    }

    drawKeyboardDock(
        true
    );
}

// ============================================================
// NUMBERS
// ============================================================

void ScreenRenderer::drawNumbers()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (
        textProfile.selectedGroup ==
        nullptr
    )
    {
        button(
            0,40,
            120,70,
            "1-3",
            2
        );

        button(
            120,40,
            120,70,
            "4-6",
            2
        );

        button(
            0,110,
            120,70,
            "7-9",
            2
        );

        button(
            120,110,
            120,70,
            "0 . -",
            2
        );
    }
    else
    {
        for (
            int col = 0;
            col < 3;
            col++
        )
        {
            char label[2] =
            {
                textProfile.selectedGroup[col],
                '\0'
            };

            button(
                col * 80,
                40,
                80,
                140,
                label,
                3
            );
        }
    }

    drawKeyboardDock(
        false
    );
}

// ============================================================
// SYMBOL HELPERS
// ============================================================

void ScreenRenderer::drawThreeSymbols(
    const char *symbols
)
{
    for (
        int col = 0;
        col < 3;
        col++
    )
    {
        char label[2] =
        {
            symbols[col],
            '\0'
        };

        button(
            col * 80,
            40,
            80,
            140,
            label,
            3
        );
    }
}

void ScreenRenderer::drawFourSymbols(
    const char *symbols
)
{
    for (
        int row = 0;
        row < 2;
        row++
    )
    {
        for (
            int col = 0;
            col < 2;
            col++
        )
        {
            int index =
                row * 2 +
                col;

            char label[2] =
            {
                symbols[index],
                '\0'
            };

            button(
                col * 120,
                40 +
                row * 70,
                120,
                70,
                label,
                3
            );
        }
    }
}

// ============================================================
// SYMBOLS
// ============================================================

void ScreenRenderer::drawSymbols()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (
        textProfile.selectedGroup ==
        nullptr
    )
    {
        button(
            0,40,
            120,70,
            "PUNC",
            2
        );

        button(
            120,40,
            120,70,
            "WEB",
            2
        );

        button(
            0,110,
            120,70,
            "MATH",
            2
        );

        button(
            120,110,
            120,70,
            "MORE",
            2
        );

        drawKeyboardDock(
            false
        );

        return;
    }

    if (
        !strcmp(
            textProfile.selectedGroup,
            "PUNC"
        )
    )
    {
        button(
            0,40,
            120,140,
            ". , ? !",
            1
        );

        button(
            120,40,
            120,140,
            ": ; ' \"",
            1
        );

        drawKeyboardDock(
            false
        );

        return;
    }

    if (
        !strcmp(
            textProfile.selectedGroup,
            "WEB"
        )
    )
    {
        button(
            0,40,
            120,140,
            "@ # $",
            2
        );

        button(
            120,40,
            120,140,
            "% & _",
            2
        );

        drawKeyboardDock(
            false
        );

        return;
    }

    if (
        !strcmp(
            textProfile.selectedGroup,
            "MATH"
        )
    )
    {
        button(
            0,40,
            120,140,
            "+ - =",
            2
        );

        button(
            120,40,
            120,140,
            "* / ^",
            2
        );

        drawKeyboardDock(
            false
        );

        return;
    }

    if (
        !strcmp(
            textProfile.selectedGroup,
            "MORE"
        )
    )
    {
        button(
            0,40,
            120,70,
            "( ) [ ]",
            1
        );

        button(
            120,40,
            120,70,
            "{ } < >",
            1
        );

        button(
            0,110,
            240,70,
            "\\ | ` ~",
            1
        );

        drawKeyboardDock(
            false
        );

        return;
    }

    size_t n =
        strlen(
            textProfile.selectedGroup
        );

    if (n == 3)
    {
        drawThreeSymbols(
            textProfile.selectedGroup
        );
    }
    else if (n == 4)
    {
        drawFourSymbols(
            textProfile.selectedGroup
        );
    }

    drawKeyboardDock(
        false
    );
}

// ============================================================
// SETTINGS
// ============================================================

void ScreenRenderer::drawSettings()
{
    gfx->fillScreen(
        UI_BG
    );

    drawHome();

    centerText(
        "SETTINGS",
        145,
        12,
        2,
        UI_WHITE
    );

    centerText(
        "SENSITIVITY",
        120,
        49,
        1,
        UI_MUTED
    );

    button(
        18,70,
        50,42,
        "-",
        3
    );

    char sens[16];

    snprintf(
        sens,
        sizeof(sens),
        "%.0f%%",
        motion.sensitivityMultiplier *
        100.0f
    );

    centerText(
        sens,
        120,
        83,
        2,
        UI_WHITE
    );

    button(
        172,70,
        50,42,
        "+",
        3
    );

    button(
        20,126,
        200,38,
        motion.gyroEnabled
            ? "GYRO: ON"
            : "GYRO: OFF",
        1
    );

    button(
        20,173,
        200,38,
        motion.invertY
            ? "VERTICAL: INVERTED"
            : "VERTICAL: NORMAL",
        1
    );
}

// ============================================================
// ROUTER
// ============================================================

void ScreenRenderer::renderCurrentScreen()
{
    switch (
        profiles.screen
    )
    {
        case UIScreen::MOUSE:
            firstDisplayDraw =
                true;

            drawStaticUI();
            drawStatus();
            break;

        case UIScreen::MENU:
            drawMenu();
            break;

        case UIScreen::TEXT_ALPHA:
            drawTextAlpha();
            break;

        case UIScreen::TEXT_GROUP:
            drawTextGroup();
            break;

        case UIScreen::TEXT_NUMBERS:
            drawNumbers();
            break;

        case UIScreen::TEXT_SYMBOLS:
            drawSymbols();
            break;


        case UIScreen::SETTINGS:
            drawSettings();
            break;
    }
}

void ScreenRenderer::updateDisplay()
{
    if (profiles.profile == Profile::SPEAKER)
    {
        return;
    }

    if (
        profiles.screen !=
        UIScreen::MOUSE
    ) {
        return;
    }

    if (
        millis() -
        lastDisplayUpdate <
        100
    ) {
        return;
    }

    lastDisplayUpdate =
        millis();

    drawStatus();
}
// ============================================================
// REST LOCK
// ============================================================

