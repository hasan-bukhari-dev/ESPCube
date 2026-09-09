# Semantic Ownership Refactor Report

## Result

This experimental branch continues from the authoritative 41/41, locally built partitioned bridge. Firmware compilation of this semantic step is **REQUIRES AUTHORITATIVE LOCAL BUILD**; physical validation is **NOT PERFORMED**.

## Genuine ownership introduced

| Subsystem | State/functions moved | Integration change | Evidence |
|---|---|---|---|
| Buttons | GPIO initialization, raw sampling, stable states and debounce timestamps → `Buttons` | Setup calls `buttons.begin()`; ordered legacy semantic dispatch reads one `ButtonSnapshot` and uses owned debounce state | pins 0/5/4 and 18 ms checked |
| Touch | ISR pending flag, cooldown timestamp, CST816S register/point reads, coordinate transform, reset/init and polling → `Touch` | Setup calls `touch.begin()`; synchronous `updateTouch` forwards a `TouchPoint` to the unchanged UI action dispatcher | repeated-start, request semantics, 20/5/60 ms reset timing, transform and clamp checked |
| HOME | hold timestamp/latch → `HomeGesture` | Existing speaker/general priority branches use the owned state; threshold remains 800 ms | timing/latch source audit |
| Profile manager | current screen and active profile → `ProfileManager` | All previous reads/writes now use `profiles.screen` and `profiles.profile` | enum values/order retained |

`InputEvent` remains a stack-only value type. Dispatch remains direct and synchronous; no queue, heap allocation, observer bus, task or latency-changing buffer was introduced.

## Semantic changes

- Button GPIO reads are performed consecutively inside `Buttons::sample()` at the same location in the loop instead of as three expanded statements.
- Touch polling moved to a separately compiled class. Register order, repeated-start, point parsing, coordinate transform/clamping, cooldown decision and action timing are preserved.
- Primitive HOME and profile globals became object-owned fields. Existing branch order and product actions remain in place.
- Function-call boundaries add ordinary C++ method calls. Timing-sensitive device behavior must therefore be checked on the authoritative build/device.

## Remaining bridge fragments

| Fragment | Why it remains |
|---|---|
| `app/RuntimeState.inc` | HID, speech and several cross-profile objects still share initialization ordering |
| `input/MotionCore.inc`, `MotionRuntime.inc` | Calibrated motion algorithm is tightly coupled to HID scroll, settings and profile state; broad member conversion was not defensible without local compiler/device iteration |
| `input/ButtonsRuntime.inc` | Profile-specific semantic actions remain here after hardware/debounce ownership moved; converting 600+ lines into a new event router would materially change call structure |
| `app/ProfileManager.inc`, `profiles/TextProfile.inc` | Existing touch/profile action branches remain ordered; state itself is now manager-owned |
| `ui/ScreenRenderer.inc` | Exact draw order and shared state reads remain proven bridge code |
| `profiles/SpeakerStream.inc` | TCP/ring-buffer/I2S recovery state machine remains intact because pacing and teardown are hardware-sensitive |
| `app/HomeGesture.inc` | `goHome()` product teardown body remains intact; only gesture state moved |
| `app/SetupRuntime.inc`, `LoopRuntime.inc` | Preserve authoritative initialization and service order |

This is not represented as total elimination of `.inc` files. The specific residuals above are intentional under the instruction to retain any extraction that cannot yet be defended.

## Verification

`firmware/scripts/semantic_parity_check.py <oracle-root>` reports **16/16 PASS** for hardware constants, touch bus/transform/reset semantics, HOME timing, profile ownership, top-level order, motion/Speaker constants, and byte-identical companion/reusable libraries. The earlier bridge proof remains documented but is not claimed to prove the new semantic code.
