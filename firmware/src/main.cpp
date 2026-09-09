#include <Arduino.h>
#include <Wire.h>
#include <ESPCubeHID.h>
#include <ESPCubeSpeech.h>
#include <ESPCubeSpeakerB1Test.h>
#include <ESPCubeSpeechLink.h>
#include <ESPCubeSpeakerControl.h>
#include <ESPCubeSpeakerPlayback.h>
#include <ESPCubeSpeakerVolume.h>

#include <ESPCubeSpeakerPcmRingBuffer.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <QMI8658.h>
#include <Arduino_GFX_Library.h>

// ============================================================
// ESPCube v0.4
//
// Standard Mode:
// - Gyro mouse
// - A = left click
// - B = middle click
// - C = right click
// - 240x240 display status UI
//
// IMPORTANT:
// Gyro engine is preserved from known-good v0.3.3.
// ============================================================

// ============================================================
// BUTTONS
// ============================================================

static constexpr uint8_t BTN_LEFT   = 0; // A
static constexpr uint8_t BTN_MIDDLE = 5; // B
static constexpr uint8_t BTN_RIGHT  = 4; // C

// ============================================================
// IMU / I2C
// ============================================================

static constexpr uint8_t I2C_SDA = 42;
static constexpr uint8_t I2C_SCL = 41;

// Touch controller
static constexpr uint8_t TOUCH_RST  = 47;
static constexpr uint8_t TOUCH_INT  = 48;
static constexpr uint8_t TOUCH_ADDR = 0x15;

// ============================================================
// DISPLAY
// ============================================================

static constexpr uint8_t LCD_CS   = 21;
static constexpr uint8_t LCD_CLK  = 38;
static constexpr uint8_t LCD_MOSI = 39;
static constexpr uint8_t LCD_RST  = 40;
static constexpr uint8_t LCD_DC   = 45;
static constexpr uint8_t LCD_BL   = 46;

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(
    LCD_DC,
    LCD_CS,
    LCD_CLK,
    LCD_MOSI,
    GFX_NOT_DEFINED
);

Arduino_GFX *gfx = new Arduino_ST7789(
    lcdBus,
    LCD_RST,
    3,      // 90 degrees counterclockwise
    true,
    240,
    240,
    0,      // column offset, normal orientation
    0,      // row offset, normal orientation
    0,      // column offset, rotated orientation
    80      // row offset, rotated orientation
);

// ============================================================
// DEVICES
// ============================================================

ESPCubeHID hid;
ESPCubeSpeech speech;
ESPCubeSpeechLink speechLink;
ESPCubeSpeakerControl speakerControl;

// S2-D0 compile/link proof only.
// Not invoked yet; runtime behavior remains unchanged.
ESPCubeSpeakerPlayback speakerPlayback;

// ============================================================
// D2D1 V3 SPEAKER VOLUME
// 100% == frozen D2C9 maximum. attenuation only.
// ============================================================
ESPCubeSpeakerVolume speakerVolume;
uint8_t speakerVolumePercent = 100;
bool speakerVolumeMuted = false;
// ============================================================
// S2-D2A ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â PSRAM BUFFERED TCP PCM
// ============================================================
//
// D1 remains compiled below as the rollback oracle, but loop()
// calls serviceSpeakerTcpD2A() instead.
//
// PCM format remains:
//   32 kHz
//   mono
//   signed PCM16 little-endian
//
// Buffer:
//   128 KiB PSRAM
//   12 x 10 ms chunks prebuffer = 120 ms
// ============================================================

static constexpr size_t D2A_BUFFER_CAPACITY =
    128 * 1024;

static constexpr size_t D2A_PREBUFFER_BYTES =
    12 * 320 * sizeof(int16_t);

static constexpr uint32_t D2A_UNDERRUN_GRACE_MS =
    14;

ESPCubeSpeakerPcmRingBuffer speakerPcmBuffer;

uint32_t d2aTcpBytesRx = 0;
uint32_t d2aPcmBytesPlayed = 0;
uint32_t d2aChunksPlayed = 0;
uint32_t d2aBufferUnderruns = 0;
uint32_t d2aBufferOverflows = 0;

bool d2aInputEnded = false;
bool d2aPlaybackStarted = false;
bool d2aUnderrunLatched = false;

uint32_t d2aLastWriteMs = 0;


// S2-D0 compile/link proof only.
// Not invoked yet; runtime behavior remains unchanged.

// ============================================================
// S2-D1 TCP PCM PROOF
// ============================================================

static constexpr uint16_t TCP_PORT = 47821;

// 32 kHz mono PCM16.
// One proof chunk = exactly 10 ms.
static constexpr size_t TCP_PCM_FRAMES = 320;
static constexpr size_t TCP_PCM_BYTES =
    TCP_PCM_FRAMES * sizeof(int16_t);

int speakerTcpServer = -1;
int speakerTcpClient = -1;

bool speakerTcpListening = false;

uint8_t speakerTcpRx[TCP_PCM_BYTES] = {};
size_t speakerTcpRxFill = 0;

uint32_t speakerTcpBytesPlayed = 0;
uint32_t speakerTcpChunksPlayed = 0;

QMI8658 imu;

// ============================================================
// TIMING
// ============================================================

static constexpr uint32_t SAMPLE_INTERVAL_US = 8000;
static constexpr uint32_t DEBOUNCE_MS = 18;

// ============================================================
// BUTTON STATE
// ============================================================

bool lastLeft   = HIGH;
bool lastMiddle = HIGH;
bool lastRight  = HIGH;

uint32_t lastLeftChange   = 0;
uint32_t lastMiddleChange = 0;
uint32_t lastRightChange  = 0;

// ============================================================
// IMU VALIDATION
// ============================================================

static constexpr float MAX_VALID_DPS = 470.0f;

uint32_t goodSamples = 0;
uint32_t rejectedSamples = 0;

// ============================================================
// BIAS
// ============================================================

float biasY = 0.0f;
float biasZ = 0.0f;

float prevRawY = 0.0f;
float prevRawZ = 0.0f;

bool havePrevious = false;

// ============================================================
// MEDIAN FILTER
// ============================================================

float histY[3] = {0, 0, 0};
float histZ[3] = {0, 0, 0};

uint8_t historyIndex = 0;
uint8_t historyCount = 0;

float median3(float a, float b, float c)
{
    if (a > b) {
        float t = a; a = b; b = t;
    }

    if (b > c) {
        float t = b; b = c; c = t;
    }

    if (a > b) {
        float t = a; a = b; b = t;
    }

    return b;
}

void addHistory(float y, float z)
{
    histY[historyIndex] = y;
    histZ[historyIndex] = z;

    historyIndex = (historyIndex + 1) % 3;

    if (historyCount < 3)
        historyCount++;
}

// ============================================================
// REST DETECTION
// ============================================================

static constexpr float REST_DELTA_Y = 1.35f;
static constexpr float REST_DELTA_Z = 1.35f;

// Small hand jitter is allowed without throwing away
// all evidence that the cube is resting.
static constexpr float REST_SOFT_DELTA = 2.60f;

// Corrected motion must also be reasonably small before
// automatic drift correction is allowed.
static constexpr float REST_CAPTURE_DPS = 9.0f;

static constexpr float REST_EXIT_DPS = 5.0f;

static constexpr int REST_SCORE_MAX   = 32;
static constexpr int REST_SCORE_ENTER = 22;

int restScore = 0;

bool stationary = false;

uint32_t quietSinceMs = 0;

// ============================================================
// BIAS LEARNING
// ============================================================

static constexpr float REST_SNAP_RATE = 0.55f;
static constexpr float REST_TRACK_RATE = 0.035f;

// ============================================================
// MICRO-DRIFT ABSORBER
//
// Used while not fully in REST.
//
// If the gyro is barely changing and the remaining corrected
// motion is tiny, treat it as bias drift instead of intentional
// pointing movement.
// ============================================================

static constexpr float MICRO_DRIFT_MAX_DPS = 4.0f;
static constexpr float MICRO_DRIFT_DELTA   = 1.50f;

// Very slow correction so intentional fine movement is preserved.
static constexpr float MICRO_BIAS_RATE = 0.008f;

// ============================================================
// POINTER TUNING
// ============================================================

static constexpr float HORIZONTAL_SIGN = +1.0f;
static constexpr float VERTICAL_SIGN   = -1.0f;

static constexpr float DEADZONE_HORIZONTAL = 2.2f;
static constexpr float DEADZONE_VERTICAL   = 1.8f;

static constexpr float BASE_SENSITIVITY = 15.0f;

static constexpr float ALPHA_MIN = 0.18f;
static constexpr float ALPHA_MAX = 0.72f;
static constexpr float ALPHA_SPEED_SCALE = 0.010f;

static constexpr float MAX_ACCEL = 1.55f;
static constexpr float ACCEL_FULL_AT_DPS = 160.0f;

float filteredX = 0.0f;
float filteredY = 0.0f;

float remainderX = 0.0f;
float remainderY = 0.0f;

uint32_t lastSampleUs = 0;
uint32_t lastStatusMs = 0;

// ============================================================
// DISPLAY STATE
// ============================================================

bool lastDisplayPaired = false;
bool lastDisplayStationary = false;
bool firstDisplayDraw = true;

uint32_t lastDisplayUpdate = 0;

// ============================================================
// TOUCH + PRODUCT UI STATE
// ============================================================

volatile bool touchPending = false;

uint32_t lastTouchActionMs = 0;

static constexpr uint32_t TOUCH_ACTION_COOLDOWN_MS = 220;

// ============================================================
// SCREENS
// ============================================================

enum class UIScreen : uint8_t
{
    MOUSE,
    MENU,

    TEXT_ALPHA,
    TEXT_GROUP,
    TEXT_NUMBERS,
    TEXT_SYMBOLS,

    SETTINGS
};

enum class Profile : uint8_t
{
    MOUSE,
    SPEAKER
};

UIScreen currentScreen =
    UIScreen::MOUSE;

Profile currentProfile =
    Profile::MOUSE;

// ============================================================
// TEXT
// ============================================================

const char *selectedGroup =
    nullptr;

bool shiftOnce =
    false;

bool capsLock =
    false;

// T9 alphabet page:
//
// PAGE 1:
// ABC | DEF
// GHI | JKL
//
// PAGE 2:
// MNO  | PQRS
// TUV  | WXYZ
bool t9SecondPage =
    false;

// ============================================================
// PHYSICAL BUTTON B SPEECH
// ============================================================

static constexpr uint32_t SPEECH_HOLD_MS =
    250;

uint32_t speechBPressedAt =
    0;

bool speechBPending =
    false;

bool speechMicActive =
    false;

// Audio capture begins immediately on physical B-down.
// speechMicActive becomes true only after the 250ms
// hold threshold confirms this is speech rather than Space.
bool speechCaptureStarted =
    false;

// ============================================================
// MOUSE B + GYRO SCROLL
//
// MOUSE screen only:
//
// quick B tap
//     -> ordinary middle click
//
// B held + deliberate vertical gyro motion
//     -> suppress pointer
//     -> convert vertical motion to mouse-wheel reports
//
// TEXT screens retain the existing Space / Speech behavior.
// ============================================================

// Short delay prevents tiny motion during an ordinary click
// from accidentally becoming a scroll gesture.
static constexpr uint32_t MOUSE_SCROLL_ARM_MS =
    90;

// Vertical motion required to convert the pending B gesture
// into scrolling.
static constexpr float MOUSE_SCROLL_ACTIVATE_DPS =
    5.0f;

// Integrated angular motion per wheel notch.
// Larger value = slower / more controlled scrolling.
static constexpr float MOUSE_SCROLL_NOTCH =
    1.60f;

uint32_t mouseScrollBPressedAt =
    0;

bool mouseScrollGesture =
    false;

float mouseScrollAccumulator =
    0.0f;

// Integrated vertical tilt proxy while B is held.
//
// This lets the scroll continue even when physical rotation stops.
// The farther the Cube is held from the gesture start orientation,
// the faster scrolling becomes.
float mouseScrollTilt =
    0.0f;

// ============================================================
// HYBRID SCROLL
//
// Under ~90 degrees:
//     preserve original movement-based gyro scrolling.
//
// Beyond ~90 degrees:
//     keep the original response PLUS add continuous scrolling
//     while the Cube remains held past the threshold.
// ============================================================

static constexpr float MOUSE_SCROLL_CONTINUOUS_START =
    20.0f;

// At this relative rotation or beyond, continuous scrolling has
// reached maximum speed.
static constexpr float MOUSE_SCROLL_CONTINUOUS_FULL =
    80.0f;

// Continuous scroll speed just after crossing ~90 degrees.
static constexpr float MOUSE_SCROLL_CONTINUOUS_MIN_RATE =
    30.0f;

// Maximum continuous wheel notches per second.
static constexpr float MOUSE_SCROLL_CONTINUOUS_MAX_RATE =
    65.0f;

uint32_t lastShiftTapMs =
    0;

static constexpr uint32_t SHIFT_DOUBLE_TAP_MS =
    450;

char textPreview[22] =
{
    0
};

uint8_t textPreviewLen =
    0;

// ============================================================
// SETTINGS
// ============================================================

float sensitivityMultiplier =
    1.0f;

bool gyroEnabled =
    true;

bool invertY =
    false;

// ============================================================
// UNIVERSAL HOME
// ============================================================

uint32_t homeComboSince =
    0;

bool homeComboTriggered =
    false;

static constexpr uint32_t HOME_COMBO_MS =
    800;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void drawStaticUI();
void drawSpeakerProfileUI();

// D2D1 V4 REPAIR APPLIED
void drawStatus();

void renderCurrentScreen();
void goHome();

void serviceSpeakerTcp();
void serviceSpeakerTcpD2A();
void stopSpeakerTcpPlayback();
// ============================================================
// HELPERS
// ============================================================

float softDeadzone(float value, float dz)
{
    float a = fabsf(value);

    if (a <= dz)
        return 0.0f;

    return copysignf(a - dz, value);
}

float adaptiveFilter(float raw, float previous)
{
    float speed = fabsf(raw);

    float alpha =
        ALPHA_MIN +
        speed * ALPHA_SPEED_SCALE;

    alpha = constrain(
        alpha,
        ALPHA_MIN,
        ALPHA_MAX
    );

    return previous +
           alpha * (raw - previous);
}

float accelerationFor(float speed)
{
    float t =
        constrain(
            speed / ACCEL_FULL_AT_DPS,
            0.0f,
            1.0f
        );

    t = t * t * (3.0f - 2.0f * t);

    return 1.0f +
           (MAX_ACCEL - 1.0f) * t;
}

// ============================================================
// SAMPLE VALIDATION
// ============================================================

bool sampleValid(float gx, float gy, float gz)
{
    if (
        !isfinite(gx) ||
        !isfinite(gy) ||
        !isfinite(gz)
    )
        return false;

    if (
        fabsf(gx) > MAX_VALID_DPS ||
        fabsf(gy) > MAX_VALID_DPS ||
        fabsf(gz) > MAX_VALID_DPS
    )
        return false;

    if (
        fabsf(gx) < 0.0001f &&
        fabsf(gy) < 0.0001f &&
        fabsf(gz) < 0.0001f
    )
        return false;

    if (
        fabsf(gx - gy) < 0.001f &&
        fabsf(gy - gz) < 0.001f &&
        fabsf(gx) > 30.0f
    )
        return false;

    return true;
}

// ============================================================
// POINTER RESET
// ============================================================

void clearPointerMotion()
{
    filteredX = 0.0f;
    filteredY = 0.0f;

    remainderX = 0.0f;
    remainderY = 0.0f;

    historyCount = 0;
    historyIndex = 0;
}

// ============================================================
// TOUCHSCREEN
// ============================================================

void IRAM_ATTR onTouchInterrupt()
{
    touchPending = true;
}

bool touchReadRegister(
    uint8_t reg,
    uint8_t *buffer,
    size_t length
)
{
    Wire.beginTransmission(
        TOUCH_ADDR
    );

    Wire.write(reg);

    // LOCKED v0.4.3b repeated-start transaction.
    if (
        Wire.endTransmission(false)
        != 0
    ) {
        return false;
    }

    size_t received =
        Wire.requestFrom(
            (uint8_t)TOUCH_ADDR,
            length,
            true
        );

    if (received != length)
        return false;

    for (
        size_t i = 0;
        i < length;
        i++
    )
    {
        if (!Wire.available())
            return false;

        buffer[i] =
            Wire.read();
    }

    return true;
}

bool touchReadPoint(
    uint16_t &rawX,
    uint16_t &rawY,
    uint8_t &points
)
{
    uint8_t data[5];

    if (!touchReadRegister(
        0x02,
        data,
        5
    )) {
        return false;
    }

    points =
        data[0] & 0x0F;

    rawX =
        ((uint16_t)(
            data[1] &
            0x0F
        ) << 8)
        |
        data[2];

    rawY =
        ((uint16_t)(
            data[3] &
            0x0F
        ) << 8)
        |
        data[4];

    return true;
}

void transformTouch(
    uint16_t rawX,
    uint16_t rawY,
    int16_t &screenX,
    int16_t &screenY
)
{
    // LOCKED v0.4.3b transform.
    screenX =
        239 -
        (int16_t)rawY;

    screenY =
        (int16_t)rawX;

    screenX =
        constrain(
            screenX,
            0,
            239
        );

    screenY =
        constrain(
            screenY,
            0,
            239
        );
}

bool hit(
    int16_t px,
    int16_t py,
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h
)
{
    return
        px >= x &&
        py >= y &&
        px < x + w &&
        py < y + h;
}

// ============================================================
// TEXT HELPERS
// ============================================================

void previewPush(char c)
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

void previewDelete()
{
    if (!textPreviewLen)
        return;

    textPreviewLen--;

    textPreview[
        textPreviewLen
    ] = '\0';
}

void sendCharacter(char c)
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

    renderCurrentScreen();
}

void sendBackspace()
{
    hid.backspace();

    previewDelete();

    renderCurrentScreen();
}

void sendSpace()
{
    hid.space();

    previewPush(' ');

    renderCurrentScreen();
}

void sendEnter()
{
    hid.enter();

    textPreviewLen =
        0;

    textPreview[0] =
        '\0';

    renderCurrentScreen();
}

void typeSpeechTranscript(
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

    renderCurrentScreen();
}

// ============================================================
// NAVIGATION
// ============================================================

void goHome()
{
    // S2-A3 SPEAKER CONTROL: every HOME is a teardown boundary.
    speakerControl.exitSafe();

    currentProfile =
        Profile::MOUSE;

    currentScreen =
        UIScreen::MENU;

    selectedGroup =
        nullptr;

    shiftOnce =
        false;

    // HOME is the universal panic/recovery action.
    // Clear both keyboard and mouse HID state.
    hid.releaseKeyboard();
    hid.releaseMouseButtons();

    mouseScrollGesture =
        false;

    mouseScrollAccumulator =
        0.0f;

    mouseScrollTilt =
        0.0f;

    mouseScrollBPressedAt =
        0;

    clearPointerMotion();

    firstDisplayDraw =
        true;

    renderCurrentScreen();

    Serial.println(
        "[UI] HOME -> LAUNCHER"
    );
}

void openMenu()
{
    currentScreen =
        UIScreen::MENU;

    selectedGroup =
        nullptr;

    clearPointerMotion();

    renderCurrentScreen();
}

void openTextAlpha()
{
    currentScreen =
        UIScreen::TEXT_ALPHA;

    selectedGroup =
        nullptr;

    clearPointerMotion();

    renderCurrentScreen();
}

void openTextGroup(
    const char *group
)
{
    selectedGroup =
        group;

    currentScreen =
        UIScreen::TEXT_GROUP;

    renderCurrentScreen();
}

// ============================================================
// TOUCH ROUTER
// ============================================================

bool ergonomicIsTextScreen()
{
    return
        currentScreen == UIScreen::TEXT_ALPHA ||
        currentScreen == UIScreen::TEXT_GROUP ||
        currentScreen == UIScreen::TEXT_NUMBERS ||
        currentScreen == UIScreen::TEXT_SYMBOLS;
}

bool ergonomicIsAlphabetScreen()
{
    return
        currentScreen == UIScreen::TEXT_ALPHA ||
        currentScreen == UIScreen::TEXT_GROUP;
}

void ergonomicOpenNumbers()
{
    currentScreen =
        UIScreen::TEXT_NUMBERS;

    selectedGroup =
        nullptr;

    shiftOnce =
        false;

    clearPointerMotion();

    renderCurrentScreen();
}

void ergonomicOpenSymbols()
{
    currentScreen =
        UIScreen::TEXT_SYMBOLS;

    selectedGroup =
        nullptr;

    shiftOnce =
        false;

    clearPointerMotion();

    renderCurrentScreen();
}

void ergonomicShiftTap()
{
    uint32_t now =
        millis();

    bool doubleTap =
        lastShiftTapMs != 0 &&
        now - lastShiftTapMs <=
            SHIFT_DOUBLE_TAP_MS;

    if (doubleTap)
    {
        capsLock =
            !capsLock;

        shiftOnce =
            false;

        lastShiftTapMs =
            0;

        Serial.println(
            capsLock
            ? "[TEXT] CAPS ON"
            : "[TEXT] CAPS OFF"
        );
    }
    else
    {
        shiftOnce =
            !shiftOnce;

        lastShiftTapMs =
            now;

        Serial.println(
            shiftOnce
            ? "[TEXT] SHIFT ON"
            : "[TEXT] SHIFT OFF"
        );
    }

    renderCurrentScreen();
}

// ============================================================
// TEXT TOP BAR
//
// x 0-59    HOME
// x 60-119  ABC/BACK
// x 120-179 123/BACK
// x 180-239 #+=/BACK
// ============================================================

bool ergonomicHandleTopBar(
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
            currentScreen ==
            UIScreen::TEXT_GROUP
        )
        {
            openTextAlpha();

            return true;
        }

        // Already at alphabet root:
        // toggle between the two T9 pages.
        if (
            currentScreen ==
            UIScreen::TEXT_ALPHA
        )
        {
            t9SecondPage =
                !t9SecondPage;

            renderCurrentScreen();

            return true;
        }

        // Coming from numbers or symbols:
        // always enter alphabet at page 1.
        t9SecondPage =
            false;

        openTextAlpha();

        return true;
    }

    // 123 / number BACK
    if (x < 180)
    {
        if (
            currentScreen ==
                UIScreen::TEXT_NUMBERS &&
            selectedGroup !=
                nullptr
        )
        {
            selectedGroup =
                nullptr;

            renderCurrentScreen();
        }
        else
        {
            ergonomicOpenNumbers();
        }

        return true;
    }

    // symbols / symbol BACK
    if (
        currentScreen ==
            UIScreen::TEXT_SYMBOLS &&
        selectedGroup !=
            nullptr
    )
    {
        selectedGroup =
            nullptr;

        renderCurrentScreen();
    }
    else
    {
        ergonomicOpenSymbols();
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

bool ergonomicHandleDock(
    int16_t x,
    int16_t y
)
{
    if (y < 180)
        return false;

    if (x < 60)
    {
        if (ergonomicIsAlphabetScreen())
        {
            ergonomicShiftTap();
        }

        return true;
    }

    if (x < 120)
    {
        Serial.println(
            "[TEXT] TOUCH SPACE"
        );

        sendSpace();

        return true;
    }

    if (x < 180)
    {
        Serial.println(
            "[TEXT] TOUCH DELETE"
        );

        sendBackspace();

        return true;
    }

    Serial.println(
        "[TEXT] TOUCH ENTER"
    );

    sendEnter();

    return true;
}

// ============================================================
// MAIN ROUTER
// ============================================================

void handleTouchAction(
    int16_t x,
    int16_t y
)
{
    // ========================================================
    // D2D2 SPEAKER HARDENING Ã¢â‚¬â€ forgiving touch targets
    // ========================================================
    if (
        currentProfile == Profile::SPEAKER &&
        currentScreen == UIScreen::MOUSE
    )
    {
        if (hit(x,y,0,0,74,48))
        {
            goHome();
            return;
        }

        if (
            y >= 88 &&
            y <= 138
        )
        {
            if (x <= 20)
            {
                speakerVolumePercent = 0;
            }
            else if (x >= 219)
            {
                speakerVolumePercent = 100;
            }
            else
            {
                speakerVolumePercent =
                    static_cast<uint8_t>(
                        constrain(
                            map(
                                x,
                                20,
                                219,
                                0,
                                100
                            ),
                            0,
                            100
                        )
                    );
            }

            speakerVolume.setPercent(
                speakerVolumePercent
            );

            drawSpeakerProfileUI();
            return;
        }

        if (hit(x,y,20,140,200,82))
        {
            speakerVolumeMuted =
                !speakerVolumeMuted;

            speakerVolume.setMuted(
                speakerVolumeMuted
            );

            drawSpeakerProfileUI();
            return;
        }

        return;
    }

    // Mic mode is controlled ONLY by physical Button B.
    if (speechMicActive)
        return;

    // ========================================================
    // STANDARD MOUSE
    // ========================================================

    if (
        currentScreen ==
        UIScreen::MOUSE
    )
    {
        openMenu();
        return;
    }

    // ========================================================
    // TEXT
    // ========================================================

    if (ergonomicIsTextScreen())
    {
        if (
            ergonomicHandleTopBar(
                x,
                y
            )
        )
        {
            return;
        }

        if (
            ergonomicHandleDock(
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
            currentScreen ==
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

            if (!t9SecondPage)
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
            currentScreen ==
                UIScreen::TEXT_GROUP &&
            selectedGroup !=
                nullptr
        )
        {
            size_t n =
                strlen(
                    selectedGroup
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
                    selectedGroup[
                        col
                    ];

                sendCharacter(c);

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
                    selectedGroup[
                        index
                    ];

                sendCharacter(c);

                openTextAlpha();

                return;
            }

            return;
        }

        // ====================================================
        // NUMBERS
        // ====================================================

        if (
            currentScreen ==
            UIScreen::TEXT_NUMBERS
        )
        {
            if (
                selectedGroup ==
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
                    selectedGroup =
                        "123";
                }
                else if (
                    top &&
                    !left
                )
                {
                    selectedGroup =
                        "456";
                }
                else if (
                    !top &&
                    left
                )
                {
                    selectedGroup =
                        "789";
                }
                else
                {
                    selectedGroup =
                        "0.-";
                }

                renderCurrentScreen();

                return;
            }

            if (
                strlen(
                    selectedGroup
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
                    selectedGroup[
                        col
                    ];

                sendCharacter(c);

                selectedGroup =
                    nullptr;

                renderCurrentScreen();

                return;
            }

            return;
        }

        // ====================================================
        // SYMBOLS
        // ====================================================

        if (
            currentScreen ==
            UIScreen::TEXT_SYMBOLS
        )
        {
            // ROOT
            if (
                selectedGroup ==
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
                    selectedGroup =
                        "PUNC";
                }
                else if (
                    top &&
                    !left
                )
                {
                    selectedGroup =
                        "WEB";
                }
                else if (
                    !top &&
                    left
                )
                {
                    selectedGroup =
                        "MATH";
                }
                else
                {
                    selectedGroup =
                        "MORE";
                }

                renderCurrentScreen();

                return;
            }

            // Punctuation split
            if (
                !strcmp(
                    selectedGroup,
                    "PUNC"
                )
            )
            {
                selectedGroup =
                    x < 120
                    ? ".,?!"
                    : ":;'\"";

                renderCurrentScreen();

                return;
            }

            // Web/common split
            if (
                !strcmp(
                    selectedGroup,
                    "WEB"
                )
            )
            {
                selectedGroup =
                    x < 120
                    ? "@#$"
                    : "%&_";

                renderCurrentScreen();

                return;
            }

            // Math split
            if (
                !strcmp(
                    selectedGroup,
                    "MATH"
                )
            )
            {
                selectedGroup =
                    x < 120
                    ? "+-="
                    : "*/^";

                renderCurrentScreen();

                return;
            }

            // More/programming split
            if (
                !strcmp(
                    selectedGroup,
                    "MORE"
                )
            )
            {
                if (y < 110)
                {
                    selectedGroup =
                        x < 120
                        ? "()[]"
                        : "{}<>";
                }
                else
                {
                    selectedGroup =
                        "\\|`~";
                }

                renderCurrentScreen();

                return;
            }

            size_t n =
                strlen(
                    selectedGroup
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
                    selectedGroup[
                        col
                    ];

                sendCharacter(c);

                selectedGroup =
                    nullptr;

                renderCurrentScreen();

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
                    selectedGroup[
                        index
                    ];

                sendCharacter(c);

                selectedGroup =
                    nullptr;

                renderCurrentScreen();

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
        currentScreen !=
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
        currentScreen ==
        UIScreen::MENU
    )
    {
        // S2-A6R3 HOME SPEAKER NAV
        if (hit(x,y,8,44,108,90))
        {
            currentProfile = Profile::MOUSE;
            currentScreen = UIScreen::MOUSE;
            clearPointerMotion();
            renderCurrentScreen();
            return;
        }

        if (hit(x,y,124,44,108,90))
        {
            openTextAlpha();
            return;
        }

        if (hit(x,y,8,142,108,90))
        {
            if (speakerControl.enter())
            {
                currentProfile = Profile::SPEAKER;
                currentScreen = UIScreen::MOUSE;
                clearPointerMotion();
                renderCurrentScreen();
            }
            else
            {
                speakerControl.exitSafe();
                goHome();
            }
            return;
        }

        if (hit(x,y,124,142,108,90))
        {
            currentScreen = UIScreen::SETTINGS;
            renderCurrentScreen();
            return;
        }

        return;
    }


    // ========================================================
    // SETTINGS
    // ========================================================

    if (
        currentScreen ==
        UIScreen::SETTINGS
    )
    {
        if (
            hit(
                x,y,
                18,70,
                50,42
            )
        )
        {
            sensitivityMultiplier -=
                0.10f;

            sensitivityMultiplier =
                constrain(
                    sensitivityMultiplier,
                    0.50f,
                    2.00f
                );

            renderCurrentScreen();

            return;
        }

        if (
            hit(
                x,y,
                172,70,
                50,42
            )
        )
        {
            sensitivityMultiplier +=
                0.10f;

            sensitivityMultiplier =
                constrain(
                    sensitivityMultiplier,
                    0.50f,
                    2.00f
                );

            renderCurrentScreen();

            return;
        }

        if (
            hit(
                x,y,
                20,126,
                200,38
            )
        )
        {
            gyroEnabled =
                !gyroEnabled;

            clearPointerMotion();

            renderCurrentScreen();

            return;
        }

        if (
            hit(
                x,y,
                20,173,
                200,38
            )
        )
        {
            invertY =
                !invertY;

            clearPointerMotion();

            renderCurrentScreen();

            return;
        }

        return;
    }
}
void initTouch()
{
    pinMode(
        TOUCH_INT,
        INPUT_PULLUP
    );

    pinMode(
        TOUCH_RST,
        OUTPUT
    );

    digitalWrite(
        TOUCH_RST,
        HIGH
    );

    delay(20);

    digitalWrite(
        TOUCH_RST,
        LOW
    );

    delay(5);

    digitalWrite(
        TOUCH_RST,
        HIGH
    );

    delay(60);

    attachInterrupt(
        digitalPinToInterrupt(
            TOUCH_INT
        ),
        onTouchInterrupt,
        FALLING
    );

    Serial.println(
        "CST816S navigation ready."
    );
}

void updateTouch()
{
    if (!touchPending)
        return;

    noInterrupts();

    touchPending =
        false;

    interrupts();

    uint32_t now =
        millis();

    if (
        now -
        lastTouchActionMs <
        TOUCH_ACTION_COOLDOWN_MS
    ) {
        return;
    }

    uint16_t rawX = 0;
    uint16_t rawY = 0;

    uint8_t points = 0;

    if (!touchReadPoint(
        rawX,
        rawY,
        points
    )) {
        return;
    }

    if (!points)
        return;

    int16_t x = 0;
    int16_t y = 0;

    transformTouch(
        rawX,
        rawY,
        x,
        y
    );

    lastTouchActionMs =
        now;
handleTouchAction(
        x,
        y
    );
}
// ============================================================
// DISPLAY UI
// ============================================================

static constexpr uint16_t UI_BG =
    RGB565(0,0,0);

static constexpr uint16_t UI_WHITE =
    RGB565(255,255,255);

static constexpr uint16_t UI_MUTED =
    RGB565(120,120,120);

static constexpr uint16_t UI_BORDER =
    RGB565(60,60,60);

static constexpr uint16_t UI_GREEN =
    RGB565(105,215,145);

static constexpr uint16_t UI_BLUE =
    RGB565(120,170,240);

static constexpr uint16_t UI_WARN =
    RGB565(220,180,80);

void centerText(
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

void button(
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    const char *label,
    uint8_t size = 1
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

void drawMicScreen()
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

void drawHome()
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

const char *profileName()
{
    switch (
        currentProfile
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
// D2D1 V3 SPEAKER VOLUME UI
// ============================================================
void drawSpeakerProfileUI()
{
    // D2D2 SPEAKER HARDENING
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

    if (speakerVolumeMuted)
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
                speakerVolumePercent
            )
        );
    }

    centerText(
        label,
        120,
        70,
        2,
        speakerVolumeMuted
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
                    speakerVolumePercent
                ) *
                200U
            ) /
            100U
        );

    gfx->fillCircle(
        knobX,
        110,
        10,
        speakerVolumeMuted
            ? UI_MUTED
            : UI_BLUE
    );

    button(
        28,
        145,
        184,
        62,
        speakerVolumeMuted
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

void drawStaticUI()
{
    if (currentProfile == Profile::SPEAKER)
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

void drawStatus()
{
    if (currentProfile == Profile::SPEAKER)
    {
        return;
    }

    if (
        currentScreen !=
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
        stationary !=
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
            !gyroEnabled ||
            currentProfile ==
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
                stationary
                ? UI_GREEN
                : UI_BLUE
            );

            gfx->setCursor(
                145,
                201
            );

            gfx->print(
                stationary
                ? "REST"
                : "MOVE"
            );
        }

        lastDisplayStationary =
            stationary;
    }

    firstDisplayDraw =
        false;
}

// ============================================================
// MENU
// ============================================================

void drawMenu()
{
    gfx->fillScreen(UI_BG);

    // S2-A6R3 HOME SPEAKER NAV
    centerText("ESPCube",120,24,2,UI_WHITE);

    button(8,44,108,90,"MOUSE",2);
    button(124,44,108,90,"TEXT",2);
    button(8,142,108,90,"SPEAKER",2);
    button(124,142,108,90,"SETTINGS",2);
}

// ============================================================
// TEXT COMMON HEADER
// ============================================================

void drawKeyboardTop()
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
        currentScreen ==
            UIScreen::TEXT_GROUP
            ? "BACK"
            : (
                t9SecondPage
                    ? "ABC<"
                    : "ABC>"
            ),
        1
    );

    button(
        120,0,
        60,40,
        (
            currentScreen ==
                UIScreen::TEXT_NUMBERS &&
            selectedGroup !=
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
            currentScreen ==
                UIScreen::TEXT_SYMBOLS &&
            selectedGroup !=
                nullptr
        )
            ? "BACK"
            : "#+=",
        1
    );

    if (
        currentScreen ==
            UIScreen::TEXT_ALPHA ||
        currentScreen ==
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
        currentScreen ==
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

void drawKeyboardDock(
    bool allowShift
)
{
    const char *shiftLabel =
        "SHIFT";

    if (allowShift)
    {
        if (capsLock)
        {
            shiftLabel =
                shiftOnce
                ? "lower"
                : "CAPS";
        }
        else if (shiftOnce)
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

void drawErgoChar(
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    char c
)
{
    bool upper =
        capsLock ^
        shiftOnce;

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

void drawTextAlpha()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (!t9SecondPage)
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

void drawTextGroup()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (!selectedGroup)
    {
        drawKeyboardDock(
            true
        );

        return;
    }

    size_t n =
        strlen(
            selectedGroup
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
                selectedGroup[
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
                    selectedGroup[
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

void drawNumbers()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (
        selectedGroup ==
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
                selectedGroup[col],
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

void drawThreeSymbols(
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

void drawFourSymbols(
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

void drawSymbols()
{
    gfx->fillScreen(
        UI_BG
    );

    drawKeyboardTop();

    if (
        selectedGroup ==
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
            selectedGroup,
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
            selectedGroup,
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
            selectedGroup,
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
            selectedGroup,
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
            selectedGroup
        );

    if (n == 3)
    {
        drawThreeSymbols(
            selectedGroup
        );
    }
    else if (n == 4)
    {
        drawFourSymbols(
            selectedGroup
        );
    }

    drawKeyboardDock(
        false
    );
}

// ============================================================
// SETTINGS
// ============================================================

void drawSettings()
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
        sensitivityMultiplier *
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
        gyroEnabled
            ? "GYRO: ON"
            : "GYRO: OFF",
        1
    );

    button(
        20,173,
        200,38,
        invertY
            ? "VERTICAL: INVERTED"
            : "VERTICAL: NORMAL",
        1
    );
}

// ============================================================
// ROUTER
// ============================================================

void renderCurrentScreen()
{
    switch (
        currentScreen
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

void updateDisplay()
{
    if (currentProfile == Profile::SPEAKER)
    {
        return;
    }

    if (
        currentScreen !=
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

void enterRest(float gy, float gz)
{
    stationary = true;

    biasY +=
        REST_SNAP_RATE *
        (gy - biasY);

    biasZ +=
        REST_SNAP_RATE *
        (gz - biasZ);

    clearPointerMotion();
}

void exitRest()
{
    if (!stationary)
        return;

    stationary = false;
    quietSinceMs = 0;
    restScore = 0;

    clearPointerMotion();

}

// ============================================================
// BUTTONS
// ============================================================

void updateButtons()
{
    uint32_t now =
        millis();

    bool leftState =
        digitalRead(
            BTN_LEFT
        );

    bool middleState =
        digitalRead(
            BTN_MIDDLE
        );

    bool rightState =
        digitalRead(
            BTN_RIGHT
        );

    
    // ========================================================
    // D2D2 SPEAKER HARDENING Ã¢â‚¬â€ physical controls
    // A release = volume -
    // B press   = mute/unmute
    // C release = volume +
    // A+C hold  = HARD HOME
    // ========================================================
    if (currentProfile == Profile::SPEAKER)
    {
        static bool speakerComboSeen = false;

        const bool comboNow =
            leftState == LOW &&
            rightState == LOW;

        if (comboNow)
        {
            speakerComboSeen = true;

            if (!homeComboSince)
            {
                homeComboSince = now;
            }

            if (
                !homeComboTriggered &&
                now -
                homeComboSince >=
                HOME_COMBO_MS
            )
            {
                homeComboTriggered = true;

                hid.releaseMouseButtons();
                goHome();

                Serial.println(
                    "[UI] HARD HOME A+C"
                );

                return;
            }
        }
        else
        {
            homeComboSince = 0;
            homeComboTriggered = false;
        }

        if (
            leftState !=
            lastLeft &&
            now -
            lastLeftChange >=
            DEBOUNCE_MS
        )
        {
            lastLeftChange = now;
            lastLeft = leftState;

            if (
                leftState == HIGH &&
                !speakerComboSeen
            )
            {
                speakerVolumePercent =
                    speakerVolumePercent >= 10
                    ? speakerVolumePercent - 10
                    : 0;

                speakerVolume.setPercent(
                    speakerVolumePercent
                );

                drawSpeakerProfileUI();
            }
        }

        if (
            middleState !=
            lastMiddle &&
            now -
            lastMiddleChange >=
            DEBOUNCE_MS
        )
        {
            lastMiddleChange = now;
            lastMiddle = middleState;

            if (middleState == LOW)
            {
                speakerVolumeMuted =
                    !speakerVolumeMuted;

                speakerVolume.setMuted(
                    speakerVolumeMuted
                );

                drawSpeakerProfileUI();
            }
        }

        if (
            rightState !=
            lastRight &&
            now -
            lastRightChange >=
            DEBOUNCE_MS
        )
        {
            lastRightChange = now;
            lastRight = rightState;

            if (
                rightState == HIGH &&
                !speakerComboSeen
            )
            {
                speakerVolumePercent =
                    speakerVolumePercent <= 90
                    ? speakerVolumePercent + 10
                    : 100;

                speakerVolume.setPercent(
                    speakerVolumePercent
                );

                drawSpeakerProfileUI();
            }
        }

        if (
            leftState == HIGH &&
            rightState == HIGH
        )
        {
            speakerComboSeen = false;
        }

        return;
    }
    // ========================================================
    // HARD HOME: HOLD A + C
    // ========================================================

    if (
        leftState == LOW &&
        rightState == LOW
    )
    {
        if (!homeComboSince)
        {
            homeComboSince =
                now;
        }

        if (
            !homeComboTriggered &&
            now -
            homeComboSince >=
            HOME_COMBO_MS
        )
        {
            homeComboTriggered =
                true;

            hid.releaseMouseButtons();

            goHome();

            Serial.println(
                "[UI] HARD HOME A+C"
            );
        }
    }
    else
    {
        homeComboSince =
            0;

        homeComboTriggered =
            false;
    }

    // ========================================================
    // A
    // ========================================================

    if (
        leftState !=
        lastLeft &&
        now -
        lastLeftChange >=
        DEBOUNCE_MS
    )
    {
        lastLeftChange =
            now;

        lastLeft =
            leftState;

        bool pressed =
            leftState == LOW;

        if (!homeComboTriggered)
        {
            if (
                currentScreen ==
                UIScreen::TEXT_ALPHA ||
                currentScreen ==
                UIScreen::TEXT_GROUP ||
                currentScreen ==
                UIScreen::TEXT_NUMBERS ||
                currentScreen ==
                UIScreen::TEXT_SYMBOLS
            )
            {
                if (pressed)
                    sendBackspace();
            }
            else if (
                currentScreen ==
                UIScreen::MOUSE &&
                currentProfile ==
                Profile::MOUSE
            )
            {
                hid.setMouseButton(
                    0x01,
                    pressed
                );
            }
        }
    }

    // ========================================================
    // B
    //
    // v0.8 STEP 2
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
        currentScreen ==
            UIScreen::TEXT_ALPHA ||
        currentScreen ==
            UIScreen::TEXT_GROUP ||
        currentScreen ==
            UIScreen::TEXT_NUMBERS ||
        currentScreen ==
            UIScreen::TEXT_SYMBOLS;

    if (
        middleState !=
        lastMiddle &&
        now -
        lastMiddleChange >=
        DEBOUNCE_MS
    )
    {
        lastMiddleChange =
            now;

        lastMiddle =
            middleState;

        bool pressed =
            middleState == LOW;

        // ====================================================
        // RELEASE CONFIRMED SPEECH SESSION
        // ====================================================

        if (
            !pressed &&
            speechMicActive
        )
        {
            if (speechCaptureStarted)
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

            speechCaptureStarted =
                false;

            speechMicActive =
                false;

            speechBPending =
                false;

            renderCurrentScreen();

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
                speechBPressedAt =
                    now;

                speechBPending =
                    true;

                speechMicActive =
                    false;

                speechCaptureStarted =
                    speech.startRecording();

                Serial.println(
                    "[MIC] B DOWN"
                );

                if (speechCaptureStarted)
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
                speechBPending
            )
            {
                // Quick tap:
                // captured preroll never enters Speech Protocol.
                if (speechCaptureStarted)
                {
                    speech.stopRecording();
                }

                speechCaptureStarted =
                    false;

                speechBPending =
                    false;

                speechMicActive =
                    false;

                sendSpace();

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
            speechBPending =
                false;

            speechMicActive =
                false;

            speechCaptureStarted =
                false;

            if (
                currentScreen ==
                UIScreen::MOUSE &&
                currentProfile ==
                Profile::MOUSE
            )
            {
                if (pressed)
                {
                    // Do not send middle-button DOWN yet.
                    //
                    // We cannot know at B-down whether this is:
                    //
                    //   1. a normal middle click
                    //   2. the beginning of a gyro-scroll gesture
                    //
                    // Defer the click until release.
                    mouseScrollBPressedAt =
                        now;

                    mouseScrollGesture =
                        false;

                    mouseScrollAccumulator =
                        0.0f;

                    mouseScrollTilt =
                        0.0f;

                    // Prevent cursor residue from becoming an
                    // accidental jump when scroll mode begins.
                    clearPointerMotion();
                }
                else
                {
                    if (!mouseScrollGesture)
                    {
                        // Ordinary B tap -> middle click.
                        //
                        // Press and release are deliberately kept
                        // together with no debug logging between them.
                        hid.setMouseButton(
                            0x04,
                            true
                        );

                        delay(12);

                        hid.setMouseButton(
                            0x04,
                            false
                        );
                    }
                    else
                    {
                        // Scroll gesture consumed B.
                        // Make absolutely sure no mouse button remains.
                        hid.releaseMouseButtons();
                    }

                    mouseScrollGesture =
                        false;

                    mouseScrollAccumulator =
                        0.0f;

                    mouseScrollTilt =
                        0.0f;

                    mouseScrollBPressedAt =
                        0;

                    clearPointerMotion();
                }
            }
        }
    }

    // ========================================================
    // PREROLL / LIVE STREAM
    // ========================================================

    if (
        keyboardScreen &&
        middleState == LOW &&
        speechBPending
    )
    {
        // ----------------------------------------------------
        // Before confirmation:
        // collect only into PSRAM preroll.
        // ----------------------------------------------------

        if (
            speechCaptureStarted &&
            !speechMicActive
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
            !speechMicActive &&
            now -
            speechBPressedAt >=
            SPEECH_HOLD_MS
        )
        {
            if (speechCaptureStarted)
            {
                speechMicActive =
                    true;

                drawMicScreen();

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
                speechBPending =
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
            speechCaptureStarted &&
            speechMicActive
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
        lastRight &&
        now -
        lastRightChange >=
        DEBOUNCE_MS
    )
    {
        lastRightChange =
            now;

        lastRight =
            rightState;

        bool pressed =
            rightState == LOW;

        if (!homeComboTriggered)
        {
            if (
                currentScreen ==
                UIScreen::TEXT_ALPHA ||
                currentScreen ==
                UIScreen::TEXT_GROUP ||
                currentScreen ==
                UIScreen::TEXT_NUMBERS ||
                currentScreen ==
                UIScreen::TEXT_SYMBOLS
            )
            {
                if (pressed)
                    sendEnter();
            }
            else if (
                currentScreen ==
                UIScreen::MOUSE &&
                currentProfile ==
                Profile::MOUSE
            )
            {
                hid.setMouseButton(
                    0x02,
                    pressed
                );
            }
        }
    }
}

// ============================================================
// GYRO ENGINE
// ============================================================

void updateGyro()
{
    uint32_t nowUs = micros();

    if (
        (uint32_t)(nowUs - lastSampleUs)
        < SAMPLE_INTERVAL_US
    ) {
        return;
    }

    float dt =
        lastSampleUs == 0
        ? SAMPLE_INTERVAL_US / 1000000.0f
        : (nowUs - lastSampleUs) / 1000000.0f;

    lastSampleUs = nowUs;

    float gx, gy, gz;

    if (
        !imu.readGyroDPS(gx, gy, gz) ||
        !sampleValid(gx, gy, gz)
    ) {
        rejectedSamples++;
        return;
    }

    goodSamples++;

    if (!havePrevious)
    {
        prevRawY = gy;
        prevRawZ = gz;

        biasY = gy;
        biasZ = gz;

        havePrevious = true;

        quietSinceMs = millis();

        return;
    }

    float deltaY =
        fabsf(gy - prevRawY);

    float deltaZ =
        fabsf(gz - prevRawZ);

    prevRawY = gy;
    prevRawZ = gz;

    float maxDelta =
        max(deltaY, deltaZ);

    // Rolling confidence:
    //
    // very quiet      -> confidence rises quickly
    // small hand jitter -> confidence falls only slightly
    // real movement   -> confidence collapses
    if (
        deltaY < REST_DELTA_Y &&
        deltaZ < REST_DELTA_Z
    ) {
        restScore += 2;
    }
    else if (maxDelta < REST_SOFT_DELTA)
    {
        restScore -= 1;
    }
    else
    {
        restScore -= 6;
    }

    restScore =
        constrain(
            restScore,
            0,
            REST_SCORE_MAX
        );

    float correctedY =
        gy - biasY;

    float correctedZ =
        gz - biasZ;

    float planarSpeed =
        sqrtf(
            correctedY * correctedY +
            correctedZ * correctedZ
        );

    // ========================================================
    // MICRO-DRIFT ABSORBER
    //
    // This runs BEFORE full REST lock.
    //
    // Conditions:
    // - sensor readings are changing very little
    // - corrected Y/Z movement is only a few dps
    //
    // That combination strongly suggests gyro bias rather than
    // an intentional hand movement.
    // ========================================================

    float maxRawDelta =
        max(deltaY, deltaZ);

    if (
        !stationary &&
        maxRawDelta < MICRO_DRIFT_DELTA &&
        fabsf(correctedY) < MICRO_DRIFT_MAX_DPS &&
        fabsf(correctedZ) < MICRO_DRIFT_MAX_DPS
    ) {
        biasY +=
            MICRO_BIAS_RATE *
            (gy - biasY);

        biasZ +=
            MICRO_BIAS_RATE *
            (gz - biasZ);

        // Recalculate after bias adjustment.
        correctedY =
            gy - biasY;

        correctedZ =
            gz - biasZ;

        planarSpeed =
            sqrtf(
                correctedY * correctedY +
                correctedZ * correctedZ
            );
    }

    if (stationary)
    {
        if (
            planarSpeed >
            REST_EXIT_DPS
        ) {
            exitRest();
        }
        else
        {
            biasY +=
                REST_TRACK_RATE *
                (gy - biasY);

            biasZ +=
                REST_TRACK_RATE *
                (gz - biasZ);

            clearPointerMotion();

            return;
        }
    }

    if (!stationary)
    {
        // Enter REST when:
        //
        // 1. the gyro has looked quiet for enough recent samples
        // 2. apparent corrected movement is still small enough
        //    to plausibly be bias drift rather than intentional motion
        if (
            restScore >= REST_SCORE_ENTER &&
            planarSpeed < REST_CAPTURE_DPS
        ) {
            enterRest(
                gy,
                gz
            );

            restScore = REST_SCORE_MAX;

            return;
        }
    }

    correctedY =
        gy - biasY;

    correctedZ =
        gz - biasZ;

    addHistory(
        correctedY,
        correctedZ
    );

    if (historyCount < 3)
        return;

    float medianY =
        median3(
            histY[0],
            histY[1],
            histY[2]
        );

    float medianZ =
        median3(
            histZ[0],
            histZ[1],
            histZ[2]
        );

    // ========================================================
    // KNOWN-GOOD AXIS MAP
    // ========================================================

    float mouseX =
        medianZ *
        HORIZONTAL_SIGN;

    float mouseY =
        medianY *
        VERTICAL_SIGN;

    mouseX =
        softDeadzone(
            mouseX,
            DEADZONE_HORIZONTAL
        );

    mouseY =
        softDeadzone(
            mouseY,
            DEADZONE_VERTICAL
        );

    // ========================================================
    // B + GYRO VERTICAL SCROLL
    // ========================================================
    //
    // Only active on the normal mouse screen.
    //
    // While B is down we suppress cursor movement immediately.
    // Deliberate vertical motion converts the pending B gesture
    // into scroll mode. Once converted, B release does NOT
    // generate a middle click.
    // ========================================================

    bool mouseScrollHeld =
        lastMiddle == LOW &&
        currentScreen ==
            UIScreen::MOUSE &&
        currentProfile ==
            Profile::MOUSE;

    if (mouseScrollHeld)
    {
        uint32_t heldMs =
            millis() -
            mouseScrollBPressedAt;

        if (
            !mouseScrollGesture &&
            mouseScrollBPressedAt != 0 &&
            heldMs >=
                MOUSE_SCROLL_ARM_MS &&
            fabsf(mouseY) >=
                MOUSE_SCROLL_ACTIVATE_DPS
        )
        {
            mouseScrollGesture =
                true;

            mouseScrollAccumulator =
                0.0f;
        }
        if (mouseScrollGesture)
        {
            // ==================================================
            // HYBRID SCROLL ENGINE
            // ==================================================
            //
            // PART 1:
            // Preserve the original movement-based gyro scroll.
            //
            // This means everything below ~90 degrees behaves
            // exactly like the first version you liked:
            //
            //     move Cube -> scroll
            //     stop Cube -> stop scrolling
            //
            // PART 2:
            // Integrate the same motion into a temporary relative
            // angle estimate.
            //
            // Once past ~90 degrees, add a continuous scroll rate.
            // Moving still contributes normally on top of it.
            // ==================================================

            float scrollMotion =
                -mouseY;

            // --------------------------------------------------
            // ORIGINAL / NORMAL SCROLL RESPONSE
            // --------------------------------------------------

            mouseScrollAccumulator +=
                (
                    scrollMotion *
                    dt
                ) /
                MOUSE_SCROLL_NOTCH;

            // --------------------------------------------------
            // TRACK RELATIVE ROTATION
            // --------------------------------------------------

            mouseScrollTilt +=
                scrollMotion *
                dt;

            mouseScrollTilt =
                constrain(
                    mouseScrollTilt,
                    -160.0f,
                    160.0f
                );

            float absTilt =
                fabsf(
                    mouseScrollTilt
                );

            // --------------------------------------------------
            // 90+ DEGREE CONTINUOUS SCROLL
            // --------------------------------------------------

            if (
                absTilt >=
                MOUSE_SCROLL_CONTINUOUS_START
            )
            {
                float normalized =
                    (
                        absTilt -
                        MOUSE_SCROLL_CONTINUOUS_START
                    ) /
                    (
                        MOUSE_SCROLL_CONTINUOUS_FULL -
                        MOUSE_SCROLL_CONTINUOUS_START
                    );

                normalized =
                    constrain(
                        normalized,
                        0.0f,
                        1.0f
                    );

                // Smooth acceleration after crossing 90 degrees.
                //
                // Just over 90:
                //     gentle continuous scroll
                //
                // farther over:
                //     progressively faster
                float shaped =
                    normalized;

                float continuousRate =
                    MOUSE_SCROLL_CONTINUOUS_MIN_RATE +
                    shaped *
                    (
                        MOUSE_SCROLL_CONTINUOUS_MAX_RATE -
                        MOUSE_SCROLL_CONTINUOUS_MIN_RATE
                    );

                if (
                    mouseScrollTilt <
                    0.0f
                )
                {
                    continuousRate =
                        -continuousRate;
                }

                mouseScrollAccumulator +=
                    continuousRate *
                    dt;
            }

            // --------------------------------------------------
            // SEND WHEEL REPORTS
            // --------------------------------------------------

            int8_t wheel =
                0;

            while (
                mouseScrollAccumulator >=
                    1.0f &&
                wheel < 3
            )
            {
                wheel++;

                mouseScrollAccumulator -=
                    1.0f;
            }

            while (
                mouseScrollAccumulator <=
                    -1.0f &&
                wheel > -3
            )
            {
                wheel--;

                mouseScrollAccumulator +=
                    1.0f;
            }

            if (
                wheel != 0 &&
                hid.isConnected()
            )
            {
                hid.move(
                    0,
                    0,
                    wheel
                );
            }
        }

        // B is held in Mouse mode:
        // pointer motion is intentionally suppressed regardless
        // of whether scrolling has crossed its activation threshold.
        clearPointerMotion();

        return;
    }

    filteredX =
        adaptiveFilter(
            mouseX,
            filteredX
        );

    filteredY =
        adaptiveFilter(
            mouseY,
            filteredY
        );

    // Remove tiny filter residue without affecting deliberate
    // fine cursor movement.
    if (fabsf(filteredX) < 0.18f)
        filteredX = 0.0f;

    if (fabsf(filteredY) < 0.18f)
        filteredY = 0.0f;

    float speed =
        sqrtf(
            filteredX * filteredX +
            filteredY * filteredY
        );

    float accel =
        accelerationFor(speed);

    float dx =
        filteredX *
        dt *
        BASE_SENSITIVITY * sensitivityMultiplier * accel;

    float dy =
        filteredY *
        dt *
        BASE_SENSITIVITY * sensitivityMultiplier * accel;

    remainderX += dx;
    remainderY += dy;

    int moveX =
        (int)remainderX;

    int moveY =
        (int)remainderY;

    remainderX -= moveX;
    remainderY -= moveY;

    moveX = constrain(
        moveX,
        -50,
        50
    );

    moveY = constrain(
        moveY,
        -50,
        50
    );

    if (
        hid.isConnected() &&
        gyroEnabled &&
        currentScreen ==
            UIScreen::MOUSE &&
        currentProfile !=
            Profile::SPEAKER &&
        (
            moveX != 0 ||
            moveY != 0
        )
    )
    {
        hid.move(
            moveX,
            invertY
                ? -moveY
                : moveY
        );
    }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    speakerVolume.begin(100);
    speakerVolumePercent = 100;
    speakerVolumeMuted = false;

    Serial.begin(115200);

    // Debug output must NEVER be able to hold product logic.
    // 1 ms is intentional: timeout=0 has had HWCDC blocking bugs.
    Serial.setTxTimeoutMs(1);
    delay(1200);

    Serial.println();
    Serial.println("================================================");
    Serial.println("              ESPCube v0.4");
    Serial.println("================================================");
    Serial.println();

    // ========================================================
    // BUTTONS
    // ========================================================

    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_MIDDLE, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);

    lastLeft =
        digitalRead(BTN_LEFT);

    lastMiddle =
        digitalRead(BTN_MIDDLE);

    lastRight =
        digitalRead(BTN_RIGHT);

    // ========================================================
    // DISPLAY
    // ========================================================

    Serial.println("Initializing display...");

    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, LOW);

    bool displayOK =
        gfx->begin();


    if (displayOK)
    {
        gfx->setTextWrap(false);

        digitalWrite(
            LCD_BL,
            HIGH
        );

        drawStaticUI();
        drawStatus();

        Serial.println("Display ready.");
    }
    else
    {
        Serial.println(
            "WARNING: display init failed."
        );
    }

    // ========================================================
    // KNOWN-GOOD IMU INIT FROM v0.3.3
    // ========================================================

    Wire.begin(
        I2C_SDA,
        I2C_SCL
    );

    Wire.setClock(400000);

    delay(100);

    Serial.println(
        "Initializing QMI8658..."
    );

    bool ok =
        imu.begin(
            I2C_SDA,
            I2C_SCL,
            QMI8658_ADDRESS_LOW
        );

    if (!ok)
    {
        ok =
            imu.begin(
                I2C_SDA,
                I2C_SCL,
                QMI8658_ADDRESS_HIGH
            );
    }

    if (!ok)
    {
        Serial.println(
            "FATAL: QMI8658 NOT FOUND"
        );

        while (true) {
            delay(1000);
        }
    }

    imu.setGyroRange(
        QMI8658_GYRO_RANGE_512DPS
    );

    imu.setGyroODR(
        QMI8658_GYRO_ODR_250HZ
    );

    imu.setGyroUnit_dps(true);
    imu.enableGyro(true);

    Serial.println("QMI8658 ready.");

    // ========================================================
    // TOUCH
    // ========================================================

    initTouch();

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
// S2-A3 SPEAKER CONTROL
// ============================================================

// ============================================================
// S2-D1 TCP PCM PROOF
// ============================================================

static void closeSpeakerTcpClient(
    bool sendResult
)
{
    bool restoreOk = true;

    if (speakerPlayback.active())
    {
        restoreOk =
            speakerPlayback.end();
    }

    if (speakerTcpClient >= 0)
    {
        if (sendResult)
        {
            char result[96];

            snprintf(
                result,
                sizeof(result),
                "DONE bytes=%lu chunks=%lu restore=%u\n",
                (unsigned long)speakerTcpBytesPlayed,
                (unsigned long)speakerTcpChunksPlayed,
                restoreOk ? 1U : 0U
            );

            ::send(
                speakerTcpClient,
                result,
                strlen(result),
                0
            );

            // Give the tiny proof response a moment to leave
            // before the socket is torn down.
            delay(5);
        }

        ::shutdown(
            speakerTcpClient,
            SHUT_RDWR
        );

        ::close(
            speakerTcpClient
        );

        speakerTcpClient =
            -1;
    }

    speakerTcpRxFill =
        0;

    speakerTcpBytesPlayed =
        0;

    speakerTcpChunksPlayed =
        0;
}

static void stopSpeakerTcpServer()
{
    closeSpeakerTcpClient(
        false
    );

    if (speakerTcpServer >= 0)
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;
    }

    speakerTcpListening =
        false;
}

void stopSpeakerTcpPlayback()
{
    stopSpeakerTcpServer();
}

static bool startSpeakerTcpServer()
{
    if (speakerTcpListening)
        return true;

    speakerTcpServer =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );

    if (speakerTcpServer < 0)
    {
        speakerTcpServer = -1;
        return false;
    }

    const int reuse = 1;

    setsockopt(
        speakerTcpServer,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );

    const int flags =
        fcntl(
            speakerTcpServer,
            F_GETFL,
            0
        );

    if (
        flags < 0 ||
        fcntl(
            speakerTcpServer,
            F_SETFL,
            flags | O_NONBLOCK
        ) != 0
    )
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;

        return false;
    }

    struct sockaddr_in localAddress;

    memset(
        &localAddress,
        0,
        sizeof(localAddress)
    );

    localAddress.sin_family =
        AF_INET;

    localAddress.sin_addr.s_addr =
        htonl(INADDR_ANY);

    localAddress.sin_port =
        htons(TCP_PORT);

    if (
        ::bind(
            speakerTcpServer,
            reinterpret_cast<struct sockaddr *>(
                &localAddress
            ),
            sizeof(localAddress)
        ) != 0
    )
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;

        return false;
    }

    if (
        ::listen(
            speakerTcpServer,
            1
        ) != 0
    )
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;

        return false;
    }

    speakerTcpListening =
        true;

    return true;
}

static void acceptSpeakerTcpClient()
{
    if (
        !speakerTcpListening ||
        speakerTcpServer < 0 ||
        speakerTcpClient >= 0
    )
    {
        return;
    }

    struct sockaddr_in remoteAddress;

    socklen_t remoteLength =
        sizeof(remoteAddress);

    const int client =
        ::accept(
            speakerTcpServer,
            reinterpret_cast<struct sockaddr *>(
                &remoteAddress
            ),
            &remoteLength
        );

    if (client < 0)
        return;

    const int one = 1;

    setsockopt(
        client,
        IPPROTO_TCP,
        TCP_NODELAY,
        &one,
        sizeof(one)
    );

    const int flags =
        fcntl(
            client,
            F_GETFL,
            0
        );

    if (
        flags < 0 ||
        fcntl(
            client,
            F_SETFL,
            flags | O_NONBLOCK
        ) != 0
    )
    {
        ::close(client);
        return;
    }

    speakerTcpClient =
        client;

    speakerTcpRxFill =
        0;

    speakerTcpBytesPlayed =
        0;

    speakerTcpChunksPlayed =
        0;
}

static bool playSpeakerTcpChunk()
{
    int16_t mono[
        TCP_PCM_FRAMES
    ];

    for (
        size_t i = 0;
        i < TCP_PCM_FRAMES;
        ++i
    )
    {
        const size_t offset =
            i * 2;

        const uint16_t raw =
            static_cast<uint16_t>(
                speakerTcpRx[offset]
            ) |
            (
                static_cast<uint16_t>(
                    speakerTcpRx[offset + 1]
                )
                << 8
            );

        mono[i] =
            static_cast<int16_t>(
                raw
            );
    }

    if (!speakerPlayback.active())
    {
        if (
            !speakerPlayback.begin(
                speech
            )
        )
        {
            return false;
        }
    }

    const size_t written =
        speakerPlayback.writeMono(
            mono,
            TCP_PCM_FRAMES
        );

    if (
        written !=
        TCP_PCM_FRAMES
    )
    {
        return false;
    }

    speakerTcpBytesPlayed +=
        TCP_PCM_BYTES;

    ++speakerTcpChunksPlayed;

    speakerTcpRxFill =
        0;

    return true;
}

static void receiveSpeakerTcpAudio()
{
    if (speakerTcpClient < 0)
        return;

    // At most one 10 ms playback chunk per main-loop pass.
    // This deliberately keeps HOME/touch/buttons responsive.
    while (
        speakerTcpRxFill <
        TCP_PCM_BYTES
    )
    {
        const int received =
            ::recv(
                speakerTcpClient,
                speakerTcpRx +
                    speakerTcpRxFill,
                TCP_PCM_BYTES -
                    speakerTcpRxFill,
                0
            );

        if (received > 0)
        {
            speakerTcpRxFill +=
                static_cast<size_t>(
                    received
                );

            continue;
        }

        if (received == 0)
        {
            // PC completed the stream using SHUT_WR.
            // End playback, restore speech16, and return proof.
            closeSpeakerTcpClient(
                true
            );

            return;
        }

        if (
            errno == EWOULDBLOCK ||
            errno == EAGAIN
        )
        {
            return;
        }

        closeSpeakerTcpClient(
            false
        );

        return;
    }

    if (
        speakerTcpRxFill ==
        TCP_PCM_BYTES
    )
    {
        if (!playSpeakerTcpChunk())
        {
            closeSpeakerTcpClient(
                false
            );
        }
    }
}

void serviceSpeakerTcp()
{
    const bool shouldRun =
        currentProfile ==
            Profile::SPEAKER &&
        speakerControl.isReady() &&
        WiFi.status() ==
            WL_CONNECTED;

    if (!shouldRun)
    {
        if (
            speakerTcpListening ||
            speakerTcpClient >= 0 ||
            speakerPlayback.active()
        )
        {
            stopSpeakerTcpServer();
        }

        return;
    }

    if (!speakerTcpListening)
    {
        if (!startSpeakerTcpServer())
            return;
    }

    acceptSpeakerTcpClient();

    receiveSpeakerTcpAudio();
}

// ============================================================
// EXISTING SPEAKER CONTROL
// ============================================================
void serviceSpeakerControl()
{
    const SpeakerCommand command =
        speakerControl.consumeCommand();

    if (command == SpeakerCommand::Enter)
    {
        if (speakerControl.enter())
        {
            currentProfile =
                Profile::SPEAKER;

            currentScreen =
                UIScreen::MOUSE;

            clearPointerMotion();
            renderCurrentScreen();
        }
        else
        {
            speakerControl.exitSafe();
            goHome();
        }
    }
    else if (command == SpeakerCommand::Exit)
    {
        speakerControl.exitSafe();

        if (currentProfile == Profile::SPEAKER)
        {
            goHome();
        }
    }
    else if (command == SpeakerCommand::Status)
    {
        speakerControl.publishStatus();
    }

    if (
        currentProfile != Profile::SPEAKER &&
        speakerControl.state() != SpeakerState::Off
    )
    {
        speakerControl.exitSafe();
    }
}

// ============================================================
// LOOP
// ============================================================


// ============================================================
// S2-D2A BUFFERED TCP SERVICE
// ============================================================

static void resetSpeakerD2ASession()
{
    speakerPcmBuffer.clear();

    d2aTcpBytesRx = 0;
    d2aPcmBytesPlayed = 0;
    d2aChunksPlayed = 0;
    d2aBufferUnderruns = 0;
    d2aBufferOverflows = 0;

    d2aInputEnded = false;
    d2aPlaybackStarted = false;
    d2aUnderrunLatched = false;

    d2aLastWriteMs = millis();
}

static bool ensureSpeakerD2ABuffer()
{
    if (speakerPcmBuffer.ready()) {
        return true;
    }

    return speakerPcmBuffer.begin(
        D2A_BUFFER_CAPACITY
    );
}

static void closeSpeakerD2AClientOnly()
{
    if (speakerTcpClient >= 0) {
        ::shutdown(
            speakerTcpClient,
            SHUT_RDWR
        );

        ::close(
            speakerTcpClient
        );

        speakerTcpClient = -1;
    }

    speakerTcpRxFill = 0;
}

static void finishSpeakerD2AStream()
{
    const bool restoreOk =
        speakerPlayback.end();

    char response[220];

    snprintf(
        response,
        sizeof(response),
        "DONE rx=%lu played=%lu chunks=%lu underruns=%lu overflows=%lu highwater=%u restore=%u\n",
        static_cast<unsigned long>(
            d2aTcpBytesRx
        ),
        static_cast<unsigned long>(
            d2aPcmBytesPlayed
        ),
        static_cast<unsigned long>(
            d2aChunksPlayed
        ),
        static_cast<unsigned long>(
            d2aBufferUnderruns
        ),
        static_cast<unsigned long>(
            d2aBufferOverflows
        ),
        static_cast<unsigned>(
            speakerPcmBuffer.highWater()
        ),
        restoreOk ? 1U : 0U
    );

    if (speakerTcpClient >= 0) {
        ::send(
            speakerTcpClient,
            response,
            strlen(response),
            0
        );
    }

    closeSpeakerD2AClientOnly();

    speakerPcmBuffer.clear();

    d2aInputEnded = false;
    d2aPlaybackStarted = false;
    d2aUnderrunLatched = false;
}

static bool acceptSpeakerD2AClient()
{
    if (
        speakerTcpClient >= 0 ||
        speakerTcpServer < 0
    ) {
        return speakerTcpClient >= 0;
    }

    struct sockaddr_in remoteAddress;

    socklen_t remoteLength =
        sizeof(remoteAddress);

    const int client =
        ::accept(
            speakerTcpServer,
            reinterpret_cast<
                struct sockaddr *
            >(
                &remoteAddress
            ),
            &remoteLength
        );

    if (client < 0) {
        if (
            errno == EWOULDBLOCK ||
            errno == EAGAIN
        ) {
            return false;
        }

        return false;
    }

    const int one = 1;

    setsockopt(
        client,
        IPPROTO_TCP,
        TCP_NODELAY,
        &one,
        sizeof(one)
    );

    const int flags =
        fcntl(
            client,
            F_GETFL,
            0
        );

    if (flags >= 0) {
        fcntl(
            client,
            F_SETFL,
            flags | O_NONBLOCK
        );
    }

    speakerTcpClient =
        client;

    resetSpeakerD2ASession();

    return true;
}

static void drainSpeakerD2ATcpIntoRing()
{
    if (
        speakerTcpClient < 0 ||
        d2aInputEnded
    ) {
        return;
    }

    uint8_t rx[2048];

    while (speakerTcpClient >= 0) {
        const size_t freeBytes =
            speakerPcmBuffer.freeBytes();

        if (freeBytes == 0) {
            // Do not read more from TCP while the PSRAM
            // queue is full. TCP backpressure is preferable
            // to dropping PCM.
            return;
        }

        const size_t request =
            min(
                sizeof(rx),
                freeBytes
            );

        const int received =
            ::recv(
                speakerTcpClient,
                rx,
                request,
                0
            );

        if (received > 0) {
            const size_t accepted =
                speakerPcmBuffer.push(
                    rx,
                    static_cast<size_t>(
                        received
                    )
                );

            d2aTcpBytesRx +=
                static_cast<uint32_t>(
                    accepted
                );

            if (
                accepted !=
                static_cast<size_t>(
                    received
                )
            ) {
                ++d2aBufferOverflows;
                return;
            }

            continue;
        }

        if (received == 0) {
            d2aInputEnded = true;
            return;
        }

        if (
            errno == EWOULDBLOCK ||
            errno == EAGAIN
        ) {
            return;
        }

        // Any other socket error ends this client safely.
        d2aInputEnded = true;
        return;
    }
}

static bool maybeStartSpeakerD2APlayback()
{
    if (d2aPlaybackStarted) {
        return true;
    }

    if (
        speakerPcmBuffer.size() <
            D2A_PREBUFFER_BYTES &&
        !d2aInputEnded
    ) {
        return false;
    }

    if (
        speakerPcmBuffer.size() <
            TCP_PCM_BYTES
    ) {
        return false;
    }

    if (!speakerPlayback.begin(speech)) {
        return false;
    }

    d2aPlaybackStarted = true;
    d2aLastWriteMs = millis();
    d2aUnderrunLatched = false;

    return true;
}

static void playOneSpeakerD2AChunk()
{
    if (!d2aPlaybackStarted) {
        return;
    }

    if (
        speakerPcmBuffer.size() <
        TCP_PCM_BYTES
    ) {
        if (
            !d2aInputEnded &&
            !d2aUnderrunLatched &&
            millis() - d2aLastWriteMs >=
                D2A_UNDERRUN_GRACE_MS
        ) {
            ++d2aBufferUnderruns;
            d2aUnderrunLatched = true;
        }

        return;
    }

    uint8_t raw[TCP_PCM_BYTES];

    const size_t popped =
        speakerPcmBuffer.pop(
            raw,
            sizeof(raw)
        );

    if (popped != sizeof(raw)) {
        ++d2aBufferUnderruns;
        d2aUnderrunLatched = true;
        return;
    }

    int16_t mono[TCP_PCM_FRAMES];

    for (
        size_t i = 0;
        i < TCP_PCM_FRAMES;
        ++i
    ) {
        mono[i] =
            static_cast<int16_t>(
                static_cast<uint16_t>(
                    raw[i * 2]
                ) |
                (
                    static_cast<uint16_t>(
                        raw[i * 2 + 1]
                    )
                    << 8
                )
            );
    }

    // D2D1 V3: safe attenuation only.
    speakerVolume.apply(
        mono,
        TCP_PCM_FRAMES
    );

    const size_t consumed =
        speakerPlayback.writeMono(
            mono,
            TCP_PCM_FRAMES
        );

    if (
        consumed !=
        TCP_PCM_FRAMES
    ) {
        ++d2aBufferUnderruns;
        d2aInputEnded = true;
        return;
    }

    d2aPcmBytesPlayed +=
        TCP_PCM_BYTES;

    ++d2aChunksPlayed;

    d2aLastWriteMs =
        millis();

    d2aUnderrunLatched =
        false;
}

void serviceSpeakerTcpD2A()
{
    const bool shouldRun =
        currentProfile ==
            Profile::SPEAKER &&
        speakerControl.isReady() &&
        WiFi.status() ==
            WL_CONNECTED;

    if (!shouldRun) {
        if (
            speakerTcpClient >= 0 ||
            speakerTcpServer >= 0 ||
            speakerPlayback.active()
        ) {
            stopSpeakerTcpPlayback();
        }

        if (speakerPcmBuffer.ready()) {
            speakerPcmBuffer.clear();
        }

        d2aInputEnded = false;
        d2aPlaybackStarted = false;
        d2aUnderrunLatched = false;

        return;
    }

    if (!ensureSpeakerD2ABuffer()) {
        stopSpeakerTcpPlayback();
        return;
    }

    if (!speakerTcpListening) {
        startSpeakerTcpServer();
        return;
    }

    if (speakerTcpClient < 0) {
        acceptSpeakerD2AClient();
        return;
    }

    // Drain every TCP byte immediately available into PSRAM.
    drainSpeakerD2ATcpIntoRing();

    // Do not touch I2S until at least 120 ms has been buffered,
    // except at EOF for intentionally short streams.
    maybeStartSpeakerD2APlayback();

    // writeMono() / I2S provides the natural audio pacing.
    // Exactly one 10 ms PCM chunk is consumed per service pass.
    playOneSpeakerD2AChunk();

    if (
        d2aInputEnded &&
        speakerPcmBuffer.size() == 0
    ) {
        finishSpeakerD2AStream();
    }
}
void loop()
{
    speechLink.maintain();
    speakerControl.maintain();
    serviceSpeakerControl();
    serviceSpeakerTcpD2A();

    updateButtons();
    updateGyro();
    updateTouch();
    updateDisplay();
    // Periodic debug STATUS logging disabled:
    // USB CDC writes can block the main loop when no host is reading COM22.


    delay(1);
}