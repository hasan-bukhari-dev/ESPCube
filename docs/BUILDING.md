# Building ESPCube from Source

> Reproducible build guide for firmware, Companion, and Windows installer.

[← Back to README](../README.md) · [Release Validation](RELEASE_VALIDATION.md) · [Troubleshooting](TROUBLESHOOTING.md)

---

# Source of truth

Repository:

```text
https://github.com/hasan-bukhari-dev/ESPCube.git
```

v1 release work is expected to build from a clean checkout of the public repository.

---

# Prerequisites

## Common

- Git
- Git LFS
- Windows for the production Companion/installer path

## Firmware

- PlatformIO
- USB data cable
- Waveshare ESP32-S3-Touch-LCD-1.54

## Companion

- Rust toolchain
- Cargo
- Windows native build environment required by the Rust dependencies
- bundled model from Git LFS

## Installer

- Inno Setup as required by `companion/scripts/build-installer-windows.ps1`

---

# Clone

```powershell
git lfs install
git clone https://github.com/hasan-bukhari-dev/ESPCube.git
cd ESPCube
```

Confirm the model is managed by Git LFS:

```powershell
git lfs ls-files
```

Expected model path:

```text
companion/models/ggml-tiny.en.bin
```

---

# Firmware build

From repository root:

```powershell
cd firmware
platformio run -e espcube
```

The environment name is:

```text
espcube
```

---

# Firmware flash

Automatic/default port:

```powershell
platformio run -e espcube -t upload
```

Explicit port:

```powershell
platformio run -e espcube -t upload --upload-port COM22
```

Find Windows serial ports:

```powershell
Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID, Name
```

Repository helper:

```powershell
.\scripts\flash-windows.ps1 -Port COM22
```

---

# Firmware build artifacts

PlatformIO output is under:

```text
firmware\.pio\build\espcube\
```

Release-relevant outputs include:

```text
firmware.bin
firmware.factory.bin
bootloader.bin
partitions.bin
```

Generated `.pio` output is not source.

---

# Companion checks

From repository root:

```powershell
cd companion
cargo check
```

---

# Companion tests

```powershell
cargo test
```

v1 contains focused tests covering behavior such as:

- Speaker status parsing
- speech control-number parsing
- trailing-space insertion contract

---

# Companion release build

```powershell
cargo build --release
```

Production runtime is native Rust.

Normal use does not depend on Python or `whisper-cli.exe`.

---

# Build the Windows installer

From `companion/`:

```powershell
.\scripts\build-installer-windows.ps1
```

Expected output:

```text
companion\installer\output\ESPCube-Companion-v1.0.0-Setup.exe
```

The installer staging/output directories are generated build material, not canonical source.

---

# Installer contents

The v1 installer bundles:

```text
ESPCube Companion.exe
models\ggml-tiny.en.bin
```

Install target:

```text
%LOCALAPPDATA%\Programs\ESPCube Companion
```

---

# Source-build verification helper

From repository root:

```powershell
.\scripts\verify-source-build.ps1
```

Use this before calling a source checkout release-ready.

---

# Clean rebuild discipline

For a high-confidence build:

```powershell
git status
git lfs status
```

Then clean generated output as appropriate and rebuild from the checked-in source.

Do not use an old binary as proof that current source builds.

---

# Release bundle

A v1 release bundle contains:

```text
ESPCube-Companion-v1.0.0-Setup.exe
firmware.bin
firmware.factory.bin
bootloader.bin
partitions.bin
SHA256SUMS.txt
```

Generate SHA-256 hashes from the exact final artifacts being released.

---

# What not to commit

Generated or machine-local material should remain outside canonical source history, including:

```text
firmware/.pio/
companion/target/
companion/installer/release/
companion/installer/output/
```

Personal credentials must not be committed.

---

# Build vs runtime proof

A successful build proves compilation.

It does **not** prove:

- BLE behavior
- physical touch/IMU behavior
- speech transport
- Windows text injection
- Speaker audio
- reconnect behavior

A release candidate should still be tested on hardware.

See [`RELEASE_VALIDATION.md`](RELEASE_VALIDATION.md).

---

## Related docs

- [Release Validation](RELEASE_VALIDATION.md)
- [Architecture](ARCHITECTURE.md)
- [Troubleshooting](TROUBLESHOOTING.md)
