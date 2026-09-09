# Protocol

## Speech BLE service

Service:
`45535043-5542-4c45-8000-000000000001`

Characteristics:
- CONTROL: `...0002`
- STATUS: `...0003`
- AUDIO: `...0004`

Speech audio uses the proven IMA ADPCM transport used by ESPCube v1.

## Speaker BLE service

Service:
`45535043-5542-4c45-8100-000000000001`

Characteristics:
- COMMAND: `...0002`
- STATUS: `...0003`

Wi-Fi is used only for the temporary high-bandwidth Speaker path.

## Speaker audio transport

TCP port: `47821`

PCM format:
- 32 kHz
- mono
- signed PCM16 little-endian

Bluetooth remains the primary control/presence path.
