# ESPCube Quick Start

> Zero-to-working setup for ESPCube v1.0.0 on Windows.

[← Back to README](../README.md) · [Troubleshooting](TROUBLESHOOTING.md) · [Building from source](BUILDING.md)

---

## What you need

- Waveshare ESP32-S3-Touch-LCD-1.54 running ESPCube firmware
- Windows PC with Bluetooth
- Wi-Fi only if you want to use the **Speaker** profile
- USB data cable if firmware still needs to be flashed
- ESPCube Companion v1.0.0

---

# Fast path

If the Cube is already flashed:

1. Install the Windows Companion.
2. Pair ESPCube in Windows Bluetooth settings if needed.
3. Keep **Show when ESPCube connects** enabled if you want the window surfaced on connection.
4. Leave **Start quietly with Windows** off for manual-only use, or enable it if you want the background watcher.
5. Turn on ESPCube.
6. Wait for BLE connection.
7. Pick a profile.

That is the normal product workflow.

---

# 1. Install the Windows Companion

Download the current installer:

**[ESPCube-Companion-v1.0.0-Setup.exe](https://github.com/hasan-bukhari-dev/ESPCube/releases/download/v1.0.0/ESPCube-Companion-v1.0.0-Setup.exe)**

Run it normally.

The installer:

- installs for the current Windows user
- installs under `%LOCALAPPDATA%\Programs\ESPCube Companion`
- bundles the Whisper model used by v1
- creates a Start Menu entry
- leaves background startup opt-in through the Companion setting
- includes uninstall support

Normal users do **not** need Python, `whisper-cli.exe`, Cargo, or a separate model download.

---

# 2. Flash ESPCube firmware

Skip this section if the device is already running the v1 firmware.

From the repository:

```powershell
cd firmware
platformio run -e espcube -t upload
```

If PlatformIO needs the port explicitly:

```powershell
platformio run -e espcube -t upload --upload-port COM22
```

Find the port on Windows:

```powershell
Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID, Name
```

Or use the repository helper:

```powershell
.\scripts\flash-windows.ps1 -Port COM22
```

Replace `COM22` with the actual device port.

---

# 3. Pair ESPCube

Open:

```text
Windows Settings
→ Bluetooth & devices
→ Add device
→ Bluetooth
→ ESPCube
```

Pair if Windows asks.

For normal use, Bluetooth should remain enabled.

---

# 4. Recommended Companion settings

Open ESPCube Companion and enable:

```text
Show when ESPCube connects      ON
Start quietly with Windows      OFF (optional)
Speech typing                   ON
Windows Speaker mirror          ON
```

The default disconnect grace is three minutes.

---

# 5. First-use test

## Mouse

1. Open **Mouse** on ESPCube.
2. Move the device.
3. Confirm the Windows pointer moves.
4. Test the physical click controls.
5. Hold **A + C** to return HOME.

If Mouse works, the standard HID path is working.

---

## Text + speech

1. Open Notepad or another normal text field.
2. Open **Text** on ESPCube.
3. Make sure the Companion shows Speech as ready.
4. Trigger speech capture.
5. Say:

```text
testing espcube speech one two three
```

6. Confirm the text appears in the focused application.

The production path is:

```text
ESPCube mic
  → IMA ADPCM
  → BLE
  → Companion
  → local Whisper
  → native Windows input
```

---

## Speaker

Before the first Speaker session on a Wi-Fi network:

1. Connect Windows to that Wi-Fi network.
2. Open the Companion.
3. Trust/save the current network if it is not trusted yet.
4. Open **Speaker** on ESPCube.
5. Play audio on Windows.

The Speaker path is:

```text
Windows loopback audio
  → Companion DSP
  → TCP over local Wi-Fi
  → ESPCube ring buffer
  → ES8311
  → amplifier
  → speaker
```

If you change networks later, trust the new current network before using Speaker there.

---

# 6. Normal daily use

Tomorrow, next week, or after a reboot:

1. Log into Windows.
2. The Companion starts quietly in the background if startup is enabled.
3. Turn on ESPCube.
4. Wait for BLE.
5. The Companion detects the device.
6. With **Show when ESPCube connects** enabled, the window appears.
7. Choose the profile you need.
8. Hold **A + C** whenever you want HOME.

No development commands are required for normal use.

---

# Companion window behavior

## Minimize

Minimize keeps the window visible on the taskbar.

## X button

- With **Start quietly with Windows** off, `X` exits the Companion.
- With it on, `X` hides the window and leaves the BLE watcher alive.
- Use **Quit** to terminate a background instance explicitly.

## Reopen

Start Menu:

```text
ESPCube Companion
```

PowerShell:

```powershell
Start-Process "$env:LOCALAPPDATA\Programs\ESPCube Companion\ESPCube Companion.exe"
```

If a hidden instance does not restore correctly:

```powershell
Get-Process "ESPCube Companion" -ErrorAction SilentlyContinue |
    Stop-Process -Force

Start-Process "$env:LOCALAPPDATA\Programs\ESPCube Companion\ESPCube Companion.exe"
```

---

# What requires the Companion?

| Capability | Companion? | Wi-Fi? |
|---|---:|---:|
| Mouse / ordinary HID | No | No |
| Text touchscreen behavior | No / profile-dependent | No |
| Local speech recognition | Yes | No |
| Speaker | Yes | Yes |
| Device Settings | No | No |
| Companion Settings | Yes | No |

---

# Demo checklist

Before showing ESPCube to someone:

- [ ] ESPCube powers on
- [ ] Bluetooth reconnects
- [ ] Companion becomes Ready
- [ ] Mouse moves the pointer
- [ ] Text speech inserts a short sentence
- [ ] Speaker plays Windows audio
- [ ] Settings opens
- [ ] A + C returns HOME
- [ ] Speaker works again after leaving/re-entering the profile

If these pass, the shipping v1 workflows are ready to demo.

---

## Next

- [Architecture](ARCHITECTURE.md)
- [Companion](COMPANION.md)
- [Troubleshooting](TROUBLESHOOTING.md)
