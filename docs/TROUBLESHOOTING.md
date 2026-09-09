# Troubleshooting

## Firmware does not upload
- verify the Cube is connected with a USB data cable
- check Device Manager for the active COM port
- retry with `--upload-port COMxx`

## Companion cannot find the Cube
- verify Bluetooth is enabled
- verify the Cube is powered and advertising
- restart the Companion

## Speech is unavailable
- verify `companion/models/ggml-tiny.en.bin` exists for source builds
- release-installer users already receive the model automatically

## Speaker does not start
- verify the PC has an active default audio output
- verify the Cube is in the Speaker profile
- verify the PC is connected to a usable Wi-Fi network
- return HOME and reopen Speaker if the transport needs to recover
