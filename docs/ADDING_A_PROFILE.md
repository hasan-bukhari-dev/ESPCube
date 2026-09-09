# Adding an ESPCube Profile

> Extension contract for adding new capabilities without breaking the universal baseline.

[← Back to README](../README.md) · [Design Principles](DESIGN_PRINCIPLES.md) · [Architecture](ARCHITECTURE.md)

---

# What a profile is

A profile is a user-facing capability layered over shared ESPCube hardware and services.

Conceptually:

```text
Launcher
   │
   ▼
Profile
   ├── touch
   ├── physical buttons
   ├── IMU
   ├── HID
   ├── optional BLE service
   ├── optional audio path
   └── optional Companion capability
```

A profile is **not** permission to take ownership of the entire product runtime.

---

# Non-negotiable invariants

Every new profile must preserve:

1. **HOME recovery**
2. **standard baseline availability**
3. **safe teardown**
4. **no unnecessary Wi-Fi**
5. **no personal credentials in source**
6. **no permanent held HID state**
7. **no dependency that breaks unrelated profiles**

---

# HOME invariant

A + C together remains the physical HOME escape.

A profile must not:

- consume the gesture permanently
- remap it into profile-specific behavior
- leave resources in a state that prevents returning HOME

---

# Where v1 profile code lives

The v1 coordinator is:

```text
firmware/src/main.cpp
```

Reusable functionality should live in focused libraries under:

```text
firmware/lib/ESPCube...
```

This keeps the shipping runtime stable while still allowing services to be factored cleanly.

---

# Input ownership

A profile should explicitly decide how it uses:

## Touch

- tap
- hold
- swipe/gesture
- hitboxes
- profile exit

Avoid invisible touch regions that conflict with HOME/navigation behavior.

## Buttons

Document:

- tap behavior
- hold behavior
- combinations
- whether a button emits HID

## IMU

If using motion:

- define calibration behavior
- define sensitivity
- clamp/control output
- reset motion state on profile exit

---

# HID rules

Use standard Bluetooth HID whenever it can represent the interaction.

Advantages:

- no custom host driver
- basic behavior works without Companion
- broader compatibility
- easier recovery

On profile exit:

- release held keys
- release held mouse buttons
- clear temporary HID state

---

# BLE rules

Add a custom BLE service only when the profile needs information or control that HID cannot represent cleanly.

A new GATT service should document:

- service UUID
- characteristics
- read/write/notify behavior
- connection lifecycle
- message format
- failure behavior

Do not overload unrelated existing characteristics simply because they already exist.

---

# Wi-Fi rules

Wi-Fi should remain off unless the profile genuinely needs the bandwidth or transport model.

Speaker is the v1 example:

```text
BLE      control / presence / provisioning
TCP      continuous PCM data
Wi-Fi    temporary high-bandwidth path
```

A low-volume control profile probably does not need Wi-Fi.

---

# Audio rules

If a profile uses audio:

- define input/output direction
- define sample format/rate
- define ownership of I²S/audio hardware
- define buffering
- define startup/teardown
- define behavior on underrun/transport loss

Do not leave the amplifier or audio pipeline active after the profile no longer owns it.

---

# Companion dependency rule

Ask:

> Can this profile work through standard HID or local device behavior without the Companion?

If yes, keep the basic path direct.

Use the Companion for capabilities that justify it, such as:

- local inference
- host system-audio capture
- desktop-native APIs
- richer BLE orchestration
- high-level configuration

---

# Enter / active / exit lifecycle

A good profile has an explicit lifecycle.

```text
ENTER
  │
  ├── initialize temporary state
  ├── enable required service
  └── render UI
  │
ACTIVE
  │
  ├── process inputs
  ├── update output
  └── recover local errors
  │
EXIT
  │
  ├── release HID state
  ├── stop temporary network/audio
  ├── clear temporary buffers
  └── return HOME cleanly
```

---

# Suggested implementation process

1. Define the user behavior first.
2. Define which hardware inputs it needs.
3. Decide whether HID is enough.
4. Add a BLE service only if required.
5. Add Wi-Fi only if required.
6. Put reusable hardware/service logic in a focused library.
7. Integrate with the current coordinator.
8. preserve A + C HOME.
9. test profile entry/exit repeatedly.
10. regression-test every existing profile.

---

# Regression checklist

Before accepting a new profile:

## Device baseline

- [ ] launcher renders correctly
- [ ] touch still works
- [ ] buttons still work
- [ ] A + C HOME always works
- [ ] Settings still works

## Mouse

- [ ] gyro pointer still works
- [ ] click controls still work
- [ ] leaving Mouse releases state

## Text

- [ ] speech still starts
- [ ] speech transport parity passes
- [ ] text inserts into focused app
- [ ] leaving Text cleans up

## Speaker

- [ ] Speaker reaches Ready
- [ ] Wi-Fi provisioning still works
- [ ] TCP audio still streams
- [ ] PCM playback remains stable
- [ ] reconnect path still works
- [ ] leaving Speaker releases temporary resources

## Companion

- [ ] device reaches Ready
- [ ] Grace reconnect still works
- [ ] show-on-connect still works
- [ ] background startup still works

---

# Documentation requirement

A new user-facing profile should update:

- root `README.md`
- `docs/QUICK_START.md` if setup changes
- `docs/ARCHITECTURE.md` if architecture changes
- `docs/PROTOCOL.md` if transport changes
- `docs/TROUBLESHOOTING.md`
- release validation matrix

---

## Related docs

- [Design Principles](DESIGN_PRINCIPLES.md)
- [Architecture](ARCHITECTURE.md)
- [Release Validation](RELEASE_VALIDATION.md)
