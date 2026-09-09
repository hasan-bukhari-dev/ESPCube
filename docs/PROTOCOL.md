# ESPCube Protocol Reference

> v1.0.0 transport reference for HID, speech BLE, Speaker BLE control, and Speaker TCP audio.

[← Back to README](../README.md) · [Architecture](ARCHITECTURE.md) · [Companion](COMPANION.md)

---

# Transport map

| Function | Transport |
|---|---|
| Standard host controls | Bluetooth HID |
| Companion presence | BLE |
| Speech control | BLE GATT |
| Speech audio | BLE GATT notifications |
| Speaker readiness/control | BLE GATT |
| Wi-Fi provisioning | BLE GATT command |
| Speaker PCM | TCP over local Wi-Fi |

Core rule:

```text
BLE = control / presence / compressed speech
TCP = continuous Speaker PCM
```

---

# Bluetooth HID

Standard HID is the direct host-control path.

```text
ESPCube ── Bluetooth HID ──► host
```

The Companion is not required for this path.

---

# Speech BLE service

Service UUID:

```text
45535043-5542-4c45-8000-000000000001
```

Characteristics:

| Name | UUID |
|---|---|
| CONTROL | `45535043-5542-4c45-8000-000000000002` |
| STATUS | `45535043-5542-4c45-8000-000000000003` |
| AUDIO | `45535043-5542-4c45-8000-000000000004` |

---

# Speech session lifecycle

Conceptually:

```text
CONTROL: START
      │
      ▼
AUDIO notifications
      │
      ├── frame 0
      ├── frame 1
      ├── frame 2
      └── ...
      │
      ▼
CONTROL: END;samples=...;frames=...;notify_failures=...
      │
      ▼
Companion parity gate
      │
      ▼
Whisper
```

---

# Speech audio packet

The Companion expects the v1 speech packet header used by the firmware.

Important fields include:

```text
byte 0-1    packet signature/version
byte 2-3    sequence number (u16 LE)
byte 4-5    sample count (u16 LE)
byte 6-7    initial predictor (i16 LE)
byte 8      ADPCM step index
byte 9      reserved/header continuation
byte 10...  packed IMA ADPCM nibbles
```

The Companion rejects malformed packets and step indices outside the valid IMA range.

---

# IMA ADPCM

Speech transport uses IMA ADPCM to reduce BLE audio payload size.

The Companion maintains:

- predictor
- step index
- standard IMA step table
- standard IMA index adjustment table

Each packed byte yields low and high 4-bit codes until the declared sample count is reconstructed.

---

# Speech sequence accounting

Every audio packet carries a `u16` sequence.

The Companion tracks the next expected sequence.

If a received sequence differs, the wrapping difference contributes to `sequence_gaps`.

This lets transport loss be detected independently of Whisper output quality.

---

# Speech parity contract

At END, the firmware reports transport totals.

The Companion accepts the transport only if:

```text
expected samples == decoded PCM samples
expected frames  == received audio packets
notify_failures  == 0
sequence_gaps    == 0
invalid_packets  == 0
PCM              != empty
```

A failed parity gate produces a transport error and no transcription is injected.

---

# Pre-START staging

The Windows notification stream can present AUDIO before START is processed.

v1 handles this by staging a short consecutive run only when it begins at sequence 0.

The staging queue is intentionally bounded.

When START arrives:

- a valid staged run starting at frame 0 is consumed
- otherwise staged data is discarded

---

# Deferred END drain

The inverse ordering case can also occur:

```text
CONTROL END
arrives before
final AUDIO
```

The Companion creates a `PendingEnd` with an 800 ms deadline.

Finalization occurs when:

- expected samples/frames have arrived, or
- the deadline expires

The parity gate still decides whether the speech transport is valid.

---

# Speaker BLE service

Service UUID:

```text
45535043-5542-4c45-8100-000000000001
```

Characteristics:

| Name | UUID |
|---|---|
| COMMAND | `45535043-5542-4c45-8100-000000000002` |
| STATUS | `45535043-5542-4c45-8100-000000000003` |

---

# Speaker readiness

The Companion only starts Speaker transport when the Speaker status reports:

```text
state=READY
```

Status fields are semicolon-delimited key/value pairs.

Example:

```text
state=READY;wifi_state=CONNECTED;ip=10.0.0.33
```

---

# Wi-Fi states used by Companion

The Companion handles at least these Speaker Wi-Fi states:

```text
CONNECTED
CONFIGURED
other / not yet provisioned
```

## CONNECTED

Reuse the device's existing IP.

## CONFIGURED

Issue:

```json
{"cmd":"wifi_connect"}
```

and wait for an IP.

## Not configured

Provision:

```json
{
  "cmd": "wifi_set",
  "ssid": "<current trusted SSID>",
  "password": "<trusted password>"
}
```

Then issue `wifi_connect`.

---

# Speaker TCP transport

Port:

```text
47821
```

Audio format:

```text
sample rate     32000 Hz
channels        mono
sample format   signed PCM16
byte order      little-endian
```

Companion write unit:

```text
320 samples
640 bytes
```

TCP uses `TCP_NODELAY` in the Companion.

---

# Speaker reconnect behavior

On TCP write failure, the Companion uses BLE state as the control truth.

If the profile is still Ready:

1. inspect device Wi-Fi state
2. reuse current connected IP if possible
3. otherwise provision/reconnect
4. reconnect TCP
5. clear stale pending PCM
6. drain queued capture blocks
7. rebuild DSP state

If the Speaker profile is no longer Ready, the worker exits.

---

# Device playback buffer

Incoming TCP bytes are consumed into `ESPCubeSpeakerPcmRingBuffer`.

The ring buffer:

- lives in PSRAM
- tracks head/tail
- tracks current occupancy
- tracks high-water usage
- clamps writes to free capacity
- clamps reads to available data
- wraps at the capacity boundary

The ring buffer is the timing boundary between network receive and audio playback.

---

# Companion single-instance control

This is a local desktop-control protocol, not a device protocol.

Address:

```text
127.0.0.1:47823
```

Command:

```text
SHOW
```

A secondary Companion process sends `SHOW` to the primary instance and exits.

---

# Versioning note

The protocol documented here describes the v1.0.0 shipping baseline.

Future protocol changes should preserve backward compatibility deliberately rather than accidentally.

---

## Related docs

- [Architecture](ARCHITECTURE.md)
- [Companion](COMPANION.md)
- [Hardware](HARDWARE.md)
