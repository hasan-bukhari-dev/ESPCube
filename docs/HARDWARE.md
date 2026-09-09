# Hardware

Target: Waveshare ESP32-S3-Touch-LCD-1.54

Core hardware used by ESPCube v1:
- ESP32-S3R8
- 8 MB PSRAM
- 16 MB flash
- 240x240 ST7789 display
- CST816S touch
- QMI8658 6-axis IMU
- ES7210 microphone ADC
- ES8311 audio codec
- NS4150B amplifier

Important pins:
- I2C SDA: GPIO42
- I2C SCL: GPIO41
- Touch RST: GPIO47
- Touch INT: GPIO48
- LCD CS: GPIO21
- LCD CLK: GPIO38
- LCD MOSI: GPIO39
- LCD RST: GPIO40
- LCD DC: GPIO45
- LCD BL: GPIO46
- Audio MCLK: GPIO8
- Audio BCLK: GPIO9
- Audio WS: GPIO10
- Audio DIN: GPIO11
- Audio DOUT: GPIO12
- PA control: GPIO7

The firmware owns the shared I2C bus with `Wire.begin(42, 41)`.
