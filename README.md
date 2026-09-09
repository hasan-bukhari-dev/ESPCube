# ESPCube

ESPCube is a universal ESP32-S3 handheld controller and interface built for the
Waveshare ESP32-S3-Touch-LCD-1.54.

The v1 baseline includes:

- **Mouse** â€” gyro mouse with physical click controls
- **Text** â€” touchscreen text entry plus speech-to-text through the Windows Companion
- **Speaker** â€” streams the Windows system audio output to the Cube over temporary Wi-Fi
- **Settings** â€” device settings and calibration controls

## Hardware

Target board:

- Waveshare ESP32-S3-Touch-LCD-1.54
- 240x240 ST7789 display
- CST816S touch controller
- QMI8658 IMU
- ES7210 dual-microphone ADC
- ES8311 audio codec
- NS4150B amplifier
- ESP32-S3R8
- 8 MB PSRAM
- 16 MB flash

## Quick start

### 1. Flash the ESPCube firmware

Install PlatformIO, connect the Cube over USB, then from `firmware/`:

```powershell
platformio run -e espcube -t upload
```

For Windows, a helper is included:

```powershell
.\scripts\flash-windows.ps1 -Port COM22
```

If PlatformIO can identify the port automatically, omit `-Port`.
If multiple serial ports are present, specify the port:

```powershell
platformio run -e espcube -t upload --upload-port COM22
```

### 2. Install the Windows Companion

Download `ESPCube-Companion-v1.0.0-Setup.exe` from the GitHub Releases page and run it.

The installer bundles the exact Whisper model used by ESPCube v1, so normal users do
not need Python, whisper-cli, Cargo, or a separate model download.

### 3. Pair and use ESPCube

The Cube exposes Bluetooth HID functionality directly for standard controls.
The Windows Companion adds speech recognition and Speaker streaming.

See `docs/QUICK_START.md` for the full setup flow.

## Repository layout

```text
firmware/      ESP32-S3 firmware source
companion/     Windows Companion source
docs/          hardware, protocols, architecture, and setup
releases/      release hashes and metadata
```

## Adding profiles

The v1 firmware keeps the proven runtime architecture intact rather than performing a
large pre-release refactor. See `docs/ADDING_A_PROFILE.md` for the extension contract
and recommended path for future profiles.

## Release integrity

See `releases/SHA256SUMS.txt`.
