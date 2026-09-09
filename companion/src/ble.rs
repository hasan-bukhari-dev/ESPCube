use std::sync::Arc;
use std::sync::atomic::Ordering;
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, bail};
use btleplug::api::{Central, Characteristic, Manager as _, Peripheral as _, ScanFilter};
use btleplug::platform::{Adapter, Manager, Peripheral};
use futures::StreamExt;
use parking_lot::RwLock;
use uuid::Uuid;

use crate::logging;
use crate::settings::Settings;
use crate::speaker::{self, SpeakerWorker};
use crate::speech::{
    PendingEnd, SpeechSession, WhisperEngine, insert_text_windows, insertion_text,
};
use crate::state::SharedState;

const SPEECH_SERVICE: &str = "45535043-5542-4c45-8000-000000000001";
const SPEECH_CONTROL: &str = "45535043-5542-4c45-8000-000000000002";
const SPEECH_STATUS: &str = "45535043-5542-4c45-8000-000000000003";
const SPEECH_AUDIO: &str = "45535043-5542-4c45-8000-000000000004";

const SPEAKER_SERVICE: &str = "45535043-5542-4c45-8100-000000000001";
const SPEAKER_COMMAND: &str = "45535043-5542-4c45-8100-000000000002";
const SPEAKER_STATUS: &str = "45535043-5542-4c45-8100-000000000003";

struct Chars {
    speech_control: Characteristic,
    speech_status: Characteristic,
    speech_audio: Characteristic,
    speaker_command: Characteristic,
    speaker_status: Characteristic,
}

pub fn spawn_unified_manager(shared: Arc<RwLock<SharedState>>, settings: Arc<RwLock<Settings>>) {
    thread::Builder::new()
        .name("espcube-ble".to_string())
        .spawn(move || {
            let runtime = match tokio::runtime::Runtime::new() {
                Ok(v) => v,
                Err(err) => {
                    shared.write().last_error = Some(format!("BLE runtime failed: {err}"));
                    return;
                }
            };

            runtime.block_on(supervisor(shared, settings));
        })
        .expect("failed to spawn ESPCube BLE manager");
}

async fn supervisor(shared: Arc<RwLock<SharedState>>, settings: Arc<RwLock<Settings>>) {
    let mut whisper: Option<WhisperEngine> = None;

    loop {
        let manager = match Manager::new().await {
            Ok(v) => v,
            Err(err) => {
                set_error(&shared, &format!("Bluetooth unavailable: {err}"));
                tokio::time::sleep(Duration::from_secs(2)).await;
                continue;
            }
        };

        let adapters = match manager.adapters().await {
            Ok(v) if !v.is_empty() => v,
            Ok(_) => {
                set_error(&shared, "No Bluetooth adapter available");
                tokio::time::sleep(Duration::from_secs(3)).await;
                continue;
            }
            Err(err) => {
                set_error(&shared, &err.to_string());
                tokio::time::sleep(Duration::from_secs(2)).await;
                continue;
            }
        };

        let mut first = find_on_adapters(&adapters, &shared).await;

        while let Some(peripheral) = first {
            let name = peripheral
                .properties()
                .await
                .ok()
                .flatten()
                .and_then(|p| p.local_name);

            shared.write().set_ready(name);
            logging::write("[BLE] ESPCube connected");

            let result = connected_session(
                peripheral,
                Arc::clone(&shared),
                Arc::clone(&settings),
                &mut whisper,
            )
            .await;

            if let Err(err) = result {
                logging::write(&format!("[BLE] session ended: {err:#}"));
            }

            let grace_secs = settings.read().disconnect_grace_secs.max(10);

            let grace = Duration::from_secs(grace_secs);

            {
                let mut s = shared.write();
                s.set_grace(grace);
                s.speech_status = if whisper.is_some() {
                    "Ready".to_string()
                } else {
                    "Dormant".to_string()
                };
                s.speech_detail = "Connection lost • keeping model warm".to_string();
                s.speaker_status = "Waiting".to_string();
                s.speaker_detail = "Connection lost • reconnecting".to_string();
            }

            let deadline = Instant::now() + grace;
            let mut reconnected = None;

            while Instant::now() < deadline {
                if let Some(p) = find_on_adapters(&adapters, &shared).await {
                    reconnected = Some(p);
                    break;
                }

                tokio::time::sleep(Duration::from_millis(900)).await;
            }

            if let Some(p) = reconnected {
                first = Some(p);
                continue;
            }

            // Product contract:
            // heavy runtime only unloads after grace expires.
            whisper = None;
            shared.write().set_dormant();

            logging::write("[LIFECYCLE] grace expired -> dormant");

            first = None;
        }

        shared.write().set_dormant();

        tokio::time::sleep(Duration::from_secs(2)).await;
    }
}

async fn connected_session(
    peripheral: Peripheral,
    shared: Arc<RwLock<SharedState>>,
    settings: Arc<RwLock<Settings>>,
    whisper: &mut Option<WhisperEngine>,
) -> Result<()> {
    if !peripheral.is_connected().await? {
        bail!("ESPCube disconnected before setup");
    }

    peripheral.discover_services().await?;

    let chars = resolve_chars(&peripheral)?;

    validate_speech_status(&peripheral, &chars.speech_status).await?;

    peripheral
        .subscribe(&chars.speech_control)
        .await
        .context("Speech CONTROL subscribe failed")?;

    peripheral
        .subscribe(&chars.speech_audio)
        .await
        .context("Speech AUDIO subscribe failed")?;

    let mut notifications = peripheral.notifications().await?;

    let mut session = SpeechSession::new();
    let mut pre_start_audio: Vec<Vec<u8>> = Vec::new();
    let mut pending_end: Option<PendingEnd> = None;
    let mut speaker_worker: Option<SpeakerWorker> = None;

    let mut tick = tokio::time::interval(Duration::from_millis(100));

    loop {
        tokio::select! {
            _ = tick.tick() => {
                if !peripheral
                    .is_connected()
                    .await
                    .unwrap_or(false)
                {
                    stop_speaker(&mut speaker_worker);
                    bail!("ESPCube BLE disconnected");
                }

                let current_settings =
                    settings.read().clone();

                // Persistent Whisper lifecycle.
                if current_settings.speech_enabled {
                    if whisper.is_none() {
                        {
                            let mut s = shared.write();
                            s.speech_status = "Loading".to_string();
                            s.speech_detail =
                                "Loading Whisper once".to_string();
                        }

                        match WhisperEngine::load(
                            &current_settings.whisper_model_path
                        ) {
                            Ok(engine) => {
                                *whisper = Some(engine);

                                let mut s = shared.write();
                                s.speech_status = "Ready".to_string();
                                s.speech_detail =
                                    "Persistent Whisper loaded".to_string();
                            }

                            Err(err) => {
                                let mut s = shared.write();
                                s.speech_status = "Error".to_string();
                                s.speech_detail = err.to_string();
                            }
                        }
                    }
                } else {
                    *whisper = None;

                    let mut s = shared.write();
                    s.speech_status = "Off".to_string();
                    s.speech_detail =
                        "Disabled in settings".to_string();
                }

                // Deferred END drain:
                // CONTROL END can precede final AUDIO on WinRT.
                if let Some(end) = pending_end.as_ref() {
                    let complete =
                        session.matches_expected(
                            end.expected_samples,
                            end.expected_frames,
                        );

                    let expired =
                        Instant::now() >= end.deadline;

                    if complete || expired {
                        let end =
                            pending_end.take().unwrap();

                        finalize_speech(
                            &mut session,
                            &end.message,
                            whisper.as_ref(),
                            &shared,
                        );
                    }
                }

                // Speaker lifecycle.
                let worker_running =
                    speaker_worker
                        .as_ref()
                        .map(|w| {
                            w.running.load(Ordering::SeqCst)
                        })
                        .unwrap_or(false);

                if !worker_running {
                    speaker_worker = None;
                }

                if !current_settings.speaker_enabled {
                    stop_speaker(&mut speaker_worker);

                    let mut s = shared.write();
                    s.speaker_status = "Off".to_string();
                    s.speaker_detail =
                        "Disabled in settings".to_string();
                } else {
                    let status_text =
                        read_text(
                            &peripheral,
                            &chars.speaker_status,
                        )
                        .await
                        .unwrap_or_default();

                    let state =
                        status_field(&status_text, "state");

                    if state == Some("READY") {
                        if speaker_worker.is_none() {
                            speaker_worker = Some(
                                speaker::spawn(
                                    peripheral.clone(),
                                    chars.speaker_command.clone(),
                                    chars.speaker_status.clone(),
                                    Arc::clone(&shared),
                                )
                            );
                        }
                    } else {
                        stop_speaker(&mut speaker_worker);

                        let mut s = shared.write();
                        s.speaker_status = "Idle".to_string();
                        s.speaker_detail =
                            "Waiting for Speaker profile".to_string();
                        s.audio_device = None;
                        s.wifi_ssid = None;
                    }
                }
            }

            maybe_notification = notifications.next() => {
                let Some(notification) = maybe_notification else {
                    stop_speaker(&mut speaker_worker);
                    bail!("BLE notification stream ended");
                };

                if notification.uuid == chars.speech_control.uuid {
                    let message =
                        String::from_utf8_lossy(
                            &notification.value
                        )
                        .trim_matches('\0')
                        .to_string();

                    if message.starts_with("START") {
                        if let Some(old_end) =
                            pending_end.take()
                        {
                            finalize_speech(
                                &mut session,
                                &old_end.message,
                                whisper.as_ref(),
                                &shared,
                            );
                        }

                        session.start();

                        {
                            let mut s = shared.write();
                            s.speech_status = "Listening".to_string();
                            s.speech_detail =
                                "Speak naturally".to_string();
                        }

                        if !pre_start_audio.is_empty() {
                            let first_sequence =
                                if pre_start_audio[0].len() >= 4 {
                                    Some(
                                        u16::from_le_bytes([
                                            pre_start_audio[0][2],
                                            pre_start_audio[0][3],
                                        ])
                                    )
                                } else {
                                    None
                                };

                            if first_sequence == Some(0) {
                                for packet in
                                    pre_start_audio.drain(..)
                                {
                                    session.handle_audio(&packet);
                                }
                            } else {
                                pre_start_audio.clear();
                            }
                        }
                    } else if message.starts_with("END") {
                        pending_end =
                            Some(PendingEnd::from_message(message));
                    }
                } else if notification.uuid == chars.speech_audio.uuid {
                    if session.active() {
                        session.handle_audio(&notification.value);
                    } else {
                        let data = &notification.value;

                        if data.len() >= 10
                            && data[0] == 0xA1
                            && data[1] == 1
                        {
                            let sequence =
                                u16::from_le_bytes([data[2], data[3]]);

                            if sequence == 0 {
                                pre_start_audio.clear();
                                pre_start_audio.push(data.clone());
                            } else if !pre_start_audio.is_empty()
                                && pre_start_audio.len() < 16
                            {
                                let previous =
                                    &pre_start_audio[
                                        pre_start_audio.len() - 1
                                    ];

                                let previous_sequence =
                                    u16::from_le_bytes([
                                        previous[2],
                                        previous[3],
                                    ]);

                                if sequence
                                    == previous_sequence.wrapping_add(1)
                                {
                                    pre_start_audio.push(data.clone());
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

fn finalize_speech(
    session: &mut SpeechSession,
    end_message: &str,
    whisper: Option<&WhisperEngine>,
    shared: &Arc<RwLock<SharedState>>,
) {
    let transport_ok = session.finish_transport(end_message);

    if !transport_ok {
        let mut s = shared.write();
        s.speech_status = "Transport error".to_string();
        s.speech_detail = "Speech parity check failed".to_string();

        logging::write("[SPEECH] parity failure");
        return;
    }

    let Some(engine) = whisper else {
        let mut s = shared.write();
        s.speech_status = "Ready".to_string();
        s.speech_detail = "Speech disabled / model unavailable".to_string();
        return;
    };

    {
        let mut s = shared.write();
        s.speech_status = "Transcribing".to_string();
        s.speech_detail = "Whisper is working".to_string();
    }

    match engine.transcribe(&session.pcm) {
        Ok(transcript) => {
            if transcript.is_empty() {
                let mut s = shared.write();
                s.speech_status = "Ready".to_string();
                s.speech_detail = "No speech detected".to_string();
                return;
            }

            let text = insertion_text(&transcript);

            match insert_text_windows(&text) {
                Ok(_) => {
                    let mut s = shared.write();
                    s.last_transcript = transcript;
                    s.speech_status = "Ready".to_string();
                    s.speech_detail = "Inserted into focused app".to_string();
                }

                Err(err) => {
                    let mut s = shared.write();
                    s.speech_status = "Insert error".to_string();
                    s.speech_detail = err.to_string();
                }
            }
        }

        Err(err) => {
            let mut s = shared.write();
            s.speech_status = "Whisper error".to_string();
            s.speech_detail = err.to_string();
        }
    }
}

fn stop_speaker(worker: &mut Option<SpeakerWorker>) {
    if let Some(worker) = worker {
        worker.stop.store(true, Ordering::SeqCst);
    }

    *worker = None;
}

async fn find_on_adapters(
    adapters: &[Adapter],
    shared: &Arc<RwLock<SharedState>>,
) -> Option<Peripheral> {
    for adapter in adapters {
        match find_and_connect(adapter, shared).await {
            Ok(Some(p)) => return Some(p),
            Ok(None) => {}
            Err(err) => {
                logging::write(&format!("[SCAN] {err:#}"));
            }
        }
    }

    None
}

async fn find_and_connect(
    adapter: &Adapter,
    shared: &Arc<RwLock<SharedState>>,
) -> Result<Option<Peripheral>> {
    let speech_service = Uuid::parse_str(SPEECH_SERVICE)?;

    let speaker_service = Uuid::parse_str(SPEAKER_SERVICE)?;

    adapter.start_scan(ScanFilter::default()).await?;

    tokio::time::sleep(Duration::from_millis(1200)).await;

    let peripherals = adapter.peripherals().await?;

    for peripheral in peripherals {
        let props = match peripheral.properties().await {
            Ok(Some(v)) => v,
            _ => continue,
        };

        let name_match = props
            .local_name
            .as_deref()
            .map(|name| name.to_ascii_lowercase().contains("espcube"))
            .unwrap_or(false);

        let advertised_match = props
            .services
            .iter()
            .any(|uuid| *uuid == speech_service || *uuid == speaker_service);

        if !name_match && !advertised_match {
            continue;
        }

        shared.write().set_activating("ESPCube found • connecting");

        if !peripheral.is_connected().await.unwrap_or(false) {
            if peripheral.connect().await.is_err() {
                continue;
            }
        }

        if peripheral.discover_services().await.is_err() {
            let _ = peripheral.disconnect().await;
            continue;
        }

        let has_speech = peripheral
            .services()
            .iter()
            .any(|service| service.uuid == speech_service);

        let has_speaker = peripheral
            .services()
            .iter()
            .any(|service| service.uuid == speaker_service);

        if !has_speech || !has_speaker {
            let _ = peripheral.disconnect().await;
            continue;
        }

        let _ = adapter.stop_scan().await;

        return Ok(Some(peripheral));
    }

    let _ = adapter.stop_scan().await;
    Ok(None)
}

fn resolve_chars(peripheral: &Peripheral) -> Result<Chars> {
    let speech_control = Uuid::parse_str(SPEECH_CONTROL)?;

    let speech_status = Uuid::parse_str(SPEECH_STATUS)?;

    let speech_audio = Uuid::parse_str(SPEECH_AUDIO)?;

    let speaker_command = Uuid::parse_str(SPEAKER_COMMAND)?;

    let speaker_status = Uuid::parse_str(SPEAKER_STATUS)?;

    let all = peripheral.characteristics();

    let find = |uuid: Uuid| {
        all.iter()
            .find(|c| c.uuid == uuid)
            .cloned()
            .with_context(|| format!("GATT characteristic missing: {uuid}"))
    };

    Ok(Chars {
        speech_control: find(speech_control)?,
        speech_status: find(speech_status)?,
        speech_audio: find(speech_audio)?,
        speaker_command: find(speaker_command)?,
        speaker_status: find(speaker_status)?,
    })
}

async fn validate_speech_status(
    peripheral: &Peripheral,
    status_char: &Characteristic,
) -> Result<()> {
    let status = read_text(peripheral, status_char).await?;

    for required in [
        "protocol=1",
        "codec=ima_adpcm",
        "source=pcm16",
        "rate=16000",
        "channels=1",
        "block_samples=320",
    ] {
        if !status.contains(required) {
            bail!("Speech STATUS parity failure: missing {required}");
        }
    }

    Ok(())
}

async fn read_text(peripheral: &Peripheral, characteristic: &Characteristic) -> Result<String> {
    let bytes = peripheral.read(characteristic).await?;

    Ok(String::from_utf8_lossy(&bytes).to_string())
}

fn status_field<'a>(status: &'a str, key: &str) -> Option<&'a str> {
    status.split(';').find_map(|part| {
        let (k, v) = part.split_once('=')?;

        if k.trim() == key {
            Some(v.trim())
        } else {
            None
        }
    })
}

fn set_error(shared: &Arc<RwLock<SharedState>>, message: &str) {
    let mut s = shared.write();
    s.set_dormant();
    s.last_error = Some(message.to_string());
}
