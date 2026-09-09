# Architecture

ESPCube v1 intentionally preserves the proven runtime architecture.

## Firmware

`firmware/src/main.cpp` is the current UI/profile coordinator.

Reusable hardware/service code lives under `firmware/lib/`:
- ESPCubeHID
- ESPCubeSpeech
- ESPCubeSpeechLink
- ESPCubeSpeakerControl
- ESPCubeSpeakerPlayback
- ESPCubeSpeakerPcmRingBuffer
- ESPCubeSpeakerVolume
- ESPCubeSpeakerB1Test

The v1 release does not perform a risky last-minute rewrite into separate profile
classes. Future releases can extract profiles behind a stable interface.

## Companion

The Windows Companion is a single Rust executable. Its major modules are:
- BLE lifecycle/discovery
- persistent Whisper speech engine
- native Windows text injection
- WASAPI/CPAL audio capture
- Speaker DSP and TCP transport
- trusted Wi-Fi provisioning
- autostart, logging, settings, and single-instance control

Normal users do not need Python or the development harnesses used during bring-up.
