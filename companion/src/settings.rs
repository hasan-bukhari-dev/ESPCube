use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub show_on_connect: bool,
    pub start_with_windows: bool,
    pub disconnect_grace_secs: u64,
    pub speech_enabled: bool,
    pub speaker_enabled: bool,
    pub whisper_model_path: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            show_on_connect: true,
            start_with_windows: true,
            disconnect_grace_secs: 180,
            speech_enabled: true,
            speaker_enabled: true,
            whisper_model_path: String::new(),
        }
    }
}

impl Settings {
    pub fn path() -> Option<PathBuf> {
        let local = std::env::var_os("LOCALAPPDATA")?;
        Some(PathBuf::from(local).join("ESPCube").join("companion.json"))
    }

    pub fn load() -> anyhow::Result<Self> {
        let Some(path) = Self::path() else {
            return Ok(Self::default());
        };

        if !path.exists() {
            return Ok(Self::default());
        }

        let text = fs::read_to_string(path)?;
        Ok(serde_json::from_str(text.trim_start_matches('\u{feff}'))?)
    }

    pub fn save(&self) -> anyhow::Result<()> {
        let path = Self::path().ok_or_else(|| anyhow::anyhow!("LOCALAPPDATA unavailable"))?;

        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }

        let text = serde_json::to_string_pretty(self)?;
        fs::write(path, text)?;
        Ok(())
    }
}
