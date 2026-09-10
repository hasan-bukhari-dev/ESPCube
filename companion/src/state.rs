use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PresenceState {
    Dormant,
    Activating,
    Ready,
    Grace,
}

#[derive(Debug, Clone)]
pub struct SharedState {
    pub presence: PresenceState,
    pub device_name: Option<String>,
    pub ble_detail: String,
    pub connection_generation: u64,
    pub grace_deadline: Option<Instant>,
    pub runtime_awake: bool,
    pub speech_status: String,
    pub speech_detail: String,
    pub last_transcript: String,
    pub speaker_status: String,
    pub speaker_detail: String,
    pub wifi_ssid: Option<String>,
    pub audio_device: Option<String>,
    pub speaker_capture_drops: u64,
    pub last_error: Option<String>,
}

impl SharedState {
    pub fn new() -> Self {
        Self {
            presence: PresenceState::Dormant,
            device_name: None,
            ble_detail: "Watching quietly".to_string(),
            connection_generation: 0,
            grace_deadline: None,
            runtime_awake: false,
            speech_status: "Dormant".to_string(),
            speech_detail: "Whisper sleeps until ESPCube appears".to_string(),
            last_transcript: String::new(),
            speaker_status: "Idle".to_string(),
            speaker_detail: "Waiting for Speaker profile".to_string(),
            wifi_ssid: None,
            audio_device: None,
            speaker_capture_drops: 0,
            last_error: None,
        }
    }

    pub fn set_activating(&mut self, detail: impl Into<String>) {
        self.presence = PresenceState::Activating;
        self.ble_detail = detail.into();
        self.runtime_awake = true;
        self.grace_deadline = None;
        self.last_error = None;
    }

    pub fn set_ready(&mut self, name: Option<String>) {
        let was_ready = self.presence == PresenceState::Ready;
        self.presence = PresenceState::Ready;
        self.device_name = name.or_else(|| Some("ESPCube".to_string()));
        self.ble_detail = "Connected over BLE".to_string();
        self.runtime_awake = true;
        self.grace_deadline = None;
        self.last_error = None;

        if !was_ready {
            self.connection_generation = self.connection_generation.wrapping_add(1);
        }
    }

    pub fn set_grace(&mut self, grace: Duration) {
        self.presence = PresenceState::Grace;
        self.ble_detail = "Trying to reconnect".to_string();
        self.grace_deadline = Some(Instant::now() + grace);
        self.runtime_awake = true;
    }

    pub fn set_dormant(&mut self) {
        self.presence = PresenceState::Dormant;
        self.device_name = None;
        self.ble_detail = "Watching quietly".to_string();
        self.grace_deadline = None;
        self.runtime_awake = false;
        self.speech_status = "Dormant".to_string();
        self.speech_detail = "Whisper unloaded".to_string();
        self.speaker_status = "Idle".to_string();
        self.speaker_detail = "Waiting for Speaker profile".to_string();
        self.wifi_ssid = None;
        self.audio_device = None;
        self.speaker_capture_drops = 0;
    }

    pub fn grace_remaining(&self) -> Option<Duration> {
        let deadline = self.grace_deadline?;
        Some(deadline.saturating_duration_since(Instant::now()))
    }
}
