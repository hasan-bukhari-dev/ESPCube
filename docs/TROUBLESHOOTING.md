# ESPCube Troubleshooting

> Symptom-first recovery guide for ESPCube v1.0.0.

[← Back to README](../README.md) · [Quick Start](QUICK_START.md) · [Companion](COMPANION.md)

---

# Start here

| Symptom | Likely layer | First action |
|---|---|---|
| ESPCube is not found | Bluetooth/BLE | verify Bluetooth + device power |
| Mouse works, speech does not | Companion / speech BLE | check Companion speech status |
| Speech is recognized but not typed | Windows input focus | focus a normal text box |
| Speaker opens but no sound | Wi-Fi/TCP/audio capture | verify trusted Wi-Fi + active audio output |
| Speaker stops after device change | audio endpoint | allow recovery / reopen Speaker |
| Companion runs but window is hidden | desktop lifecycle | reopen/restart Companion |
| Firmware will not flash | USB/COM | identify the serial port |
| Source build misses Whisper model | Git LFS | verify LFS checkout |

---

# Companion does not appear

First try:

```powershell
Start-Process "$env:LOCALAPPDATA\Programs\ESPCube Companion\ESPCube Companion.exe"
```

If the process is running but the window stays hidden:

```powershell
Get-Process "ESPCube Companion" -ErrorAction SilentlyContinue |
    Stop-Process -Force

Start-Process "$env:LOCALAPPDATA\Programs\ESPCube Companion\ESPCube Companion.exe"
```

The installed executable should be at:

```text
%LOCALAPPDATA%\Programs\ESPCube Companion\ESPCube Companion.exe
```

---

# Companion does not start with Windows

Check the Companion setting:

```text
Start quietly with Windows
```

The installer uses the current user's Windows startup registration.

If the app has been reinstalled, reopen the Companion and verify the setting.

---

# Companion does not show when ESPCube connects

Check:

```text
Show when ESPCube connects
```

Then:

1. make sure Companion is already running
2. power-cycle ESPCube
3. wait for a new BLE Ready connection

Show-on-connect cannot work if the Companion process is completely stopped.

---

# ESPCube cannot be found over BLE

Check:

- Bluetooth enabled in Windows
- ESPCube powered
- ESPCube advertising/reconnecting
- Companion running
- no major Windows Bluetooth failure

Try:

1. turn ESPCube off
2. wait a few seconds
3. turn it back on
4. restart Companion if necessary

If pairing itself is broken, remove the existing Windows pairing and pair again.

---

# Mouse works but Companion features do not

This is useful diagnostic information.

It means:

```text
Bluetooth HID path     likely working
Companion BLE path     needs investigation
```

Check the Companion status.

If speech and Speaker services are unavailable, focus on BLE discovery/service resolution rather than HID.

---

# Speech unavailable

## Release install

The model should already be bundled.

Expected location:

```text
%LOCALAPPDATA%\Programs\ESPCube Companion\models\ggml-tiny.en.bin
```

## Source build

Expected path:

```text
companion\models\ggml-tiny.en.bin
```

Verify Git LFS:

```powershell
git lfs ls-files
git lfs status
```

---

# Speech says Transport error

The v1 Companion rejects speech when parity fails.

Possible causes include:

- missing BLE audio packet
- sequence gap
- malformed packet
- decoded sample total does not match firmware total
- audio packet count does not match firmware frame count
- firmware reported notification failure

This is intentional: corrupted transport should not be silently passed to Whisper.

Try the utterance again after confirming BLE connection quality.

---

# Speech transcribes but text does not appear

Check:

1. a normal text field is focused
2. the target application accepts ordinary Unicode keyboard input
3. Companion reports insertion success rather than Insert error

The Companion uses native Windows `SendInput`, not clipboard paste.

---

# Speaker says current Wi-Fi is not trusted

The Companion only provisions a network that has been explicitly saved as trusted.

Connect Windows to the desired Wi-Fi network, then save/trust it in the Companion.

Trusted network file:

```text
%LOCALAPPDATA%\ESPCube\trusted_wifi.json
```

---

# Speaker opens but no audio

Check:

- ESPCube is actually in Speaker
- Companion says Speaker is Streaming
- Windows has an active default audio output
- Windows is connected to usable Wi-Fi
- current Wi-Fi is trusted
- Cube Wi-Fi reaches Connected
- TCP transport can connect

Try:

1. return HOME
2. wait briefly
3. reopen Speaker
4. play Windows audio again

---

# Windows audio output changed

The Companion attempts to recover automatically when the loopback endpoint changes.

During recovery it can:

- drop old capture
- drain queued audio
- retry capture
- rebuild DSP
- resume streaming

If recovery does not settle, leave Speaker and reopen it.

---

# Speaker disconnects mid-stream

A TCP write failure triggers scoped recovery while the profile remains Ready.

The Companion can:

- re-read BLE status
- reuse existing Cube Wi-Fi
- reconnect Wi-Fi if needed
- reconnect TCP
- discard stale queued PCM
- rebuild DSP

If the Cube is no longer in Speaker, the worker exits normally.

---

# Firmware does not upload

Check:

- USB cable supports data
- board is powered
- Windows sees a serial device
- correct port is selected

List ports:

```powershell
Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID, Name
```

Flash explicitly:

```powershell
platformio run -e espcube -t upload --upload-port COMxx
```

---

# PlatformIO command not found

If `platformio` is not on PATH but PlatformIO is installed in the common user environment, you can run the full executable path.

Example:

```powershell
& "$HOME\.platformio\penv\Scripts\platformio.exe" run -e espcube
```

---

# Build works but device behavior is wrong

Compilation is not runtime proof.

Re-test:

- Mouse
- Text/speech
- Speaker
- Settings
- A + C HOME
- disconnect/reconnect

See [`RELEASE_VALIDATION.md`](RELEASE_VALIDATION.md).

---

# Resetting Companion configuration

Settings:

```text
%LOCALAPPDATA%\ESPCube\companion.json
```

Trusted Wi-Fi:

```text
%LOCALAPPDATA%\ESPCube\trusted_wifi.json
```

Back up these files before manually changing/removing them.

---

# Useful status interpretation

| Companion status | Meaning |
|---|---|
| Dormant | device absent; heavy runtime asleep |
| Activating | runtime/device setup in progress |
| Ready | ESPCube connected |
| Grace | temporary disconnect; reconnect attempt active |
| Speech Loading | Whisper is loading |
| Speech Listening | speech session active |
| Speech Transcribing | inference running |
| Speech Transport error | parity failed |
| Speaker Connecting | Wi-Fi/TCP setup |
| Speaker Streaming | active system-audio mirror |
| Speaker Recovering | Windows audio endpoint changed |

---

## Still stuck?

Collect:

- exact Companion status text
- whether Mouse/HID still works
- whether BLE reaches Ready
- current Speaker Wi-Fi state
- whether the problem survives a Cube power-cycle
- whether it survives a Companion restart

That narrows the failure to a specific layer much faster than rebuilding everything.
