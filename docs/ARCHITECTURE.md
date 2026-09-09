# ESPCube Architecture

## Final firmware source architecture

The v1 firmware now uses real compiled C++ ownership modules; the temporary `.inc` migration bridge has been removed completely.

```text
main.cpp
   │
   ▼
AppRuntime
   ├── Buttons
   ├── Touch
   ├── Motion
   ├── HomeGesture
   ├── ProfileManager
   │    ├── MouseProfile
   │    ├── TextProfile
   │    ├── SettingsProfile
   │    └── SpeakerProfile
   ├── ScreenRenderer
   └── SpeakerStream
        ├── TCP session
        ├── PSRAM ring buffer
        └── playback pacing
```

Key design rules remain unchanged:

- input dispatch is synchronous and allocation-free;
- HOME is profile-independent;
- Motion owns the calibrated QMI8658 algorithm and state;
- Speaker BLE remains the control plane;
- Speaker TCP remains the high-bandwidth PCM data plane;
- the Speaker service consumes exactly one 10 ms PCM chunk per runtime service pass;
- Companion and reusable firmware libraries are unchanged by the architecture migration.

> Engineering architecture for ESPCube v1.0.0.

[← Back to README](../README.md) · [Protocol](PROTOCOL.md) · [Hardware](HARDWARE.md) · [Companion](COMPANION.md)

---

# System overview

ESPCube is deliberately split into two cooperating runtimes:

1. **Device firmware** on ESP32-S3
2. **Native Windows Companion** in Rust

The boundary is designed so basic host control remains standard while higher-level features use explicit services.

<img width="1536" height="1024" alt="fbcca3d9-d8c4-44e5-941d-0ec3e383ebf1" src="https://github.com/user-attachments/assets/2eedf63d-ad02-4faf-8d05-202ce0e30341" />

---

# Architectural boundary

## Direct path

```text
ESPCube ── Bluetooth HID ──► Windows
```

Used whenever standard HID is sufficient.

This path does not need the Companion.

## Extended path

```text
ESPCube ◄──── BLE ────► Companion
```

Used for:

- presence
- speech transport
- Speaker control
- status
- Wi-Fi provisioning

## High-bandwidth Speaker path

```text
Companion ── TCP / local Wi-Fi ──► ESPCube
```

Used only for PCM audio.

The control plane stays on BLE while the high-bandwidth data plane moves to TCP.

---

# Firmware architecture

The firmware entry point is intentionally tiny:

```cpp
#include "app/AppRuntime.h"

AppRuntime app;

void setup() { app.begin(); }
void loop() { app.update(); }
```

The runtime is split into focused compiled units:

```text
firmware/src/
├── app/
│   ├── AppRuntime.*
│   ├── RuntimeGlobals.*
│   ├── HomeGesture.*
│   └── ProfileManager.*
├── hardware/
│   ├── BoardConfig.h
│   └── Display.*
├── input/
│   ├── Buttons.*
│   ├── Touch.*
│   ├── Motion.*
├── profiles/
│   ├── MouseProfile.*
│   ├── TextProfile.*
│   ├── SettingsProfile.*
│   ├── SpeakerProfile.*
│   └── SpeakerStream.*
└── ui/
    ├── Theme.h
    └── ScreenRenderer.*
```

`ProfileManager` owns synchronous profile/screen dispatch. Profile-specific state and actions are assigned to their concrete profile modules. `ScreenRenderer` owns launcher/profile/text/settings rendering. `SpeakerStream` owns the TCP socket/session, PSRAM ring buffer, playback state, and transport metrics.

Reusable services remain in `firmware/lib/` and were not modified by this migration.

---

# Companion architecture

The Companion is one native Rust process with responsibilities split across modules.

Conceptually:

```text
main
 │
 ├── app / UI
 ├── settings
 ├── single-instance control
 ├── BLE supervisor
 │    ├── presence lifecycle
 │    ├── speech service
 │    └── Speaker service
 ├── speech
 │    ├── ADPCM decode
 │    ├── parity / sequence validation
 │    ├── Whisper
 │    └── SendInput
 ├── audio
 │    └── Windows loopback
 ├── dsp
 ├── speaker
 │    ├── Wi-Fi provisioning
 │    ├── TCP transport
 │    └── endpoint recovery
 ├── wifi
 └── logging
```

---

# BLE presence lifecycle

The Companion tracks four presence states:

```text
Dormant
   │
   ▼
Activating
   │
   ▼
 Ready
   │
   │ BLE disconnect
   ▼
 Grace
   │
   ├── device returns ─────────► Ready
   │
   └── deadline expires ───────► Dormant
```

## Dormant

- Companion watches quietly
- heavy speech runtime is not retained
- Speaker is idle

## Activating

- device is being prepared
- runtime is considered awake

## Ready

- ESPCube is connected over BLE
- required services are available
- connection generation increments on a new Ready transition
- optional show-on-connect behavior can surface the window

## Grace

- a disconnect has occurred
- reconnect scanning continues
- Whisper can remain loaded
- the grace deadline is configurable
- the default is 180 seconds

When Grace expires, Whisper is dropped and the runtime returns to Dormant.

### Why Grace exists

Without Grace, a brief BLE interruption would unnecessarily:

- unload Whisper
- discard warm runtime state
- force full rediscovery/reinitialization immediately

Grace turns short disconnects into a recoverable lifecycle event rather than a full cold restart.

---

# Speech architecture

The speech path is more than “send microphone audio to Whisper.”

```text
Microphones
   │
   ▼
ES7210
   │
   ▼
ESP32-S3
   │
   ├── IMA ADPCM
   ├── packet sequence
   └── sample/frame totals
   │
   ▼
BLE AUDIO notifications
   │
   ▼
Companion SpeechSession
   │
   ├── packet format validation
   ├── sequence-gap accounting
   ├── ADPCM decode
   ├── decoded PCM accumulation
   ├── expected sample count
   └── expected frame count
   │
   ▼
transport parity validation
   │
   ▼
Whisper
   │
   ▼
Unicode SendInput
```

## Packet integrity

The Companion tracks:

- expected sequence number
- sequence gaps
- audio packet count
- invalid packet count
- decoded PCM sample count

At `END`, firmware-provided metadata is checked against the Companion's observed transport.

A speech session is only considered transport-clean when:

- sample totals match
- frame totals match
- firmware reports zero notification failures
- no sequence gaps were observed
- no invalid packets were observed
- decoded PCM is non-empty

## Notification ordering protection

Windows BLE notification ordering can produce edge cases around speech boundaries.

v1 handles two important cases:

### AUDIO before START

A small pre-START staging queue can retain the beginning of a sequence that starts at frame 0.

### END before final AUDIO

`END` is deferred rather than finalized immediately. The Companion waits for the expected frame/sample counts or an 800 ms deadline before finalizing.

This makes the transport tolerant of control/audio notification ordering without silently accepting corrupted parity.

---

# Persistent Whisper architecture

The model is loaded once into a `WhisperEngine` context while speech is enabled and the device runtime is active.

Each utterance creates an inference state from the persistent context rather than launching a separate process.

Current inference configuration includes:

- English
- greedy decoding
- 8 threads
- no timestamps
- no translation
- no prior context

The practical effect is that the expensive model load is not repeated per utterance.

---

# Native text injection

After transcription, the Companion appends the product's trailing-space contract and injects the text using native Windows `SendInput`.

For each UTF-16 unit:

```text
Unicode key down
      │
      ▼
Unicode key up
```

This avoids depending on clipboard paste or an external automation tool.

---

# Speaker architecture

Speaker uses BLE as the control plane and TCP as the PCM data plane.

```text
BLE status says Speaker READY
      │
      ▼
Load trusted current Windows Wi-Fi
      │
      ├── reuse already-connected Cube Wi-Fi if possible
      ├── connect configured Wi-Fi if available
      └── otherwise provision SSID/password over BLE
      │
      ▼
Connect TCP :47821
      │
      ▼
Start Windows system loopback
      │
      ▼
DSP / resampling
      │
      ▼
320-sample PCM16 chunks
      │
      ▼
TCP
      │
      ▼
Device PCM ring buffer
      │
      ▼
I²S playback path
```

---

# Device-side PCM ring buffer

The v1 ring buffer is a purpose-built byte ring backed by ESP32-S3 PSRAM.

Allocation requests:

```text
MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
```

The structure tracks:

- head
- tail
- current size
- capacity
- high-water mark

`push()` clamps writes to free capacity.  
`pop()` clamps reads to available data.  
Both wrap around the buffer boundary.

The ring decouples network arrival timing from audio playback timing.

That separation is essential for a streamed-audio path: TCP delivery and I²S consumption do not run with identical timing.

---

# Speaker recovery behavior

The Companion monitors Speaker readiness while streaming.

## Windows endpoint change

If loopback capture reports an endpoint change:

1. Speaker status becomes Recovering.
2. old capture is dropped
3. queued capture messages are drained
4. capture is retried
5. DSP is rebuilt for the new input sample rate
6. streaming resumes

## TCP write failure

If a TCP write fails while the device is still Ready:

1. current Speaker status is re-read over BLE
2. existing Wi-Fi is reused if still connected
3. otherwise Wi-Fi can be re-provisioned/reconnected
4. TCP is re-established
5. pending PCM is cleared
6. queued old audio is drained
7. DSP state is rebuilt

This keeps recovery scoped to the broken layer.

---

# Why BLE + TCP instead of one transport?

BLE is a strong fit for:

- discovery
- presence
- low-volume control
- GATT status
- speech payloads at the v1 compressed rate

Continuous Speaker PCM is a different workload.

v1 therefore uses:

```text
BLE = identity + control plane
TCP = high-bandwidth Speaker data plane
```

This keeps each transport responsible for the job it handles well.

---

# Trusted Wi-Fi boundary

Trusted network state is owned by the Windows Companion.

```text
%LOCALAPPDATA%\ESPCube\trusted_wifi.json
```

The public firmware contains blank credential constants and does not ship a personal network password.

During Speaker activation, the Companion can send the current trusted network to the device over the Speaker BLE command characteristic.

---

# Desktop lifecycle

The Companion is intentionally not a one-shot console process.

It supports:

- startup with Windows
- background launch
- hidden window state
- show-on-connect
- persistent JSON settings
- single-instance coordination over localhost
- device presence tracking
- graceful runtime sleep

The single-instance control address is:

```text
127.0.0.1:47823
```

A second launch can send a `SHOW` request to the primary instance.

---

# Failure boundaries

ESPCube v1 tries to fail locally:

| Failure | Intended scope |
|---|---|
| HID issue | input path |
| BLE disconnect | enter Grace / reconnect |
| speech parity mismatch | reject that utterance |
| Whisper error | speech service only |
| Windows input failure | text insertion only |
| audio endpoint change | restart loopback/DSP |
| TCP write failure | reconnect Speaker data path |
| Wi-Fi unavailable | Speaker only |
| Companion hidden | UI lifecycle only |

The device's universal baseline should not disappear merely because an optional service fails.

---

# Architectural invariants

Future changes should preserve:

1. HOME remains physically recoverable.
2. Basic HID does not depend on the Companion.
3. Wi-Fi is not required for ordinary controls.
4. Personal credentials do not live in public firmware.
5. profile teardown releases temporary resources.
6. release changes are proven on the actual device.
7. stable behavior is not sacrificed for a release-time refactor.

See [`DESIGN_PRINCIPLES.md`](DESIGN_PRINCIPLES.md).

---

## Related docs

- [Protocol](PROTOCOL.md)
- [Companion](COMPANION.md)
- [Hardware](HARDWARE.md)
- [Adding a Profile](ADDING_A_PROFILE.md)


## Profile lifecycle and synchronous dispatch

`MouseProfile` and `SpeakerProfile` are the two device-level profiles and implement the lightweight `ProfileLifecycle` interface (`enter()` / `exit()`). `TextProfile` and `SettingsProfile` intentionally remain screen-mode behavior owners rather than values in the device-level `Profile` enum; this preserves the v1 product model in which Text and Settings are UI modes layered on the standard device state.

Input dispatch is deliberately direct, synchronous, allocation-free, and queue-free. The earlier unused `InputEvent` contract was removed rather than introducing an event bus that could change ordering or latency. Physical button, touch, HOME, motion, speech, and Speaker branches therefore retain their established main-loop ordering.
