# ESPCube Hardware

> Hardware reference for the Waveshare ESP32-S3-Touch-LCD-1.54 target used by ESPCube v1.0.0.

[← Back to README](../README.md) · [Architecture](ARCHITECTURE.md) · [Protocol](PROTOCOL.md)

---

# Board overview

| Component | ESPCube v1 hardware |
|---|---|
| MCU | ESP32-S3R8 |
| PSRAM | 8 MB |
| Flash | 16 MB |
| Display | 1.54" 240×240 ST7789 |
| Touch | CST816S |
| IMU | QMI8658 6-axis |
| Microphone ADC | ES7210 |
| Audio codec | ES8311 |
| Power amplifier | NS4150B |
| Wireless | 2.4 GHz Wi-Fi + Bluetooth LE |

---

# Hardware roles

## ESP32-S3

The MCU owns the profile runtime and coordinates:

- touchscreen input
- physical controls
- IMU motion
- Bluetooth HID
- BLE services
- speech capture/transport
- Speaker network receive
- PCM buffering
- audio playback control
- launcher/profile state

8 MB of PSRAM is especially useful for streamed-audio buffering.

---

## Display

**Controller:** ST7789  
**Resolution:** 240×240

The display is the main profile surface:

- launcher
- Mouse UI
- Text UI
- Speaker UI
- Settings

---

## Touch

**Controller:** CST816S

Touch shares the device's I²C bus.

Pins:

```text
SDA  GPIO42
SCL  GPIO41
RST  GPIO47
INT  GPIO48
```

---

## IMU

**Sensor:** QMI8658 6-axis

The IMU provides the motion source for the Mouse profile.

The firmware maps gyro behavior into standard Bluetooth HID pointer movement.

---

# Audio input path

```text
Dual microphones
      │
      ▼
    ES7210
      │
      ▼
     I²S
      │
      ▼
  ESP32-S3
      │
      ▼
speech transport
```

The ES7210 is the microphone-side audio frontend used by the speech path.

---

# Audio output path

```text
TCP PCM stream
      │
      ▼
ESP32-S3
      │
      ▼
PSRAM PCM ring buffer
      │
      ▼
I²S
      │
      ▼
ES8311 codec
      │
      ▼
NS4150B amplifier
      │
      ▼
Speaker
```

The network receive path and I²S playback path are decoupled by the PCM ring buffer.

---

# I²S pins

| Signal | GPIO |
|---|---:|
| MCLK | 8 |
| BCLK | 9 |
| WS / LRCLK | 10 |
| DIN | 11 |
| DOUT | 12 |
| PA control | 7 |

The same physical audio interface supports the board's input/output audio devices.

---

# Display pins

| Signal | GPIO |
|---|---:|
| LCD CS | 21 |
| LCD CLK | 38 |
| LCD MOSI | 39 |
| LCD RST | 40 |
| LCD DC | 45 |
| LCD backlight | 46 |

---

# Shared I²C ownership

ESPCube firmware owns the common I²C initialization:

```cpp
Wire.begin(42, 41);
```

This is an important integration rule.

Libraries sharing that bus should not independently reinitialize `Wire` with a conflicting configuration.

---

# Full pinout

| Function | GPIO |
|---|---:|
| I²C SDA | 42 |
| I²C SCL | 41 |
| Touch RST | 47 |
| Touch INT | 48 |
| LCD CS | 21 |
| LCD CLK | 38 |
| LCD MOSI | 39 |
| LCD RST | 40 |
| LCD DC | 45 |
| LCD BL | 46 |
| Audio MCLK | 8 |
| Audio BCLK | 9 |
| Audio WS | 10 |
| Audio DIN | 11 |
| Audio DOUT | 12 |
| PA control | 7 |

---

# Wireless roles

## Bluetooth HID

Used for direct standard host controls.

## Bluetooth LE

Used for:

- presence
- speech service
- Speaker control/status
- Wi-Fi provisioning commands

## Wi-Fi

Used only for the high-bandwidth Speaker audio data path in v1.

Normal HID operation does not depend on Wi-Fi.

---

# Memory use worth knowing

The Speaker ring buffer explicitly allocates its backing store from PSRAM using ESP heap capabilities.

That makes PSRAM a functional part of the streaming architecture rather than merely unused board capacity.

The ring buffer tracks a high-water mark, which is useful when evaluating how much buffering the stream actually consumes under real timing.

---

# Hardware invariants

Changes should preserve:

- shared I²C ownership
- correct display pin mapping
- correct I²S pin mapping
- PA control behavior
- HOME button behavior
- ring-buffer allocation in suitable memory
- no unnecessary Wi-Fi activation outside profiles that need it

---

## Related docs

- [Architecture](ARCHITECTURE.md)
- [Protocol](PROTOCOL.md)
- [Adding a Profile](ADDING_A_PROFILE.md)
