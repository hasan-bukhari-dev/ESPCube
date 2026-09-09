#include "ProfileManager.h"
#include "RuntimeGlobals.h"
#include "../ui/ScreenRenderer.h"
#include "../profiles/SpeakerStream.h"
#include <cstring>

void ProfileManager::goHome()
{
    // HOME is a teardown boundary for Speaker control resources.
    // Route Speaker teardown through its profile owner when active;
    // otherwise preserve the original defensive exitSafe() call.
    if (profiles.profile == Profile::SPEAKER)
        speakerProfile.exit();
    else
        speakerControl.exitSafe();

    profiles.profile =
        Profile::MOUSE;

    profiles.screen =
        UIScreen::MENU;

    textProfile.selectedGroup =
        nullptr;

    textProfile.shiftOnce =
        false;

    // HOME is the universal panic/recovery action.
    // Clear both keyboard and mouse HID state.
    hid.releaseKeyboard();
    hid.releaseMouseButtons();

    motion.mouseScrollGesture =
        false;

    motion.mouseScrollAccumulator =
        0.0f;

    motion.mouseScrollTilt =
        0.0f;

    motion.mouseScrollBPressedAt =
        0;

    motion.clearPointerMotion();

    firstDisplayDraw =
        true;

    renderer.renderCurrentScreen();

    Serial.println(
        "[UI] HOME -> LAUNCHER"
    );
}


bool ProfileManager::hit(
    int16_t px,
    int16_t py,
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h
) const
{
    return
        px >= x &&
        py >= y &&
        px < x + w &&
        py < y + h;
}


void ProfileManager::openMenu()
{
    profiles.screen =
        UIScreen::MENU;

    textProfile.selectedGroup =
        nullptr;

    motion.clearPointerMotion();

    renderer.renderCurrentScreen();
}

void ProfileManager::openTextAlpha()
{
    profiles.screen =
        UIScreen::TEXT_ALPHA;

    textProfile.selectedGroup =
        nullptr;

    motion.clearPointerMotion();

    renderer.renderCurrentScreen();
}

void ProfileManager::openTextGroup(
    const char *group
)
{
    textProfile.selectedGroup =
        group;

    profiles.screen =
        UIScreen::TEXT_GROUP;

    renderer.renderCurrentScreen();
}

// ============================================================
// TOUCH ROUTER
// ============================================================

bool ProfileManager::isTextScreen() const
{
    return
        profiles.screen == UIScreen::TEXT_ALPHA ||
        profiles.screen == UIScreen::TEXT_GROUP ||
        profiles.screen == UIScreen::TEXT_NUMBERS ||
        profiles.screen == UIScreen::TEXT_SYMBOLS;
}

bool ProfileManager::isAlphabetScreen() const
{
    return
        profiles.screen == UIScreen::TEXT_ALPHA ||
        profiles.screen == UIScreen::TEXT_GROUP;
}

void ProfileManager::openNumbers()
{
    profiles.screen =
        UIScreen::TEXT_NUMBERS;

    textProfile.selectedGroup =
        nullptr;

    textProfile.shiftOnce =
        false;

    motion.clearPointerMotion();

    renderer.renderCurrentScreen();
}

void ProfileManager::openSymbols()
{
    profiles.screen =
        UIScreen::TEXT_SYMBOLS;

    textProfile.selectedGroup =
        nullptr;

    textProfile.shiftOnce =
        false;

    motion.clearPointerMotion();

    renderer.renderCurrentScreen();
}

void ProfileManager::shiftTap()
{
    uint32_t now =
        millis();

    bool doubleTap =
        textProfile.lastShiftTapMs != 0 &&
        now - textProfile.lastShiftTapMs <=
            TextProfile::ShiftDoubleTapMs;

    if (doubleTap)
    {
        textProfile.capsLock =
            !textProfile.capsLock;

        textProfile.shiftOnce =
            false;

        textProfile.lastShiftTapMs =
            0;

        Serial.println(
            textProfile.capsLock
            ? "[TEXT] CAPS ON"
            : "[TEXT] CAPS OFF"
        );
    }
    else
    {
        textProfile.shiftOnce =
            !textProfile.shiftOnce;

        textProfile.lastShiftTapMs =
            now;

        Serial.println(
            textProfile.shiftOnce
            ? "[TEXT] SHIFT ON"
            : "[TEXT] SHIFT OFF"
        );
    }

    renderer.renderCurrentScreen();
}

// ============================================================
// TEXT TOP BAR
//
// x 0-59    HOME
// x 60-119  ABC/BACK
// x 120-179 123/BACK
// x 180-239 #+=/BACK
// ============================================================

bool ProfileManager::handleTextTopBar(
    int16_t x,
    int16_t y
)
{
    if (y >= 40)
        return false;

    // HOME
    if (x < 60)
    {
        goHome();
        return true;
    }

    // ABC / alphabet BACK / T9 page toggle
    if (x < 120)
    {
        // Inside an individual letter group:
        // ABC button behaves as BACK.
        if (
            profiles.screen ==
            UIScreen::TEXT_GROUP
        )
        {
            openTextAlpha();

            return true;
        }

        // Already at alphabet root:
        // toggle between the two T9 pages.
        if (
            profiles.screen ==
            UIScreen::TEXT_ALPHA
        )
        {
            textProfile.t9SecondPage =
                !textProfile.t9SecondPage;

            renderer.renderCurrentScreen();

            return true;
        }

        // Coming from numbers or symbols:
        // always enter alphabet at page 1.
        textProfile.t9SecondPage =
            false;

        openTextAlpha();

        return true;
    }

    // 123 / number BACK
    if (x < 180)
    {
        if (
            profiles.screen ==
                UIScreen::TEXT_NUMBERS &&
            textProfile.selectedGroup !=
                nullptr
        )
        {
            textProfile.selectedGroup =
                nullptr;

            renderer.renderCurrentScreen();
        }
        else
        {
            openNumbers();
        }

        return true;
    }

    // symbols / symbol BACK
    if (
        profiles.screen ==
            UIScreen::TEXT_SYMBOLS &&
        textProfile.selectedGroup !=
            nullptr
    )
    {
        textProfile.selectedGroup =
            nullptr;

        renderer.renderCurrentScreen();
    }
    else
    {
        openSymbols();
    }

    return true;
}

// ============================================================
// BOTTOM DOCK
//
// y 180-239
//
// x 0-59     SHIFT
// x 60-119   SPACE
// x 120-179  DEL
// x 180-239  ENTER
//
// NO DEAD PIXELS.
// ============================================================

bool ProfileManager::handleTextDock(
    int16_t x,
    int16_t y
)
{
    if (y < 180)
        return false;

    if (x < 60)
    {
        if (isAlphabetScreen())
        {
            shiftTap();
        }

        return true;
    }

    if (x < 120)
    {
        Serial.println(
            "[TEXT] TOUCH SPACE"
        );

        textProfile.sendSpace();

        return true;
    }

    if (x < 180)
    {
        Serial.println(
            "[TEXT] TOUCH DELETE"
        );

        textProfile.sendBackspace();

        return true;
    }

    Serial.println(
        "[TEXT] TOUCH ENTER"
    );

    textProfile.sendEnter();

    return true;
}

// ============================================================
// MAIN ROUTER
// ============================================================

void ProfileManager::handleTouchAction(
    int16_t x,
    int16_t y
)
{
    // Speaker owns its touch controls; dispatch remains synchronous.
    if (profile == Profile::SPEAKER && screen == UIScreen::MOUSE)
    {
        speakerProfile.handleTouch(x, y);
        return;
    }

    // Mic mode is controlled ONLY by physical Button B.
    if (textProfile.speechMicActive)
        return;

    // ========================================================
    // STANDARD MOUSE
    // ========================================================

    if (
        profiles.screen ==
        UIScreen::MOUSE
    )
    {
        openMenu();
        return;
    }

    // ========================================================
    // TEXT
    // ========================================================

    if (isTextScreen())
    {
        if (
            handleTextTopBar(
                x,
                y
            )
        )
        {
            return;
        }

        if (
            handleTextDock(
                x,
                y
            )
        )
        {
            return;
        }

        // ====================================================
        // ABC ROOT
        //
        // y 40-109:
        // A-F | G-L
        //
        // y 110-179:
        // M-R | S-Z
        // ====================================================

        if (
            profiles.screen ==
            UIScreen::TEXT_ALPHA
        )
        {
            bool left =
                x < 120;

            bool top =
                y < 110;

            // ================================================
            // T9 PAGE 1
            //
            // ABC | DEF
            // GHI | JKL
            // ================================================

            if (!textProfile.t9SecondPage)
            {
                if (
                    top &&
                    left
                )
                {
                    openTextGroup(
                        "abc"
                    );
                }
                else if (
                    top &&
                    !left
                )
                {
                    openTextGroup(
                        "def"
                    );
                }
                else if (
                    !top &&
                    left
                )
                {
                    openTextGroup(
                        "ghi"
                    );
                }
                else
                {
                    openTextGroup(
                        "jkl"
                    );
                }

                return;
            }

            // ================================================
            // T9 PAGE 2
            //
            // MNO  | PQRS
            // TUV  | WXYZ
            // ================================================

            if (
                top &&
                left
            )
            {
                openTextGroup(
                    "mno"
                );
            }
            else if (
                top &&
                !left
            )
            {
                openTextGroup(
                    "pqrs"
                );
            }
            else if (
                !top &&
                left
            )
            {
                openTextGroup(
                    "tuv"
                );
            }
            else
            {
                openTextGroup(
                    "wxyz"
                );
            }

            return;
        }

        // ====================================================
        // LETTER GROUP
        // ====================================================

        if (
            profiles.screen ==
                UIScreen::TEXT_GROUP &&
            textProfile.selectedGroup !=
                nullptr
        )
        {
            size_t n =
                strlen(
                    textProfile.selectedGroup
                );

            // ================================================
            // THREE-LETTER T9 GROUP
            //
            // ABC / DEF / GHI / JKL / MNO / TUV
            //
            // Three giant vertical zones:
            //
            // | A | B | C |
            //
            // Each = 80 x 140 pixels.
            // ================================================

            if (n == 3)
            {
                int col =
                    constrain(
                        x / 80,
                        0,
                        2
                    );

                char c =
                    textProfile.selectedGroup[
                        col
                    ];

                textProfile.sendCharacter(c);

                openTextAlpha();

                return;
            }

            // ================================================
            // FOUR-LETTER T9 GROUP
            //
            // PQRS / WXYZ
            //
            // P | Q
            // R | S
            //
            // Each = 120 x 70 pixels.
            // ================================================

            if (n == 4)
            {
                int col =
                    x < 120
                    ? 0
                    : 1;

                int row =
                    y < 110
                    ? 0
                    : 1;

                int index =
                    row * 2 +
                    col;

                char c =
                    textProfile.selectedGroup[
                        index
                    ];

                textProfile.sendCharacter(c);

                openTextAlpha();

                return;
            }

            return;
        }

        // ====================================================
        // NUMBERS
        // ====================================================

        if (
            profiles.screen ==
            UIScreen::TEXT_NUMBERS
        )
        {
            if (
                textProfile.selectedGroup ==
                nullptr
            )
            {
                bool left =
                    x < 120;

                bool top =
                    y < 110;

                if (
                    top &&
                    left
                )
                {
                    textProfile.selectedGroup =
                        "123";
                }
                else if (
                    top &&
                    !left
                )
                {
                    textProfile.selectedGroup =
                        "456";
                }
                else if (
                    !top &&
                    left
                )
                {
                    textProfile.selectedGroup =
                        "789";
                }
                else
                {
                    textProfile.selectedGroup =
                        "0.-";
                }

                renderer.renderCurrentScreen();

                return;
            }

            if (
                strlen(
                    textProfile.selectedGroup
                ) == 3
            )
            {
                int col =
                    constrain(
                        x / 80,
                        0,
                        2
                    );

                char c =
                    textProfile.selectedGroup[
                        col
                    ];

                textProfile.sendCharacter(c);

                textProfile.selectedGroup =
                    nullptr;

                renderer.renderCurrentScreen();

                return;
            }

            return;
        }

        // ====================================================
        // SYMBOLS
        // ====================================================

        if (
            profiles.screen ==
            UIScreen::TEXT_SYMBOLS
        )
        {
            // ROOT
            if (
                textProfile.selectedGroup ==
                nullptr
            )
            {
                bool left =
                    x < 120;

                bool top =
                    y < 110;

                if (
                    top &&
                    left
                )
                {
                    textProfile.selectedGroup =
                        "PUNC";
                }
                else if (
                    top &&
                    !left
                )
                {
                    textProfile.selectedGroup =
                        "WEB";
                }
                else if (
                    !top &&
                    left
                )
                {
                    textProfile.selectedGroup =
                        "MATH";
                }
                else
                {
                    textProfile.selectedGroup =
                        "MORE";
                }

                renderer.renderCurrentScreen();

                return;
            }

            // Punctuation split
            if (
                !strcmp(
                    textProfile.selectedGroup,
                    "PUNC"
                )
            )
            {
                textProfile.selectedGroup =
                    x < 120
                    ? ".,?!"
                    : ":;'\"";

                renderer.renderCurrentScreen();

                return;
            }

            // Web/common split
            if (
                !strcmp(
                    textProfile.selectedGroup,
                    "WEB"
                )
            )
            {
                textProfile.selectedGroup =
                    x < 120
                    ? "@#$"
                    : "%&_";

                renderer.renderCurrentScreen();

                return;
            }

            // Math split
            if (
                !strcmp(
                    textProfile.selectedGroup,
                    "MATH"
                )
            )
            {
                textProfile.selectedGroup =
                    x < 120
                    ? "+-="
                    : "*/^";

                renderer.renderCurrentScreen();

                return;
            }

            // More/programming split
            if (
                !strcmp(
                    textProfile.selectedGroup,
                    "MORE"
                )
            )
            {
                if (y < 110)
                {
                    textProfile.selectedGroup =
                        x < 120
                        ? "()[]"
                        : "{}<>";
                }
                else
                {
                    textProfile.selectedGroup =
                        "\\|`~";
                }

                renderer.renderCurrentScreen();

                return;
            }

            size_t n =
                strlen(
                    textProfile.selectedGroup
                );

            // 3 symbols = huge vertical thirds
            if (n == 3)
            {
                int col =
                    constrain(
                        x / 80,
                        0,
                        2
                    );

                char c =
                    textProfile.selectedGroup[
                        col
                    ];

                textProfile.sendCharacter(c);

                textProfile.selectedGroup =
                    nullptr;

                renderer.renderCurrentScreen();

                return;
            }

            // 4 symbols = huge quadrants
            if (n == 4)
            {
                int col =
                    x < 120
                    ? 0
                    : 1;

                int row =
                    y < 110
                    ? 0
                    : 1;

                int index =
                    row * 2 +
                    col;

                char c =
                    textProfile.selectedGroup[
                        index
                    ];

                textProfile.sendCharacter(c);

                textProfile.selectedGroup =
                    nullptr;

                renderer.renderCurrentScreen();

                return;
            }

            return;
        }

        return;
    }

    // ========================================================
    // GLOBAL HOME OUTSIDE TEXT
    // ========================================================

    if (
        profiles.screen !=
        UIScreen::MENU &&
        hit(
            x,y,
            4,4,
            54,30
        )
    )
    {
        goHome();
        return;
    }

    // ========================================================
    // LAUNCHER
    // ========================================================

    if (
        profiles.screen ==
        UIScreen::MENU
    )
    {
        // Speaker HOME navigation.
        if (hit(x,y,8,44,108,90))
        {
            mouseProfile.enter();
            renderer.renderCurrentScreen();
            return;
        }

        if (hit(x,y,124,44,108,90))
        {
            openTextAlpha();
            return;
        }

        if (hit(x,y,8,142,108,90))
        {
            // The launcher is reached through HOME, so mouse buttons are
            // already released. Run the Mouse lifecycle exit before the
            // Speaker lifecycle enter to keep ownership explicit.
            mouseProfile.exit();

            if (speakerProfile.enter())
            {
                renderer.renderCurrentScreen();
            }
            else
            {
                goHome();
            }
            return;
        }

        if (hit(x,y,124,142,108,90))
        {
            profiles.screen = UIScreen::SETTINGS;
            renderer.renderCurrentScreen();
            return;
        }

        return;
    }


    // ========================================================
    // SETTINGS
    // ========================================================

    if (screen == UIScreen::SETTINGS)
    {
        settingsProfile.handleTouch(x, y);
        return;
    }
}

void ProfileManager::updateTouch()
{
    TouchPoint point;
    if (touch.poll(point))
    {
        handleTouchAction(point.x, point.y);
    }
}

// ============================================================
// BUTTONS
// ============================================================

void ProfileManager::updateButtons()
{
    uint32_t now =
        millis();

    const ButtonSnapshot snapshot =
        buttons.sample();

    bool leftState = snapshot.left;
    bool middleState = snapshot.middle;
    bool rightState = snapshot.right;

    
    // ========================================================
    // Speaker physical controls.
    // A release = volume -
    // B press   = mute/unmute
    // C release = volume +
    // A+C hold  = HARD HOME
    // ========================================================
    if (profiles.profile == Profile::SPEAKER)
    {
        speakerProfile.handleButtons(leftState, middleState, rightState, now);
        return;
    }

    // ========================================================
    // HARD HOME: HOLD A + C
    // ========================================================

    if (homeGesture.update(
        leftState == LOW,
        rightState == LOW,
        now
    ))
    {
        hid.releaseMouseButtons();
        goHome();

        Serial.println(
            "[UI] HARD HOME A+C"
        );
    }

    // ========================================================
    // A
    // ========================================================

    if (
        leftState !=
        buttons.lastLeft &&
        now -
        buttons.lastLeftChange >=
        Buttons::DebounceMs
    )
    {
        buttons.lastLeftChange =
            now;

        buttons.lastLeft =
            leftState;

        bool pressed =
            leftState == LOW;

        if (!homeGesture.triggered())
        {
            if (
                profiles.screen ==
                UIScreen::TEXT_ALPHA ||
                profiles.screen ==
                UIScreen::TEXT_GROUP ||
                profiles.screen ==
                UIScreen::TEXT_NUMBERS ||
                profiles.screen ==
                UIScreen::TEXT_SYMBOLS
            )
            {
                if (pressed)
                    textProfile.sendBackspace();
            }
            else if (
                profiles.screen ==
                UIScreen::MOUSE &&
                profiles.profile ==
                Profile::MOUSE
            )
            {
                mouseProfile.handleButtonA(pressed);
            }
        }
    }

    // ========================================================
    // B
    //
    // TAP / HOLD SPEECH INPUT
    //
    // QUICK TAP:
    //   immediate capture begins
    //   release <250ms -> discard + SPACE
    //
    // SPEECH HOLD:
    //   capture begins at B-down
    //   at 250ms -> Speech Protocol START
    //   preroll is sent first
    //   live PCM is encoded/transmitted continuously
    //   release -> final audio + END
    //
    // NO WI-FI.
    // NO CLOUD TRANSCRIPTION.
    // ========================================================

    bool keyboardScreen =
        profiles.screen ==
            UIScreen::TEXT_ALPHA ||
        profiles.screen ==
            UIScreen::TEXT_GROUP ||
        profiles.screen ==
            UIScreen::TEXT_NUMBERS ||
        profiles.screen ==
            UIScreen::TEXT_SYMBOLS;

    if (
        middleState !=
        buttons.lastMiddle &&
        now -
        buttons.lastMiddleChange >=
        Buttons::DebounceMs
    )
    {
        buttons.lastMiddleChange =
            now;

        buttons.lastMiddle =
            middleState;

        bool pressed =
            middleState == LOW;

        // ====================================================
        // RELEASE CONFIRMED SPEECH SESSION
        // ====================================================

        if (
            !pressed &&
            textProfile.speechMicActive
        )
        {
            if (textProfile.speechCaptureStarted)
            {
                // stopRecording() performs one final captureTick().
                // Record its range so that final tail also reaches PC.
                size_t before =
                    speech.sampleCount();

                speech.stopRecording();

                size_t after =
                    speech.sampleCount();

                if (
                    speech.audioData() &&
                    after > before
                )
                {
                    speechLink.pushPcm(
                        speech.audioData() +
                        before,
                        after -
                        before
                    );
                }
            }

            speechLink.endSession();

            textProfile.speechCaptureStarted =
                false;

            textProfile.speechMicActive =
                false;

            textProfile.speechBPending =
                false;

            renderer.renderCurrentScreen();

            Serial.println(
                "[MIC] RELEASE -> keyboard restored"
            );

            Serial.println(
                "[STT] PC companion owns transcription."
            );
        }

        // ====================================================
        // KEYBOARD B
        // ====================================================

        else if (keyboardScreen)
        {
            if (pressed)
            {
                textProfile.speechBPressedAt =
                    now;

                textProfile.speechBPending =
                    true;

                textProfile.speechMicActive =
                    false;

                textProfile.speechCaptureStarted =
                    speech.startRecording();

                Serial.println(
                    "[MIC] B DOWN"
                );

                if (textProfile.speechCaptureStarted)
                {
                    Serial.println(
                        "[MIC] PREROLL CAPTURE"
                    );
                }
                else
                {
                    Serial.println(
                        "[MIC] PREROLL START FAILED"
                    );
                }
            }
            else if (
                textProfile.speechBPending
            )
            {
                // Quick tap:
                // captured preroll never enters Speech Protocol.
                if (textProfile.speechCaptureStarted)
                {
                    speech.stopRecording();
                }

                textProfile.speechCaptureStarted =
                    false;

                textProfile.speechBPending =
                    false;

                textProfile.speechMicActive =
                    false;

                textProfile.sendSpace();

                Serial.println(
                    "[TEXT] PHYSICAL B -> SPACE"
                );
            }
        }

        // ====================================================
        // NON-TEXT BEHAVIOR REMAINS STANDARD HID
        // ====================================================

        else
        {
            textProfile.speechBPending =
                false;

            textProfile.speechMicActive =
                false;

            textProfile.speechCaptureStarted =
                false;

            if (
                profiles.screen ==
                UIScreen::MOUSE &&
                profiles.profile ==
                Profile::MOUSE
            )
            {
                mouseProfile.handleButtonB(pressed, now);
            }
        }
    }

    // ========================================================
    // PREROLL / LIVE STREAM
    // ========================================================

    if (
        keyboardScreen &&
        middleState == LOW &&
        textProfile.speechBPending
    )
    {
        // ----------------------------------------------------
        // Before confirmation:
        // collect only into PSRAM preroll.
        // ----------------------------------------------------

        if (
            textProfile.speechCaptureStarted &&
            !textProfile.speechMicActive
        )
        {
            speech.captureTick();
        }

        // ----------------------------------------------------
        // 250ms:
        // start BLE session and immediately transmit all audio
        // captured since physical B-down.
        // ----------------------------------------------------

        if (
            !textProfile.speechMicActive &&
            now -
            textProfile.speechBPressedAt >=
            TextProfile::SpeechHoldMs
        )
        {
            if (textProfile.speechCaptureStarted)
            {
                textProfile.speechMicActive =
                    true;

                renderer.drawMicScreen();

                speechLink.startSession();

                if (
                    speech.audioData() &&
                    speech.sampleCount()
                )
                {
                    speechLink.pushPcm(
                        speech.audioData(),
                        speech.sampleCount()
                    );
                }

                Serial.println(
                    "[MIC] LISTENING"
                );
            }
            else
            {
                textProfile.speechBPending =
                    false;

                Serial.println(
                    "[MIC] START FAILED"
                );
            }
        }

        // ----------------------------------------------------
        // After confirmation:
        // capture new PCM and send it immediately.
        // ----------------------------------------------------

        if (
            textProfile.speechCaptureStarted &&
            textProfile.speechMicActive
        )
        {
            int16_t livePcm[128];

            size_t liveCount =
                speech.captureMono(
                    livePcm,
                    128
                );

            if (liveCount)
            {
                speechLink.pushPcm(
                    livePcm,
                    liveCount
                );
            }
        }
    }
    // ========================================================
    // C
    // ========================================================

    if (
        rightState !=
        buttons.lastRight &&
        now -
        buttons.lastRightChange >=
        Buttons::DebounceMs
    )
    {
        buttons.lastRightChange =
            now;

        buttons.lastRight =
            rightState;

        bool pressed =
            rightState == LOW;

        if (!homeGesture.triggered())
        {
            if (
                profiles.screen ==
                UIScreen::TEXT_ALPHA ||
                profiles.screen ==
                UIScreen::TEXT_GROUP ||
                profiles.screen ==
                UIScreen::TEXT_NUMBERS ||
                profiles.screen ==
                UIScreen::TEXT_SYMBOLS
            )
            {
                if (pressed)
                    textProfile.sendEnter();
            }
            else if (
                profiles.screen ==
                UIScreen::MOUSE &&
                profiles.profile ==
                Profile::MOUSE
            )
            {
                mouseProfile.handleButtonC(pressed);
            }
        }
    }
}

// ============================================================
// GYRO ENGINE
// ============================================================
