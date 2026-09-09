use std::io::Write;
use std::net::{IpAddr, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, mpsc};
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, bail};
use btleplug::api::{Characteristic, Peripheral as _, WriteType};
use btleplug::platform::Peripheral;
use parking_lot::RwLock;
use serde_json::json;

use crate::audio::{self, AudioMessage};
use crate::dsp::SpeakerDsp;
use crate::logging;
use crate::state::SharedState;
use crate::wifi;

const TCP_PORT: u16 = 47821;
const OUT_RATE: usize = 32_000;
const CHUNK_SAMPLES: usize = 320;
const CHUNK_BYTES: usize = CHUNK_SAMPLES * 2;

pub struct SpeakerWorker {
    pub stop: Arc<AtomicBool>,
    pub running: Arc<AtomicBool>,
}

pub fn spawn(
    peripheral: Peripheral,
    command: Characteristic,
    status: Characteristic,
    shared: Arc<RwLock<SharedState>>,
) -> SpeakerWorker {
    let stop = Arc::new(AtomicBool::new(false));
    let running = Arc::new(AtomicBool::new(true));

    let stop_thread = Arc::clone(&stop);
    let running_thread = Arc::clone(&running);

    let _ = thread::Builder::new()
        .name("espcube-speaker".to_string())
        .spawn(move || {
            let runtime = match tokio::runtime::Runtime::new() {
                Ok(v) => v,
                Err(err) => {
                    let mut s = shared.write();
                    s.speaker_status = "Error".to_string();
                    s.speaker_detail = format!("Speaker runtime failed: {err}");

                    running_thread.store(false, Ordering::SeqCst);
                    return;
                }
            };

            let result = runtime.block_on(run(
                peripheral,
                command,
                status,
                Arc::clone(&shared),
                Arc::clone(&stop_thread),
            ));

            if let Err(err) = result {
                if !stop_thread.load(Ordering::SeqCst) {
                    logging::write(&format!("[SPEAKER] {err:#}"));

                    let mut s = shared.write();
                    s.speaker_status = "Error".to_string();
                    s.speaker_detail = err.to_string();
                }
            }

            running_thread.store(false, Ordering::SeqCst);
        });

    SpeakerWorker { stop, running }
}

async fn run(
    peripheral: Peripheral,
    command: Characteristic,
    status: Characteristic,
    shared: Arc<RwLock<SharedState>>,
    stop: Arc<AtomicBool>,
) -> Result<()> {
    if status_field(&read_status(&peripheral, &status).await?, "state") != Some("READY") {
        bail!("Speaker profile is not READY.");
    }

    let network = wifi::load_trusted_current_network()?;

    {
        let mut s = shared.write();
        s.speaker_status = "Connecting".to_string();
        s.speaker_detail = "Starting Speaker".to_string();
        s.wifi_ssid = Some(network.ssid.clone());
    }

    // Fast start:
    // Reuse the Cube's existing Wi-Fi session whenever possible.
    let current = read_status(&peripheral, &status).await?;

    let ip = match status_field(&current, "wifi_state") {
        Some("CONNECTED") => {
            let ip_text = status_field(&current, "ip")
                .ok_or_else(|| anyhow::anyhow!("Cube reports CONNECTED Wi-Fi but no IP"))?;

            {
                let mut s = shared.write();
                s.speaker_detail = "Wi-Fi ready - connecting audio".to_string();
            }

            ip_text.parse::<IpAddr>()?
        }

        Some("CONFIGURED") => {
            {
                let mut s = shared.write();
                s.speaker_detail = "Connecting saved Wi-Fi".to_string();
            }

            connect_wifi_and_wait(&peripheral, &command, &status, &stop).await?
        }

        _ => {
            {
                let mut s = shared.write();
                s.speaker_detail = "Preparing trusted Wi-Fi".to_string();
            }

            provision(
                &peripheral,
                &command,
                &status,
                &network.ssid,
                &network.password,
            )
            .await?;

            connect_wifi_and_wait(&peripheral, &command, &status, &stop).await?
        }
    };

    let mut stream = connect_tcp_retry(ip, &stop).await?;

    let (tx, rx) = mpsc::sync_channel::<AudioMessage>(8);

    let mut capture = audio::start_system_loopback(tx.clone())?;

    {
        let mut s = shared.write();
        s.speaker_status = "Streaming".to_string();
        s.speaker_detail = "D2C9 â€¢ system audio mirror".to_string();
        s.audio_device = Some(capture.device_name.clone());
    }

    let mut dsp = SpeakerDsp::new(capture.sample_rate as usize, OUT_RATE);

    let mut pending: Vec<i16> = Vec::with_capacity(CHUNK_SAMPLES * 4);

    let mut last_status_check = Instant::now();

    loop {
        if stop.load(Ordering::SeqCst) {
            break;
        }

        if last_status_check.elapsed() >= Duration::from_millis(450) {
            last_status_check = Instant::now();

            match read_status(&peripheral, &status).await {
                Ok(text) => {
                    if status_field(&text, "state") != Some("READY") {
                        break;
                    }
                }
                Err(_) => {
                    break;
                }
            }
        }

        let message = match rx.recv_timeout(Duration::from_millis(100)) {
            Ok(v) => v,
            Err(mpsc::RecvTimeoutError::Timeout) => continue,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        };

        match message {
            AudioMessage::Error(err) => {
                logging::write(&format!("[WASAPI] endpoint changed: {err}"));

                {
                    let mut s = shared.write();
                    s.speaker_status = "Recovering".to_string();
                    s.speaker_detail = "Windows audio device changed".to_string();
                    s.audio_device = None;
                }

                drop(capture);

                while rx.try_recv().is_ok() {}

                capture = loop {
                    if stop.load(Ordering::SeqCst) {
                        return Ok(());
                    }

                    let current = read_status(&peripheral, &status).await?;

                    if status_field(&current, "state") != Some("READY") {
                        return Ok(());
                    }

                    match audio::start_system_loopback(tx.clone()) {
                        Ok(v) => break v,
                        Err(_) => {
                            tokio::time::sleep(Duration::from_secs(1)).await;
                        }
                    }
                };

                dsp = SpeakerDsp::new(capture.sample_rate as usize, OUT_RATE);

                {
                    let mut s = shared.write();
                    s.speaker_status = "Streaming".to_string();
                    s.speaker_detail = "D2C9 â€¢ system audio mirror".to_string();
                    s.audio_device = Some(capture.device_name.clone());
                }
            }

            AudioMessage::Block(block) => {
                let mono = block.to_mono_f32();

                let out = dsp.process(&mono, block.sample_rate as usize);

                pending.extend(out);

                while pending.len() >= CHUNK_SAMPLES {
                    let chunk: Vec<i16> = pending.drain(..CHUNK_SAMPLES).collect();

                    let mut bytes = [0u8; CHUNK_BYTES];

                    for (i, sample) in chunk.iter().enumerate() {
                        let b = sample.to_le_bytes();
                        bytes[i * 2] = b[0];
                        bytes[i * 2 + 1] = b[1];
                    }

                    if let Err(err) = stream.write_all(&bytes) {
                        logging::write(&format!("[TCP] write failed: {err}"));

                        let current = read_status(&peripheral, &status).await?;

                        if status_field(&current, "state") == Some("READY")
                            && !stop.load(Ordering::SeqCst)
                        {
                            let ip = if status_field(&current, "wifi_state") == Some("CONNECTED") {
                                status_field(&current, "ip")
                                    .ok_or_else(|| anyhow::anyhow!("Connected Wi-Fi has no IP"))?
                                    .parse::<IpAddr>()?
                            } else {
                                provision(
                                    &peripheral,
                                    &command,
                                    &status,
                                    &network.ssid,
                                    &network.password,
                                )
                                .await?;

                                connect_wifi_and_wait(&peripheral, &command, &status, &stop).await?
                            };

                            stream = connect_tcp_retry(ip, &stop).await?;

                            pending.clear();

                            while rx.try_recv().is_ok() {}

                            dsp = SpeakerDsp::new(block.sample_rate as usize, OUT_RATE);

                            break;
                        } else {
                            return Ok(());
                        }
                    }
                }
            }
        }
    }

    drop(capture);

    {
        let mut s = shared.write();
        s.speaker_status = "Idle".to_string();
        s.speaker_detail = "Waiting for Speaker profile".to_string();
        s.audio_device = None;
        s.wifi_ssid = None;
    }

    Ok(())
}

async fn provision(
    peripheral: &Peripheral,
    command: &Characteristic,
    status: &Characteristic,
    ssid: &str,
    password: &str,
) -> Result<()> {
    let payload = json!({
        "cmd": "wifi_set",
        "ssid": ssid,
        "password": password,
    });

    write_cmd(peripheral, command, &payload.to_string()).await?;

    wait_field(
        peripheral,
        status,
        "wifi_state",
        "CONFIGURED",
        Duration::from_secs(6),
    )
    .await
}

async fn connect_wifi_and_wait(
    peripheral: &Peripheral,
    command: &Characteristic,
    status: &Characteristic,
    stop: &AtomicBool,
) -> Result<IpAddr> {
    write_cmd(peripheral, command, r#"{"cmd":"wifi_connect"}"#).await?;

    let deadline = Instant::now() + Duration::from_secs(30);

    while Instant::now() < deadline {
        if stop.load(Ordering::SeqCst) {
            bail!("Speaker stopped.");
        }

        let current = read_status(peripheral, status).await?;

        if status_field(&current, "wifi_state") == Some("CONNECTED") {
            if let Some(ip) = status_field(&current, "ip") {
                return Ok(ip.parse()?);
            }
        }

        tokio::time::sleep(Duration::from_millis(500)).await;
    }

    bail!("Timed out waiting for ESPCube Wi-Fi.")
}

async fn connect_tcp_retry(ip: IpAddr, stop: &AtomicBool) -> Result<TcpStream> {
    let address = format!("{}:{}", ip, TCP_PORT);

    for attempt in 1..=12 {
        if stop.load(Ordering::SeqCst) {
            bail!("Speaker stopped.");
        }

        match TcpStream::connect_timeout(&address.parse()?, Duration::from_secs(3)) {
            Ok(stream) => {
                stream.set_nodelay(true).ok();
                stream.set_write_timeout(Some(Duration::from_secs(3))).ok();

                return Ok(stream);
            }

            Err(err) => {
                if attempt == 12 {
                    return Err(err).context("Could not connect to ESPCube speaker TCP port");
                }

                tokio::time::sleep(Duration::from_millis(500)).await;
            }
        }
    }

    unreachable!()
}

async fn write_cmd(peripheral: &Peripheral, command: &Characteristic, text: &str) -> Result<()> {
    peripheral
        .write(command, text.as_bytes(), WriteType::WithResponse)
        .await?;

    Ok(())
}

async fn read_status(peripheral: &Peripheral, status: &Characteristic) -> Result<String> {
    let bytes = peripheral.read(status).await?;
    Ok(String::from_utf8_lossy(&bytes).to_string())
}

async fn wait_field(
    peripheral: &Peripheral,
    status: &Characteristic,
    key: &str,
    expected: &str,
    timeout: Duration,
) -> Result<()> {
    let deadline = Instant::now() + timeout;

    while Instant::now() < deadline {
        let current = read_status(peripheral, status).await?;

        if status_field(&current, key) == Some(expected) {
            return Ok(());
        }

        tokio::time::sleep(Duration::from_millis(250)).await;
    }

    bail!("Timed out waiting for {key}={expected}")
}

pub fn status_field<'a>(status: &'a str, key: &str) -> Option<&'a str> {
    status.split(';').find_map(|part| {
        let (k, v) = part.split_once('=')?;

        if k.trim() == key {
            Some(v.trim())
        } else {
            None
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_fields_parse() {
        let s = "state=READY;wifi_state=CONNECTED;ip=10.0.0.33";

        assert_eq!(status_field(s, "state"), Some("READY"));

        assert_eq!(status_field(s, "wifi_state"), Some("CONNECTED"));
    }
}
