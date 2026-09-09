#include "SpeakerStream.h"
#include "../app/RuntimeGlobals.h"
#include "../ui/ScreenRenderer.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/tcp.h>
#include <fcntl.h>
#include <errno.h>

SpeakerStream speakerStream;

void SpeakerStream::closeSpeakerTcpClient(
    bool sendResult
)
{
    bool restoreOk = true;

    if (speakerPlayback.active())
    {
        restoreOk =
            speakerPlayback.end();
    }

    if (speakerTcpClient >= 0)
    {
        if (sendResult)
        {
            char result[96];

            snprintf(
                result,
                sizeof(result),
                "DONE bytes=%lu chunks=%lu restore=%u\n",
                (unsigned long)speakerTcpBytesPlayed,
                (unsigned long)speakerTcpChunksPlayed,
                restoreOk ? 1U : 0U
            );

            ::send(
                speakerTcpClient,
                result,
                strlen(result),
                0
            );

            // Allow the final stream status response a moment to leave
            // before the socket is torn down.
            delay(5);
        }

        ::shutdown(
            speakerTcpClient,
            SHUT_RDWR
        );

        ::close(
            speakerTcpClient
        );

        speakerTcpClient =
            -1;
    }

    speakerTcpRxFill =
        0;

    speakerTcpBytesPlayed =
        0;

    speakerTcpChunksPlayed =
        0;
}

void SpeakerStream::stopSpeakerTcpServer()
{
    closeSpeakerTcpClient(
        false
    );

    if (speakerTcpServer >= 0)
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;
    }

    speakerTcpListening =
        false;
}

void SpeakerStream::stopPlayback()
{
    stopSpeakerTcpServer();
}

bool SpeakerStream::startSpeakerTcpServer()
{
    if (speakerTcpListening)
        return true;

    speakerTcpServer =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );

    if (speakerTcpServer < 0)
    {
        speakerTcpServer = -1;
        return false;
    }

    const int reuse = 1;

    setsockopt(
        speakerTcpServer,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );

    const int flags =
        fcntl(
            speakerTcpServer,
            F_GETFL,
            0
        );

    if (
        flags < 0 ||
        fcntl(
            speakerTcpServer,
            F_SETFL,
            flags | O_NONBLOCK
        ) != 0
    )
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;

        return false;
    }

    struct sockaddr_in localAddress;

    memset(
        &localAddress,
        0,
        sizeof(localAddress)
    );

    localAddress.sin_family =
        AF_INET;

    localAddress.sin_addr.s_addr =
        htonl(INADDR_ANY);

    localAddress.sin_port =
        htons(TcpPort);

    if (
        ::bind(
            speakerTcpServer,
            reinterpret_cast<struct sockaddr *>(
                &localAddress
            ),
            sizeof(localAddress)
        ) != 0
    )
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;

        return false;
    }

    if (
        ::listen(
            speakerTcpServer,
            1
        ) != 0
    )
    {
        ::close(
            speakerTcpServer
        );

        speakerTcpServer =
            -1;

        return false;
    }

    speakerTcpListening =
        true;

    return true;
}

void SpeakerStream::acceptSpeakerTcpClient()
{
    if (
        !speakerTcpListening ||
        speakerTcpServer < 0 ||
        speakerTcpClient >= 0
    )
    {
        return;
    }

    struct sockaddr_in remoteAddress;

    socklen_t remoteLength =
        sizeof(remoteAddress);

    const int client =
        ::accept(
            speakerTcpServer,
            reinterpret_cast<struct sockaddr *>(
                &remoteAddress
            ),
            &remoteLength
        );

    if (client < 0)
        return;

    const int one = 1;

    setsockopt(
        client,
        IPPROTO_TCP,
        TCP_NODELAY,
        &one,
        sizeof(one)
    );

    const int flags =
        fcntl(
            client,
            F_GETFL,
            0
        );

    if (
        flags < 0 ||
        fcntl(
            client,
            F_SETFL,
            flags | O_NONBLOCK
        ) != 0
    )
    {
        ::close(client);
        return;
    }

    speakerTcpClient =
        client;

    speakerTcpRxFill =
        0;

    speakerTcpBytesPlayed =
        0;

    speakerTcpChunksPlayed =
        0;
}

bool SpeakerStream::playSpeakerTcpChunk()
{
    int16_t mono[
        PcmFrames
    ];

    for (
        size_t i = 0;
        i < PcmFrames;
        ++i
    )
    {
        const size_t offset =
            i * 2;

        const uint16_t raw =
            static_cast<uint16_t>(
                speakerTcpRx[offset]
            ) |
            (
                static_cast<uint16_t>(
                    speakerTcpRx[offset + 1]
                )
                << 8
            );

        mono[i] =
            static_cast<int16_t>(
                raw
            );
    }

    if (!speakerPlayback.active())
    {
        if (
            !speakerPlayback.begin(
                speech
            )
        )
        {
            return false;
        }
    }

    const size_t written =
        speakerPlayback.writeMono(
            mono,
            PcmFrames
        );

    if (
        written !=
        PcmFrames
    )
    {
        return false;
    }

    speakerTcpBytesPlayed +=
        PcmBytes;

    ++speakerTcpChunksPlayed;

    speakerTcpRxFill =
        0;

    return true;
}

void SpeakerStream::receiveSpeakerTcpAudio()
{
    if (speakerTcpClient < 0)
        return;

    // At most one 10 ms playback chunk per main-loop pass.
    // This deliberately keeps HOME/touch/buttons responsive.
    while (
        speakerTcpRxFill <
        PcmBytes
    )
    {
        const int received =
            ::recv(
                speakerTcpClient,
                speakerTcpRx +
                    speakerTcpRxFill,
                PcmBytes -
                    speakerTcpRxFill,
                0
            );

        if (received > 0)
        {
            speakerTcpRxFill +=
                static_cast<size_t>(
                    received
                );

            continue;
        }

        if (received == 0)
        {
            // PC completed the stream using SHUT_WR.
            // End playback, restore speech16, and return final stream status.
            closeSpeakerTcpClient(
                true
            );

            return;
        }

        if (
            errno == EWOULDBLOCK ||
            errno == EAGAIN
        )
        {
            return;
        }

        closeSpeakerTcpClient(
            false
        );

        return;
    }

    if (
        speakerTcpRxFill ==
        PcmBytes
    )
    {
        if (!playSpeakerTcpChunk())
        {
            closeSpeakerTcpClient(
                false
            );
        }
    }
}

void SpeakerStream::serviceTcp()
{
    const bool shouldRun =
        profiles.profile ==
            Profile::SPEAKER &&
        speakerControl.isReady() &&
        WiFi.status() ==
            WL_CONNECTED;

    if (!shouldRun)
    {
        if (
            speakerTcpListening ||
            speakerTcpClient >= 0 ||
            speakerPlayback.active()
        )
        {
            stopSpeakerTcpServer();
        }

        return;
    }

    if (!speakerTcpListening)
    {
        if (!startSpeakerTcpServer())
            return;
    }

    acceptSpeakerTcpClient();

    receiveSpeakerTcpAudio();
}

// ============================================================
// EXISTING SPEAKER CONTROL
// ============================================================
void SpeakerStream::serviceControl()
{
    const SpeakerCommand command =
        speakerControl.consumeCommand();

    if (command == SpeakerCommand::Enter)
    {
        if (speakerProfile.enter())
        {
            renderer.renderCurrentScreen();
        }
        else
        {
            // speakerProfile.enter() preserves the original failed-enter
            // exitSafe() behavior before returning false. goHome() then
            // performs the same universal recovery boundary as before.
            profiles.goHome();
        }
    }
    else if (command == SpeakerCommand::Exit)
    {
        speakerProfile.exit();

        if (profiles.profile == Profile::SPEAKER)
        {
            profiles.goHome();
        }
    }
    else if (command == SpeakerCommand::Status)
    {
        speakerControl.publishStatus();
    }

    if (
        profiles.profile != Profile::SPEAKER &&
        speakerControl.state() != SpeakerState::Off
    )
    {
        speakerProfile.exit();
    }
}

// ============================================================
// LOOP
// ============================================================


// ============================================================
// BUFFERED TCP PCM SERVICE
// ============================================================

void SpeakerStream::resetSpeakerD2ASession()
{
    speakerPcmBuffer.clear();

    d2aTcpBytesRx = 0;
    d2aPcmBytesPlayed = 0;
    d2aChunksPlayed = 0;
    d2aBufferUnderruns = 0;
    d2aBufferOverflows = 0;

    d2aInputEnded = false;
    d2aPlaybackStarted = false;
    d2aUnderrunLatched = false;

    d2aLastWriteMs = millis();
}

bool SpeakerStream::ensureSpeakerD2ABuffer()
{
    if (speakerPcmBuffer.ready()) {
        return true;
    }

    return speakerPcmBuffer.begin(
        BufferCapacity
    );
}

void SpeakerStream::closeSpeakerD2AClientOnly()
{
    if (speakerTcpClient >= 0) {
        ::shutdown(
            speakerTcpClient,
            SHUT_RDWR
        );

        ::close(
            speakerTcpClient
        );

        speakerTcpClient = -1;
    }

    speakerTcpRxFill = 0;
}

void SpeakerStream::finishSpeakerD2AStream()
{
    const bool restoreOk =
        speakerPlayback.end();

    char response[220];

    snprintf(
        response,
        sizeof(response),
        "DONE rx=%lu played=%lu chunks=%lu underruns=%lu overflows=%lu highwater=%u restore=%u\n",
        static_cast<unsigned long>(
            d2aTcpBytesRx
        ),
        static_cast<unsigned long>(
            d2aPcmBytesPlayed
        ),
        static_cast<unsigned long>(
            d2aChunksPlayed
        ),
        static_cast<unsigned long>(
            d2aBufferUnderruns
        ),
        static_cast<unsigned long>(
            d2aBufferOverflows
        ),
        static_cast<unsigned>(
            speakerPcmBuffer.highWater()
        ),
        restoreOk ? 1U : 0U
    );

    if (speakerTcpClient >= 0) {
        ::send(
            speakerTcpClient,
            response,
            strlen(response),
            0
        );
    }

    closeSpeakerD2AClientOnly();

    speakerPcmBuffer.clear();

    d2aInputEnded = false;
    d2aPlaybackStarted = false;
    d2aUnderrunLatched = false;
}

bool SpeakerStream::acceptSpeakerD2AClient()
{
    if (
        speakerTcpClient >= 0 ||
        speakerTcpServer < 0
    ) {
        return speakerTcpClient >= 0;
    }

    struct sockaddr_in remoteAddress;

    socklen_t remoteLength =
        sizeof(remoteAddress);

    const int client =
        ::accept(
            speakerTcpServer,
            reinterpret_cast<
                struct sockaddr *
            >(
                &remoteAddress
            ),
            &remoteLength
        );

    if (client < 0) {
        if (
            errno == EWOULDBLOCK ||
            errno == EAGAIN
        ) {
            return false;
        }

        return false;
    }

    const int one = 1;

    setsockopt(
        client,
        IPPROTO_TCP,
        TCP_NODELAY,
        &one,
        sizeof(one)
    );

    const int flags =
        fcntl(
            client,
            F_GETFL,
            0
        );

    if (flags >= 0) {
        fcntl(
            client,
            F_SETFL,
            flags | O_NONBLOCK
        );
    }

    speakerTcpClient =
        client;

    resetSpeakerD2ASession();

    return true;
}

void SpeakerStream::drainSpeakerD2ATcpIntoRing()
{
    if (
        speakerTcpClient < 0 ||
        d2aInputEnded
    ) {
        return;
    }

    uint8_t rx[2048];

    while (speakerTcpClient >= 0) {
        const size_t freeBytes =
            speakerPcmBuffer.freeBytes();

        if (freeBytes == 0) {
            // Do not read more from TCP while the PSRAM
            // queue is full. TCP backpressure is preferable
            // to dropping PCM.
            return;
        }

        const size_t request =
            min(
                sizeof(rx),
                freeBytes
            );

        const int received =
            ::recv(
                speakerTcpClient,
                rx,
                request,
                0
            );

        if (received > 0) {
            const size_t accepted =
                speakerPcmBuffer.push(
                    rx,
                    static_cast<size_t>(
                        received
                    )
                );

            d2aTcpBytesRx +=
                static_cast<uint32_t>(
                    accepted
                );

            if (
                accepted !=
                static_cast<size_t>(
                    received
                )
            ) {
                ++d2aBufferOverflows;
                return;
            }

            continue;
        }

        if (received == 0) {
            d2aInputEnded = true;
            return;
        }

        if (
            errno == EWOULDBLOCK ||
            errno == EAGAIN
        ) {
            return;
        }

        // Any other socket error ends this client safely.
        d2aInputEnded = true;
        return;
    }
}

bool SpeakerStream::maybeStartSpeakerD2APlayback()
{
    if (d2aPlaybackStarted) {
        return true;
    }

    if (
        speakerPcmBuffer.size() <
            PrebufferBytes &&
        !d2aInputEnded
    ) {
        return false;
    }

    if (
        speakerPcmBuffer.size() <
            PcmBytes
    ) {
        return false;
    }

    if (!speakerPlayback.begin(speech)) {
        return false;
    }

    d2aPlaybackStarted = true;
    d2aLastWriteMs = millis();
    d2aUnderrunLatched = false;

    return true;
}

void SpeakerStream::playOneSpeakerD2AChunk()
{
    if (!d2aPlaybackStarted) {
        return;
    }

    if (
        speakerPcmBuffer.size() <
        PcmBytes
    ) {
        if (
            !d2aInputEnded &&
            !d2aUnderrunLatched &&
            millis() - d2aLastWriteMs >=
                UnderrunGraceMs
        ) {
            ++d2aBufferUnderruns;
            d2aUnderrunLatched = true;
        }

        return;
    }

    uint8_t raw[PcmBytes];

    const size_t popped =
        speakerPcmBuffer.pop(
            raw,
            sizeof(raw)
        );

    if (popped != sizeof(raw)) {
        ++d2aBufferUnderruns;
        d2aUnderrunLatched = true;
        return;
    }

    int16_t mono[PcmFrames];

    for (
        size_t i = 0;
        i < PcmFrames;
        ++i
    ) {
        mono[i] =
            static_cast<int16_t>(
                static_cast<uint16_t>(
                    raw[i * 2]
                ) |
                (
                    static_cast<uint16_t>(
                        raw[i * 2 + 1]
                    )
                    << 8
                )
            );
    }

    // Apply attenuation only; never amplify above the calibrated maximum.
    speakerVolume.apply(
        mono,
        PcmFrames
    );

    const size_t consumed =
        speakerPlayback.writeMono(
            mono,
            PcmFrames
        );

    if (
        consumed !=
        PcmFrames
    ) {
        ++d2aBufferUnderruns;
        d2aInputEnded = true;
        return;
    }

    d2aPcmBytesPlayed +=
        PcmBytes;

    ++d2aChunksPlayed;

    d2aLastWriteMs =
        millis();

    d2aUnderrunLatched =
        false;
}

void SpeakerStream::serviceD2A()
{
    const bool shouldRun =
        profiles.profile ==
            Profile::SPEAKER &&
        speakerControl.isReady() &&
        WiFi.status() ==
            WL_CONNECTED;

    if (!shouldRun) {
        if (
            speakerTcpClient >= 0 ||
            speakerTcpServer >= 0 ||
            speakerPlayback.active()
        ) {
            stopPlayback();
        }

        if (speakerPcmBuffer.ready()) {
            speakerPcmBuffer.clear();
        }

        d2aInputEnded = false;
        d2aPlaybackStarted = false;
        d2aUnderrunLatched = false;

        return;
    }

    if (!ensureSpeakerD2ABuffer()) {
        stopPlayback();
        return;
    }

    if (!speakerTcpListening) {
        startSpeakerTcpServer();
        return;
    }

    if (speakerTcpClient < 0) {
        acceptSpeakerD2AClient();
        return;
    }

    // Drain every TCP byte immediately available into PSRAM.
    drainSpeakerD2ATcpIntoRing();

    // Do not touch I2S until at least 120 ms has been buffered,
    // except at EOF for intentionally short streams.
    maybeStartSpeakerD2APlayback();

    // writeMono() / I2S provides the natural audio pacing.
    // Exactly one 10 ms PCM chunk is consumed per service pass.
    playOneSpeakerD2AChunk();

    if (
        d2aInputEnded &&
        speakerPcmBuffer.size() == 0
    ) {
        finishSpeakerD2AStream();
    }
}
