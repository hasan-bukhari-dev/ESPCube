# Quick Start

## Firmware

1. Install PlatformIO.
2. Connect the Waveshare ESP32-S3-Touch-LCD-1.54 over USB.
3. Open a terminal in `firmware/`.
4. Build:

```powershell
platformio run -e espcube
```

5. Flash:

```powershell
platformio run -e espcube -t upload
```

If needed, add `--upload-port COM22`.

## Windows Companion

Download the current ESPCube Companion installer from GitHub Releases.

The installer:
- installs per-user
- bundles the Whisper model
- creates a Start Menu shortcut
- enables optional/background Windows startup
- includes uninstall support

No Python runtime is required for normal use.

## First use

1. Turn on / reset the Cube.
2. Pair ESPCube over Bluetooth if Windows asks.
3. Start ESPCube Companion.
4. Use Mouse, Text, Speaker, or Settings from the Cube launcher.
5. A+C held together is the hard HOME gesture.
