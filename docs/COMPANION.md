# Windows Companion

The production Companion source is under `companion/`.

It is a native Rust application and does not require Python for normal use.

Main responsibilities:
- discover and track ESPCube over BLE
- keep the Whisper model loaded while the Cube is active
- receive and decode speech audio
- transcribe locally
- inject recognized text through native Windows input
- capture the default Windows audio output
- stream Speaker audio to ESPCube over TCP
- manage trusted Wi-Fi credentials
- run quietly in the background
- expose a small native settings/status window

The release installer bundles the exact v1 Whisper model.
