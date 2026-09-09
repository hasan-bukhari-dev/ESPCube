#include "ESPCubeSpeech.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_check.h>

#include "es7210.h"
#include "../../../include/espcube_secrets.h"

// Official Waveshare ESP32-S3-Touch-LCD-1.54 speech example.
static constexpr int I2S_PIN_MCK  = 8;
static constexpr int I2S_PIN_BCK  = 9;
static constexpr int I2S_PIN_WS   = 10;
static constexpr int I2S_PIN_DIN  = 11;
static constexpr int I2S_PIN_DOUT = 12;

static constexpr uint8_t ES7210_ADDR = 0x40;

static es7210_dev_handle_t es7210Handle = nullptr;

static bool initEs7210()
{
    es7210_i2c_config_t i2cConfig = {};
    i2cConfig.i2c_addr = ES7210_ADDR;

    if (
        es7210_new_codec(
            &i2cConfig,
            &es7210Handle
        ) != ESP_OK
    )
    {
        Serial.println("[MIC] ES7210 create failed.");
        return false;
    }

    es7210_codec_config_t codec = {};

    codec.i2s_format = ES7210_I2S_FMT_I2S;
    codec.mclk_ratio = 256;
    codec.sample_rate_hz = 16000;
    codec.bit_width = ES7210_I2S_BITS_16B;
    codec.mic_bias = ES7210_MIC_BIAS_2V87;
    codec.mic_gain = ES7210_MIC_GAIN_30DB;
    codec.flags.tdm_enable = false;

    if (
        es7210_config_codec(
            es7210Handle,
            &codec
        ) != ESP_OK
    )
    {
        Serial.println("[MIC] ES7210 config failed.");
        return false;
    }

    if (
        es7210_config_volume(
            es7210Handle,
            24
        ) != ESP_OK
    )
    {
        Serial.println("[MIC] ES7210 volume failed.");
        return false;
    }

    return true;
}

bool ESPCubeSpeech::begin()
{
    // DO NOT call Wire.begin() here.
    // ESPCube already owns SDA42/SCL41 for QMI8658 + CST816S.

    if (!initEs7210())
        return false;

    _i2s.setPins(
        I2S_PIN_BCK,
        I2S_PIN_WS,
        I2S_PIN_DOUT,
        I2S_PIN_DIN,
        I2S_PIN_MCK
    );

    _i2s.setTimeout(20);

    if (
        !_i2s.begin(
            I2S_MODE_STD,
            SAMPLE_RATE,
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        )
    )
    {
        Serial.println("[MIC] I2S begin failed.");
        return false;
    }

    _ready = true;

    Serial.println("[MIC] ES7210/I2S READY 16kHz.");
    return true;
}
// ============================================================
// S1-B1 SPEAKER MODE
//
// ESPCubeSpeech owns every I2S teardown/reconfigure/restore.
// No other module directly calls _i2s.end()/begin().
// ============================================================

bool ESPCubeSpeech::enterSpeakerPlaybackMode(uint32_t sampleRate)
{
    if (_recording || sampleRate != 32000)
        return false;

    _ready = false;
    _speakerMode = false;

    _i2s.end();
    delay(5);

    _i2s.setPins(
        I2S_PIN_BCK,
        I2S_PIN_WS,
        I2S_PIN_DOUT,
        I2S_PIN_DIN,
        I2S_PIN_MCK
    );

    _i2s.setTimeout(20);

    if (
        !_i2s.begin(
            I2S_MODE_STD,
            sampleRate,
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        )
    )
    {
        return false;
    }

    _speakerMode = true;
    return true;
}

bool ESPCubeSpeech::restoreSpeechMode()
{
    if (_recording)
        return false;

    _ready = false;
    _speakerMode = false;

    _i2s.end();
    delay(5);

    // Re-run the same known-good ES7210 configuration used by begin().
    if (!initEs7210())
        return false;

    _i2s.setPins(
        I2S_PIN_BCK,
        I2S_PIN_WS,
        I2S_PIN_DOUT,
        I2S_PIN_DIN,
        I2S_PIN_MCK
    );

    _i2s.setTimeout(20);

    if (
        !_i2s.begin(
            I2S_MODE_STD,
            SAMPLE_RATE,
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        )
    )
    {
        Serial.println("[S1-B1] 16kHz speech I2S restore failed.");
        return false;
    }

    _ready = true;
    Serial.println("[S1-B1] 16kHz speech mode restored.");
    return true;
}

size_t ESPCubeSpeech::writeSpeakerPcm(
    const int16_t *samples,
    size_t sampleCount
)
{
    if (
        !_speakerMode ||
        _recording ||
        !samples ||
        sampleCount == 0
    )
    {
        return 0;
    }

    return _i2s.write(
        (const void *)samples,
        sampleCount * sizeof(int16_t)
    );
}

bool ESPCubeSpeech::startRecording()
{
    if (!_ready)
    {
        Serial.println(
            "[MIC] Not ready - retrying ES7210/I2S init..."
        );

        delay(100);

        if (!begin())
        {
            Serial.println(
                "[MIC] RETRY INIT FAILED"
            );

            return false;
        }

        Serial.println(
            "[MIC] RETRY INIT SUCCESS"
        );
    }

    if (_audio)
    {
        free(_audio);
        _audio = nullptr;
    }

    _audio =
        (int16_t *)heap_caps_malloc(
            MAX_SAMPLES * sizeof(int16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

    if (!_audio)
    {
        Serial.println("[MIC] PSRAM allocation failed.");
        return false;
    }

    _samples = 0;
    _recording = true;

    // Discard stale DMA audio.
    uint8_t junk[256];

    for (int i = 0; i < 3; i++)
    {
        _i2s.readBytes(
            (char *)junk,
            sizeof(junk)
        );
    }

    Serial.println("[MIC] RECORDING");
    return true;
}

size_t ESPCubeSpeech::captureMono(
    int16_t *dst,
    size_t capacity
)
{
    if (
        !_recording ||
        !_audio
    )
        return 0;

    int16_t stereo[256];

    size_t bytes =
        _i2s.readBytes(
            (char *)stereo,
            sizeof(stereo)
        );

    size_t stereoSamples =
        bytes / sizeof(int16_t);

    size_t frames =
        stereoSamples / 2;

    size_t produced =
        0;

    for (
        size_t i = 0;
        i < frames &&
        _samples < MAX_SAMPLES;
        i++
    )
    {
        int32_t a =
            stereo[i * 2];

        int32_t b =
            stereo[i * 2 + 1];

        int16_t mono =
            (int16_t)((a + b) / 2);

        _audio[_samples++] =
            mono;

        if (
            dst &&
            produced < capacity
        )
        {
            dst[produced++] =
                mono;
        }
    }

    if (_samples >= MAX_SAMPLES)
    {
        _recording = false;

        Serial.println(
            "[MIC] 15 second maximum reached."
        );
    }

    return produced;
}

void ESPCubeSpeech::captureTick()
{
    captureMono(
        nullptr,
        0
    );
}

void ESPCubeSpeech::stopRecording()
{
    if (!_recording)
        return;

    captureTick();

    _recording = false;

    Serial.printf(
        "[MIC] STOP samples=%u seconds=%.2f\n",
        (unsigned)_samples,
        (double)_samples /
            (double)SAMPLE_RATE
    );
}

bool ESPCubeSpeech::isRecording() const
{
    return _recording;
}

size_t ESPCubeSpeech::sampleCount() const
{
    return _samples;
}

const int16_t *ESPCubeSpeech::audioData() const
{
    return _audio;
}

bool ESPCubeSpeech::connectWifi()
{
    if (WiFi.status() == WL_CONNECTED)
        return true;

    Serial.print("[STT] Wi-Fi -> ");
    Serial.println(ESPCUBE_WIFI_SSID);

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        ESPCUBE_WIFI_SSID,
        ESPCUBE_WIFI_PASSWORD
    );

    uint32_t started =
        millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - started < 15000
    )
    {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[STT] Wi-Fi failed.");
        return false;
    }

    Serial.print("[STT] Wi-Fi connected: ");
    Serial.println(WiFi.localIP());

    return true;
}

void ESPCubeSpeech::disconnectWifi()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

static void put16(
    uint8_t *p,
    uint16_t v
)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(
    uint8_t *p,
    uint32_t v
)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void ESPCubeSpeech::writeWavHeader(
    uint8_t *h,
    uint32_t pcmBytes
)
{
    memcpy(h + 0, "RIFF", 4);
    put32(h + 4, 36 + pcmBytes);
    memcpy(h + 8, "WAVE", 4);

    memcpy(h + 12, "fmt ", 4);
    put32(h + 16, 16);
    put16(h + 20, 1);  // PCM
    put16(h + 22, 1);  // mono
    put32(h + 24, SAMPLE_RATE);
    put32(h + 28, SAMPLE_RATE * 2);
    put16(h + 32, 2);
    put16(h + 34, 16);

    memcpy(h + 36, "data", 4);
    put32(h + 40, pcmBytes);
}

String ESPCubeSpeech::transcribe()
{
    // Ignore accidental ultra-short captures.
    if (
        !_audio ||
        _samples < 1600
    )
    {
        Serial.println("[STT] Recording too short.");
        return "";
    }

    if (!connectWifi())
        return "";

    const String boundary =
        "----ESPCubeSpeechBoundary";

    String prefix;

    prefix += "--" + boundary + "\r\n";
    prefix +=
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
    prefix +=
        "whisper-large-v3-turbo\r\n";

    prefix += "--" + boundary + "\r\n";
    prefix +=
        "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n";
    prefix +=
        "json\r\n";

    prefix += "--" + boundary + "\r\n";
    prefix +=
        "Content-Disposition: form-data; name=\"file\"; filename=\"espcube.wav\"\r\n";
    prefix +=
        "Content-Type: audio/wav\r\n\r\n";

    String suffix =
        "\r\n--" +
        boundary +
        "--\r\n";

    size_t pcmBytes =
        _samples *
        sizeof(int16_t);

    size_t wavBytes =
        44 +
        pcmBytes;

    size_t bodyBytes =
        prefix.length() +
        wavBytes +
        suffix.length();

    uint8_t *body =
        (uint8_t *)heap_caps_malloc(
            bodyBytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

    if (!body)
    {
        Serial.println("[STT] Request allocation failed.");
        disconnectWifi();
        return "";
    }

    size_t pos = 0;

    memcpy(
        body + pos,
        prefix.c_str(),
        prefix.length()
    );

    pos += prefix.length();

    writeWavHeader(
        body + pos,
        (uint32_t)pcmBytes
    );

    pos += 44;

    memcpy(
        body + pos,
        _audio,
        pcmBytes
    );

    pos += pcmBytes;

    memcpy(
        body + pos,
        suffix.c_str(),
        suffix.length()
    );

    WiFiClientSecure client;

    // Encrypted HTTPS MVP. Production version should pin a CA.
    client.setInsecure();

    HTTPClient http;

    if (
        !http.begin(
            client,
            "https://api.groq.com/openai/v1/audio/transcriptions"
        )
    )
    {
        free(body);
        disconnectWifi();

        Serial.println("[STT] HTTPS begin failed.");
        return "";
    }

    http.setTimeout(30000);

    http.addHeader(
        "Authorization",
        String("Bearer ") +
        ESPCUBE_GROQ_API_KEY
    );

    http.addHeader(
        "Content-Type",
        "multipart/form-data; boundary=" +
        boundary
    );

    Serial.printf(
        "[STT] Sending %.2f sec (%.1f KB WAV)\n",
        (double)_samples /
            (double)SAMPLE_RATE,
        (double)wavBytes /
            1024.0
    );

    int status =
        http.POST(
            body,
            bodyBytes
        );

    free(body);

    String response =
        http.getString();

    http.end();
    disconnectWifi();

    if (
        status < 200 ||
        status >= 300
    )
    {
        Serial.printf(
            "[STT] HTTP %d\n",
            status
        );

        Serial.println(response);
        return "";
    }

    JsonDocument doc;

    DeserializationError error =
        deserializeJson(
            doc,
            response
        );

    if (error)
    {
        Serial.print("[STT] JSON error: ");
        Serial.println(error.c_str());

        return "";
    }

    String result =
        doc["text"] |
        "";

    result.trim();

    Serial.print("[STT] Transcript: ");
    Serial.println(result);

    return result;
}