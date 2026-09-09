# Building from Source

## Firmware

Requirements:
- PlatformIO
- USB data connection to the Waveshare ESP32-S3-Touch-LCD-1.54

From `firmware/`:

```powershell
platformio run -e espcube
```

Flash:

```powershell
platformio run -e espcube -t upload --upload-port COM22
```

The COM port can differ by machine.

## Companion

Requirements:
- Rust toolchain
- Windows
- the model at `companion/models/ggml-tiny.en.bin`

From `companion/`:

```powershell
cargo check
cargo test
cargo build --release
```

The source uses `whisper-rs`; `whisper-cli.exe` is not part of the normal product path.
