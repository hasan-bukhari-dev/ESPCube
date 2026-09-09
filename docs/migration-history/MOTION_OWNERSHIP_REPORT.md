# Motion Ownership Checkpoint

## Status

**Firmware build: REQUIRES AUTHORITATIVE LOCAL BUILD**

No firmware-build or physical-device PASS is claimed in this workspace.

## Scope

This checkpoint continues from `ESPCube-v1-UI-CONSTANT-ORDER-REPAIRED.zip` and
changes Motion ownership only. UI drawing logic, Speaker transport logic, and
profile lifecycle architecture were not refactored.

## State moved into `Motion`

- The `QMI8658` device instance.
- Sample counters and the 8 ms cadence timestamp.
- Previous raw Y/Z samples and initialization latch.
- Learned Y/Z bias.
- Three-sample Y/Z history, index, and count.
- Rest score, stationary state, and quiet timestamp.
- Filtered X/Y values and fractional X/Y remainders.
- Gyro enable, sensitivity multiplier, and Y inversion settings.
- Mouse-scroll press timestamp, gesture latch, accumulator, and tilt estimate.

`ScreenRenderer.inc` remains byte-identical to the authoritative-built input.
Four zero-storage C++ references expose Motion-owned status/settings to that
unchanged renderer. They are aliases, not duplicate variables.

## Functions moved into `Motion.cpp`

- `begin()` owns Wire/QMI8658 initialization and configuration.
- `update()` owns the complete sampled motion/scroll/pointer algorithm.
- `median3`, `addHistory`, `softDeadzone`, `adaptiveFilter`,
  `accelerationFor`, and `sampleValid` are private helpers.
- `enterRest` and `exitRest` are private rest-state transitions.
- `clearPointerMotion` remains public because HOME, Speaker recovery, button
  gestures, and profile actions must synchronously clear motion residue.

## Live integration

- `RuntimeState.inc` contains the sole `Motion motion;` instance.
- Setup calls `motion.begin()` at the exact former IMU initialization position.
- The fatal-not-found message and infinite recovery wait remain in setup.
- The main loop calls `motion.update(buttons, profiles, hid)` between
  `updateButtons()` and `updateTouch()`, exactly where `updateGyro()` ran.
- HOME, button-scroll, profile settings, and Speaker recovery call or modify the
  Motion-owned state directly.
- `MotionCore.inc` and `MotionRuntime.inc` were removed. Non-motion globals that
  previously shared `MotionCore.inc` remain unchanged in
  `app/LegacyRuntimeState.inc`; moving those belongs to later scoped passes.

## Parity evidence

Run from `firmware/`:

```sh
python3 scripts/check_motion_ownership.py /path/to/unpacked-authoritative-checkpoint
python3 scripts/check_live_ownership.py
python3 scripts/check_semantic_declarations.py
python3 scripts/check_ui_constant_order.py
```

The focused motion check proves:

- Complete update algorithm body is token-identical to `updateGyro()`.
- Nine helper/transition bodies are token-identical.
- All 30 motion constants retain exact values.
- QMI8658 addresses, range, ODR, unit, enable calls, and ordering are retained.
- Live setup/update calls exist and legacy motion ownership is absent.
- The renderer hash remains the authoritative checkpoint hash.

## Authoritative local build

Using the already-proven Espressif 32 55.3.311 / Arduino 3.3.11 environment:

```sh
cd firmware
pio run
```

Local hardware checkpoints still required: stationary entry/exit, fine cursor
motion, fast acceleration, drift recovery, B-tap middle click, B+gyro movement
scroll, held-angle continuous scroll, settings sensitivity/enable/invert, HOME
reset, and Speaker-to-HOME recovery.
