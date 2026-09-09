ESPCube Companion — FINAL RC1
================================

This is the complete Windows Companion implementation pass before device QA.

The UI is intentionally tiny and functional. The product is the background
behavior, not a fancy dashboard.

C1 Unified Companion Core
- one native Rust executable
- central shared state
- minimal native egui UI
- no Python in the normal path

C2 Presence + Lifecycle
- BLE is the universal presence signal
- broad scan; no hardcoded MAC
- auto wake when ESPCube appears
- default 3-minute disconnect grace
- Whisper stays warm during grace
- heavy runtime unloads only after grace expires
- dormant BLE watcher remains
- background Windows startup
- single-instance control: launching again shows the existing hidden app

C3 Unified BLE Manager
- one ESPCube BLE connection
- verifies both Speech and Speaker services
- persistent CONTROL/AUDIO subscriptions
- reconnect after Cube reboot, BLE loss, range loss, or Windows stack interruption
- no per-utterance scan

C4 Persistent Speech
- exact Speech Protocol v1 UUID/status contract
- exact IMA ADPCM transport decoder
- pre-START AUDIO staging
- robust deferred-END drain for WinRT notification reordering
- exact sample/frame/sequence/notify parity gate
- whisper-rs 0.16.0
- ggml-tiny.en.bin loaded in-process
- model retained through short disconnects
- no WAV in normal path
- no whisper-cli per utterance
- native Win32 SendInput UTF-16 typing
- trailing-space behavior preserved

C5 D2D2 Speaker Integration
- starts only when Cube Speaker profile reports READY
- trusted current Windows SSID matching
- BLE Wi-Fi provisioning
- TCP :47821
- CPAL/WASAPI default-output loopback
- exact proven D2C9 dsp.rs copied from live speaker-bridge-dev
- system audio mirrors to ESPCube
- leaving Speaker stops expensive Windows capture
- Speaker re-entry starts a clean fresh capture/TCP session
- TCP reconnect while Speaker remains READY
- default Windows output endpoint change recovery
- password never written to logs

C6 Settings / Persistence
- %LOCALAPPDATA%\ESPCube\companion.json
- show-on-connect
- start-with-Windows
- 1/3/5/10 minute grace
- Speech enable
- Speaker enable
- trusted current Wi-Fi
- trusted_wifi.json compatibility retained
- best-effort current-user ACL hardening

C7 Minimal UI
- Connected / Connecting / Reconnecting / Dormant
- Speech state + last transcript
- Speaker state
- Wi-Fi/audio endpoint state
- tiny settings panel
- Hide / Open logs / Quit
- dark + soft lavender styling

C8 Windows Lifecycle Hardening
- Cube off/on
- BLE reconnect
- Bluetooth stack temporary failure
- short range loss
- 3-minute dormant transition
- output-device disappearance/change
- app hidden/reopened
- duplicate launch prevention
- local diagnostic log with 2 MB rotation

C9 Packaging / Clean-Machine
- source backup/install
- cargo fmt/check/test/release gate
- per-user Windows install
- Start Menu shortcut
- autostart
- model bundled beside EXE
- uninstall
- clean-machine static audit
- optional Inno Setup source for Setup.exe

Hard product rules
------------------
- D2D2 frozen firmware checkpoint is NEVER modified.
- CODEC_VOLUME stays firmware-owned at the frozen value 80.
- Speaker transport remains TCP.
- Mobile Speaker support is out of scope.
- BLE is universal presence; Wi-Fi is temporary Speaker bandwidth.
- Python proof/oracle tools stay in the repo until final QA/freeze.
