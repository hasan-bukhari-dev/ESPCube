# ESPCube Windows Companion

> Native Windows application that extends ESPCube with local speech recognition, system-audio streaming, lifecycle management, and configuration.

[← Back to README](../README.md) · [Quick Start](QUICK_START.md) · [Architecture](ARCHITECTURE.md) · [Troubleshooting](TROUBLESHOOTING.md)

---

# What the Companion is

The production Companion is a **native Rust application**.

It is not a Python wrapper around development scripts.

Its job is to provide host-side capabilities that standard Bluetooth HID cannot provide cleanly:

- BLE discovery and presence
- custom ESPCube service handling
- local Whisper inference
- speech packet validation/decoding
- native Windows text insertion
- Windows loopback audio capture
- Speaker DSP
- trusted Wi-Fi provisioning
- TCP Speaker transport
- background startup
- persistent settings
- single-instance lifecycle
- status UI

---

# What the Companion is not

The Companion is **not** required for ordinary Bluetooth HID behavior.

That distinction is important:

```text
Mouse / standard HID
ESPCube ─────────────────────────► Windows

Extended services
ESPCube ◄──── BLE ────► Companion
```

---

# Installation

The v1 installer is:

```text
ESPCube-Companion-v1.0.0-Setup.exe
```

It installs per-user at:

```text
%LOCALAPPDATA%\Programs\ESPCube Companion
```

The installer bundles:

```text
ESPCube Companion.exe
models\ggml-tiny.en.bin
```

It also:

- creates a Start Menu shortcut
- registers startup under the current user's `Run` key
- launches the app after installation unless install is silent
- includes an uninstaller

The startup command is:

```text
"ESPCube Companion.exe" --background
```

---

# Module map

Conceptually:

```text
companion/src/
│
├── main.rs              startup / app wiring
├── app.rs               egui UI + window lifecycle
├── ble.rs               unified BLE supervisor
├── state.rs             shared presence/service state
├── speech.rs            ADPCM + Whisper + SendInput
├── speaker.rs           Wi-Fi + audio + TCP worker
├── audio.rs             Windows system loopback capture
├── dsp.rs               Speaker processing / output shaping
├── wifi.rs              trusted Windows Wi-Fi state
├── settings.rs          persistent product settings
├── single_instance.rs   localhost SHOW control
├── autostart.rs         Windows startup behavior
└── logging.rs           runtime log output
```

---

# Presence state machine

```text
Dormant
   │
   ▼
Activating
   │
   ▼
 Ready
   │
   │ disconnect
   ▼
 Grace
   │
   ├── reconnect ─────► Ready
   └── timeout ───────► Dormant
```

The shared state includes:

- presence
- connected device name
- connection generation
- grace deadline
- runtime-awake flag
- speech status/detail
- last transcript
- Speaker status/detail
- active Wi-Fi SSID
- active audio device
- last error

---

# Default settings

The default Companion configuration is:

| Setting | Default |
|---|---:|
| Show when ESPCube connects | `true` |
| Start with Windows | `false` |
| Disconnect grace | `180 s` |
| Speech enabled | `true` |
| Speaker enabled | `true` |
| Whisper model override | empty / auto-resolve |

Settings file:

```text
%LOCALAPPDATA%\ESPCube\companion.json
```

---

# Speech engine

## Model resolution

When no explicit model path is configured, the Companion first checks:

```text
<installed exe folder>\models\ggml-tiny.en.bin
```

The release installer places the model there.

## Persistent context

The model is loaded into a `WhisperContext`.

The context is retained while ESPCube is active and speech is enabled.

The Companion creates a new Whisper state per inference rather than reloading the model per utterance.

## Inference settings

v1 uses:

```text
sample rate     16 kHz speech PCM
language        English
sampling        greedy
best_of         1
threads         8
translate       false
timestamps      false
prior context   disabled
```

---

# Speech transport handling

The Companion maintains a `SpeechSession` containing:

- active/inactive state
- decoded PCM
- expected sequence
- sequence-gap count
- received audio packet count
- invalid-packet count

An IMA ADPCM packet contains transport metadata including:

- packet signature/version
- sequence number
- sample count
- initial predictor
- ADPCM index
- packed nibbles

The decoder reconstructs PCM16 and validates the packet structure.

---

# Boundary-ordering protection

Two ordering protections are built into the BLE manager.

## Pre-START staging

If valid audio begins before the `START` control notification is processed, frame 0 can start a small staging run.

When START arrives, a staged run beginning at sequence 0 is applied to the new speech session.

## Deferred END

`END` is not finalized immediately.

A pending END stores:

- message
- expected samples
- expected frames
- deadline

The Companion waits until parity is complete or the 800 ms deadline expires.

This specifically handles the case where final AUDIO arrives after END through the Windows BLE notification path.

---

# Speech parity gate

Before Whisper is called, the transport must pass the v1 parity contract:

```text
firmware sample count == decoded sample count
firmware frame count  == received audio packets
notify_failures       == 0
sequence gaps         == 0
invalid packets       == 0
decoded PCM           != empty
```

If that fails, the utterance is rejected as a transport error instead of transcribing uncertain audio.

---

# Native Windows text insertion

Recognized text is inserted with the Windows `SendInput` API.

The Companion encodes output as UTF-16 and emits Unicode key-down/key-up input events.

A trailing space is appended after the transcript as part of the current product contract.

This avoids requiring:

- clipboard access
- paste shortcuts
- Python automation
- accessibility scripting

---

# Speaker worker

When the Speaker GATT status reports `READY`, the Companion can create a Speaker worker.

The worker:

1. loads the trusted current Windows network
2. reuses Cube Wi-Fi if already connected
3. otherwise connects an already-configured network
4. otherwise provisions the trusted SSID/password over BLE
5. connects to TCP port 47821
6. starts system loopback capture
7. creates the DSP path
8. streams PCM chunks

---

# Windows audio capture

The Companion captures the current default Windows system output using loopback audio.

The Speaker UI records the active capture device name.

If the endpoint changes, the worker enters a Recovering state and retries capture.

---

# Speaker DSP and chunking

The Speaker worker targets:

```text
OUT_RATE       32000 Hz
CHUNK_SAMPLES  320
CHUNK_BYTES    640
```

Incoming system audio is converted to mono and passed through `SpeakerDsp`.

Processed samples accumulate until a complete 320-sample chunk is available.

That chunk is serialized as little-endian PCM16 and written to the TCP stream.

---

# TCP recovery

TCP is configured with:

- `TCP_NODELAY`
- write timeout
- bounded connection retry

If a write fails while Speaker remains Ready, the Companion can:

- check current device Wi-Fi state over BLE
- reuse the IP if still connected
- otherwise re-provision/reconnect Wi-Fi
- reconnect TCP
- clear old pending audio
- drain queued capture blocks
- rebuild DSP state

---

# Trusted Wi-Fi model

File:

```text
%LOCALAPPDATA%\ESPCube\trusted_wifi.json
```

The Companion determines the current Windows SSID with:

```text
netsh wlan show interfaces
```

A trusted entry contains:

- SSID
- password

The Companion attempts to restrict the file to the current Windows user using `icacls` where available.

The firmware itself does not contain a personal production password.

---

# Single-instance behavior

The Companion uses:

```text
127.0.0.1:47823
```

as a local single-instance control address.

The primary process listens there.

A secondary launch:

1. detects that the address is already owned
2. sends:

```text
SHOW
```

3. exits

The primary process receives the flag and asks egui to make the window visible and focused.

---

# Window lifecycle

## Background start

`--background` causes the first UI update to hide the window.

## Show on connect

When a new BLE connection generation is observed and the setting is enabled:

- window becomes visible
- window requests focus

## X button

- If **Start quietly with Windows** is off, closing the window exits the Companion.
- If **Start quietly with Windows** is on, closing the window hides it and leaves the BLE watcher running.
- **Quit** always exits.

## Grace expiry

When presence transitions from Grace to Dormant, the window can hide again.

---

# Normal use

Recommended:

```text
Show when ESPCube connects      ON
Start quietly with Windows      ON
```

Then:

```text
Windows login
  → Companion starts hidden
  → ESPCube powers on
  → BLE Ready
  → window appears
  → user chooses a profile
```

---

# Reopen / recovery

Normal:

```powershell
Start-Process "$env:LOCALAPPDATA\Programs\ESPCube Companion\ESPCube Companion.exe"
```

If the existing background instance does not surface:

```powershell
Get-Process "ESPCube Companion" -ErrorAction SilentlyContinue |
    Stop-Process -Force

Start-Process "$env:LOCALAPPDATA\Programs\ESPCube Companion\ESPCube Companion.exe"
```

---

## Related docs

- [Quick Start](QUICK_START.md)
- [Architecture](ARCHITECTURE.md)
- [Protocol](PROTOCOL.md)
- [Troubleshooting](TROUBLESHOOTING.md)


# Optional background watcher

The Companion can be used in either of two modes:

1. **Manual mode (default):** Windows startup is off. Open Companion when needed; closing its window exits it.
2. **Background mode (opt-in):** Enable **Start quietly with Windows (optional)**. The app launches hidden at login, remains resident without loading the heavy speech runtime until ESPCube is present, watches through the existing BLE manager, and can surface the existing window when a connection is established if **Show window when ESPCube connects** is enabled.

The two settings are independent. A user may keep the watcher resident without automatically showing the window, or disable startup entirely and use the app only on demand.
