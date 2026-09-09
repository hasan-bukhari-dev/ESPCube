#include "Motion.h"

#include <Wire.h>
#include <ESPCubeHID.h>

#include "Buttons.h"
#include "../app/ProfileManager.h"
#include "../hardware/BoardConfig.h"

namespace
{
static constexpr uint32_t SAMPLE_INTERVAL_US = 8000;
static constexpr float MAX_VALID_DPS = 470.0f;
static constexpr float REST_DELTA_Y = 1.35f;
static constexpr float REST_DELTA_Z = 1.35f;
static constexpr float REST_SOFT_DELTA = 2.60f;
static constexpr float REST_CAPTURE_DPS = 9.0f;
static constexpr float REST_EXIT_DPS = 5.0f;
static constexpr int REST_SCORE_MAX = 32;
static constexpr int REST_SCORE_ENTER = 22;
static constexpr float REST_SNAP_RATE = 0.55f;
static constexpr float REST_TRACK_RATE = 0.035f;
static constexpr float MICRO_DRIFT_MAX_DPS = 4.0f;
static constexpr float MICRO_DRIFT_DELTA = 1.50f;
static constexpr float MICRO_BIAS_RATE = 0.008f;
static constexpr float HORIZONTAL_SIGN = +1.0f;
static constexpr float VERTICAL_SIGN = -1.0f;
static constexpr float DEADZONE_HORIZONTAL = 2.2f;
static constexpr float DEADZONE_VERTICAL = 1.8f;
static constexpr float BASE_SENSITIVITY = 15.0f;
static constexpr float ALPHA_MIN = 0.18f;
static constexpr float ALPHA_MAX = 0.72f;
static constexpr float ALPHA_SPEED_SCALE = 0.010f;
static constexpr float MAX_ACCEL = 1.55f;
static constexpr float ACCEL_FULL_AT_DPS = 160.0f;
static constexpr uint32_t MOUSE_SCROLL_ARM_MS = 90;
static constexpr float MOUSE_SCROLL_ACTIVATE_DPS = 5.0f;
static constexpr float MOUSE_SCROLL_NOTCH = 1.60f;
static constexpr float MOUSE_SCROLL_CONTINUOUS_START = 20.0f;
static constexpr float MOUSE_SCROLL_CONTINUOUS_FULL = 80.0f;
static constexpr float MOUSE_SCROLL_CONTINUOUS_MIN_RATE = 30.0f;
static constexpr float MOUSE_SCROLL_CONTINUOUS_MAX_RATE = 65.0f;
}

bool Motion::begin()
{
    Wire.begin(Board::I2cSda, Board::I2cScl);
    Wire.setClock(400000);
    delay(100);

    Serial.println("Initializing QMI8658...");

    bool ok = imu.begin(Board::I2cSda, Board::I2cScl, QMI8658_ADDRESS_LOW);
    if (!ok)
        ok = imu.begin(Board::I2cSda, Board::I2cScl, QMI8658_ADDRESS_HIGH);

    if (!ok)
        return false;

    imu.setGyroRange(QMI8658_GYRO_RANGE_512DPS);
    imu.setGyroODR(QMI8658_GYRO_ODR_250HZ);
    imu.setGyroUnit_dps(true);
    imu.enableGyro(true);

    Serial.println("QMI8658 ready.");
    return true;
}

float Motion::median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

void Motion::addHistory(float y, float z)
{
    histY[historyIndex] = y;
    histZ[historyIndex] = z;
    historyIndex = (historyIndex + 1) % 3;
    if (historyCount < 3)
        historyCount++;
}

float Motion::softDeadzone(float value, float dz)
{
    float a = fabsf(value);
    if (a <= dz)
        return 0.0f;
    return copysignf(a - dz, value);
}

float Motion::adaptiveFilter(float raw, float previous)
{
    float speed = fabsf(raw);
    float alpha = ALPHA_MIN + speed * ALPHA_SPEED_SCALE;
    alpha = constrain(alpha, ALPHA_MIN, ALPHA_MAX);
    return previous + alpha * (raw - previous);
}

float Motion::accelerationFor(float speed)
{
    float t = constrain(speed / ACCEL_FULL_AT_DPS, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    return 1.0f + (MAX_ACCEL - 1.0f) * t;
}

bool Motion::sampleValid(float gx, float gy, float gz)
{
    if (!isfinite(gx) || !isfinite(gy) || !isfinite(gz))
        return false;
    if (fabsf(gx) > MAX_VALID_DPS || fabsf(gy) > MAX_VALID_DPS || fabsf(gz) > MAX_VALID_DPS)
        return false;
    if (fabsf(gx) < 0.0001f && fabsf(gy) < 0.0001f && fabsf(gz) < 0.0001f)
        return false;
    if (fabsf(gx - gy) < 0.001f && fabsf(gy - gz) < 0.001f && fabsf(gx) > 30.0f)
        return false;
    return true;
}

void Motion::clearPointerMotion()
{
    filteredX = 0.0f;
    filteredY = 0.0f;
    remainderX = 0.0f;
    remainderY = 0.0f;
    historyCount = 0;
    historyIndex = 0;
}

void Motion::enterRest(float gy, float gz)
{
    stationary = true;
    biasY += REST_SNAP_RATE * (gy - biasY);
    biasZ += REST_SNAP_RATE * (gz - biasZ);
    clearPointerMotion();
}

void Motion::exitRest()
{
    if (!stationary)
        return;
    stationary = false;
    quietSinceMs = 0;
    restScore = 0;
    clearPointerMotion();
}

void Motion::update(
    const Buttons &buttons,
    const ProfileManager &profiles,
    ESPCubeHID &hid
)
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
    // GYRO-TO-POINTER AXIS MAP
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
        buttons.lastMiddle == LOW &&
        profiles.screen ==
            UIScreen::MOUSE &&
        profiles.profile ==
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
        profiles.screen ==
            UIScreen::MOUSE &&
        profiles.profile !=
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
