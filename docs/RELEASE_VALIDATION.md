# ESPCube Release Validation

> v1.0.0 validation record and future release checklist.

[← Back to README](../README.md) · [Building](BUILDING.md) · [Troubleshooting](TROUBLESHOOTING.md)

---

# Release philosophy

A release is not considered proven because an existing developer folder works.

The target is:

```text
public repository
      ↓
fresh checkout
      ↓
source build
      ↓
physical device
      ↓
installed desktop software
      ↓
real runtime workflows
```

---

# v1.0.0 public release

Release:

```text
https://github.com/hasan-bukhari-dev/ESPCube/releases/tag/v1.0.0
```

Published assets:

```text
ESPCube-Companion-v1.0.0-Setup.exe
firmware.bin
firmware.factory.bin
bootloader.bin
partitions.bin
SHA256SUMS.txt
```

---

# Source validation

v1.0.0 was verified from a fresh clone of the public GitHub repository.

Checks included:

- clean `main`
- Git LFS model present
- Companion version `1.0.0`
- source verification script
- firmware build
- Companion build/tests
- installer build

---

# Whisper model

v1 model SHA-256:

```text
921E4CF8686FDD993DCD081A5DA5B6C365BFDE1162E72B08D75AC75289920B1F
```

Model path:

```text
companion/models/ggml-tiny.en.bin
```

The release installer bundles this model.

---

# Final release artifact hashes

## bootloader.bin

```text
31B3C1BE45DC5A76AA85C82540D6787B675E711EAF11021A5EEA7E36F469C6DE
```

## ESPCube-Companion-v1.0.0-Setup.exe

```text
0C94DF4423500F12F5868C3DFD81C09DC9393D8FEA226C069D24B1496D1B4037
```

## firmware.bin

```text
8F102601A3303CB94B4C5C601DA5F12765BAFCA5ED8782795281920B96FD3029
```

## firmware.factory.bin

```text
5859609EE9172B43F23639648565D06CD69FFA6DD8A6EEB80C1B4F12AF4882AA
```

## partitions.bin

```text
1D9CCA96DE0FE07AD7FC0648B9878DDECD9CE565E38B589AD20FEA698ED4C80C
```

The GitHub release also publishes `SHA256SUMS.txt`.

---

# Companion executable hash used to build installer

Fresh final Companion executable SHA-256:

```text
D2403D804F8FFFDD8ED12567480F649FFC10222BEFA8420E7B42ED33AD7AF4BE
```

---

# Physical firmware validation

The final fresh-clone firmware was flashed to the ESPCube hardware before release.

Build/flash proof included:

- successful PlatformIO build
- successful device upload
- final source-derived firmware image
- real device boot/runtime

---

# Companion validation

The final Companion path was tested as an installed product, not only from `cargo run`.

Validation included:

- release build
- installer build
- installer execution
- installed executable launch
- bundled model
- runtime connection to ESPCube

---

# Runtime matrix

The final installed-runtime validation passed:

| Workflow | v1.0.0 |
|---|---|
| Mouse | PASS |
| Text | PASS |
| Speech via Companion | PASS |
| Speaker | PASS |
| Settings | PASS |
| A + C HOME | PASS |
| Speaker disconnect/reconnect | PASS |
| Installed Companion launch | PASS |

---

# GitHub release verification

The v1.0.0 GitHub release was checked after publication.

All six release assets were present with matching SHA-256 digests.

That closes the release chain:

```text
source
→ build
→ device
→ installer
→ runtime
→ release assets
```

---

# Future release checklist

## Source

- [ ] working tree clean
- [ ] version normalized
- [ ] no personal credentials
- [ ] Git LFS model present
- [ ] source verification script passes

## Firmware

- [ ] clean build
- [ ] firmware artifacts generated
- [ ] flash succeeds on target board
- [ ] launcher renders
- [ ] touch works
- [ ] buttons work
- [ ] IMU works
- [ ] HOME works

## Companion

- [ ] `cargo check`
- [ ] `cargo test`
- [ ] release build
- [ ] installer build
- [ ] installed executable launches
- [ ] model found from installed location

## Mouse

- [ ] pointer movement
- [ ] click controls
- [ ] profile exit

## Text

- [ ] speech START
- [ ] BLE audio
- [ ] parity check
- [ ] Whisper inference
- [ ] native text injection
- [ ] profile exit

## Speaker

- [ ] trusted-network provisioning
- [ ] Wi-Fi connect/reuse
- [ ] TCP connect
- [ ] system loopback capture
- [ ] DSP
- [ ] PCM ring buffer playback
- [ ] volume behavior
- [ ] profile exit
- [ ] reconnect

## Lifecycle

- [ ] Dormant
- [ ] Activating
- [ ] Ready
- [ ] Grace
- [ ] reconnect during Grace
- [ ] unload after Grace expiry
- [ ] show-on-connect
- [ ] startup/background behavior

## Release

- [ ] stage exact final artifacts
- [ ] generate SHA256SUMS
- [ ] create GitHub release
- [ ] verify every published asset
- [ ] download/verify if release process changes

---

# Rule

Do not call a future release PASS until the evidence for that exact release candidate exists.

That discipline is what keeps “works on my machine” from becoming the release standard.
