use std::path::PathBuf;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, bail};
use whisper_rs::{
    FullParams, SamplingStrategy, WhisperContext, WhisperContextParameters,
    convert_integer_to_float_audio,
};

const SAMPLE_RATE: usize = 16_000;
const WHISPER_THREADS: i32 = 8;

const INDEX_TABLE: [i32; 16] = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

const STEP_TABLE: [i32; 89] = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
];

pub struct WhisperEngine {
    context: WhisperContext,
}

impl WhisperEngine {
    pub fn resolve_model_path(configured: &str) -> PathBuf {
        if !configured.trim().is_empty() {
            return PathBuf::from(configured.trim());
        }

        if let Ok(exe) = std::env::current_exe() {
            if let Some(dir) = exe.parent() {
                let installed = dir.join("models").join("ggml-tiny.en.bin");

                if installed.exists() {
                    return installed;
                }
            }
        }

        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("..")
            .join("..")
            .join("tools")
            .join("whisper.cpp")
            .join("models")
            .join("ggml-tiny.en.bin")
    }

    pub fn load(configured: &str) -> Result<Self> {
        let model = Self::resolve_model_path(configured);

        if !model.exists() {
            bail!("Whisper model missing: {}", model.display());
        }

        let model_string = model.to_string_lossy().to_string();

        let context =
            WhisperContext::new_with_params(&model_string, WhisperContextParameters::default())
                .context("Failed to load Whisper model")?;

        Ok(Self { context })
    }

    pub fn transcribe(&self, pcm: &[i16]) -> Result<String> {
        if pcm.is_empty() {
            return Ok(String::new());
        }

        let mut audio = vec![0.0f32; pcm.len()];

        convert_integer_to_float_audio(pcm, &mut audio)
            .context("PCM16 -> f32 conversion failed")?;

        let mut state = self
            .context
            .create_state()
            .context("Could not create Whisper state")?;

        let mut params = FullParams::new(SamplingStrategy::Greedy { best_of: 1 });

        params.set_n_threads(WHISPER_THREADS);
        params.set_language(Some("en"));
        params.set_translate(false);
        params.set_no_context(true);
        params.set_no_timestamps(true);
        params.set_temperature_inc(-1.0);
        params.set_print_special(false);
        params.set_print_progress(false);
        params.set_print_realtime(false);
        params.set_print_timestamps(false);

        state
            .full(params, &audio)
            .context("Whisper inference failed")?;

        let mut transcript = String::new();

        for segment in state.as_iter() {
            transcript.push_str(&segment.to_string());
        }

        Ok(transcript.trim().to_string())
    }
}

pub struct SpeechSession {
    active: bool,
    pub pcm: Vec<i16>,
    expected_sequence: Option<u16>,
    sequence_gaps: u64,
    audio_packets: u64,
    invalid_packets: u64,
}

impl SpeechSession {
    pub fn new() -> Self {
        Self {
            active: false,
            pcm: Vec::new(),
            expected_sequence: None,
            sequence_gaps: 0,
            audio_packets: 0,
            invalid_packets: 0,
        }
    }

    pub fn active(&self) -> bool {
        self.active
    }

    pub fn start(&mut self) {
        self.pcm.clear();
        self.expected_sequence = None;
        self.sequence_gaps = 0;
        self.audio_packets = 0;
        self.invalid_packets = 0;
        self.active = true;
    }

    fn decode_nibble(code: u8, mut predictor: i32, mut index: i32) -> (i32, i32) {
        let step = STEP_TABLE[index as usize];
        let mut delta = step >> 3;

        if code & 4 != 0 {
            delta += step;
        }
        if code & 2 != 0 {
            delta += step >> 1;
        }
        if code & 1 != 0 {
            delta += step >> 2;
        }

        if code & 8 != 0 {
            predictor -= delta;
        } else {
            predictor += delta;
        }

        predictor = predictor.clamp(-32768, 32767);

        index += INDEX_TABLE[(code & 0x0F) as usize];
        index = index.clamp(0, 88);

        (predictor, index)
    }

    fn decode_audio_packet(&mut self, data: &[u8]) -> Vec<i16> {
        if data.len() < 10 || data[0] != 0xA1 || data[1] != 1 {
            self.invalid_packets += 1;
            return Vec::new();
        }

        let sequence = u16::from_le_bytes([data[2], data[3]]);

        let sample_count = u16::from_le_bytes([data[4], data[5]]) as usize;

        let initial_predictor = i16::from_le_bytes([data[6], data[7]]);

        let mut predictor = initial_predictor as i32;
        let mut index = data[8] as i32;

        if sample_count == 0 || index > 88 {
            self.invalid_packets += 1;
            return Vec::new();
        }

        if let Some(expected) = self.expected_sequence {
            if sequence != expected {
                let missing = sequence.wrapping_sub(expected);
                if missing != 0 {
                    self.sequence_gaps += missing as u64;
                }
            }
        }

        self.expected_sequence = Some(sequence.wrapping_add(1));

        let mut out = Vec::with_capacity(sample_count);
        out.push(initial_predictor);

        let needed = sample_count - 1;
        let mut decoded = 0usize;

        for packed in &data[10..] {
            if decoded >= needed {
                break;
            }

            let low = packed & 0x0F;
            (predictor, index) = Self::decode_nibble(low, predictor, index);
            out.push(predictor as i16);
            decoded += 1;

            if decoded >= needed {
                break;
            }

            let high = (packed >> 4) & 0x0F;
            (predictor, index) = Self::decode_nibble(high, predictor, index);
            out.push(predictor as i16);
            decoded += 1;
        }

        if out.len() != sample_count {
            self.invalid_packets += 1;
        }

        self.audio_packets += 1;
        out
    }

    pub fn handle_audio(&mut self, data: &[u8]) {
        if !self.active {
            return;
        }

        let decoded = self.decode_audio_packet(data);

        if !decoded.is_empty() {
            self.pcm.extend(decoded);
        }
    }

    pub fn matches_expected(
        &self,
        expected_samples: Option<u64>,
        expected_frames: Option<u64>,
    ) -> bool {
        let samples_ok = expected_samples
            .map(|v| v == self.pcm.len() as u64)
            .unwrap_or(false);

        let frames_ok = expected_frames
            .map(|v| v == self.audio_packets)
            .unwrap_or(false);

        samples_ok && frames_ok
    }

    pub fn finish_transport(&mut self, message: &str) -> bool {
        self.active = false;

        let firmware_samples = parse_control_number(message, "samples");

        let firmware_frames = parse_control_number(message, "frames");

        let notify_failures = parse_control_number(message, "notify_failures");

        let samples_match = firmware_samples
            .map(|v| v == self.pcm.len() as u64)
            .unwrap_or(false);

        let frames_match = firmware_frames
            .map(|v| v == self.audio_packets)
            .unwrap_or(false);

        let notify_clean = notify_failures.map(|v| v == 0).unwrap_or(false);

        samples_match
            && frames_match
            && notify_clean
            && self.sequence_gaps == 0
            && self.invalid_packets == 0
            && !self.pcm.is_empty()
    }
}

pub struct PendingEnd {
    pub message: String,
    pub expected_samples: Option<u64>,
    pub expected_frames: Option<u64>,
    pub deadline: Instant,
}

impl PendingEnd {
    pub fn from_message(message: String) -> Self {
        Self {
            expected_samples: parse_control_number(&message, "samples"),
            expected_frames: parse_control_number(&message, "frames"),
            message,
            deadline: Instant::now() + Duration::from_millis(800),
        }
    }
}

pub fn parse_control_number(message: &str, key: &str) -> Option<u64> {
    for field in message.split(';').skip(1) {
        let Some((field_key, field_value)) = field.split_once('=') else {
            continue;
        };

        if field_key == key {
            return field_value.parse::<u64>().ok();
        }
    }

    None
}

#[cfg(target_os = "windows")]
const INPUT_KEYBOARD: u32 = 1;
#[cfg(target_os = "windows")]
const KEYEVENTF_KEYUP: u32 = 0x0002;
#[cfg(target_os = "windows")]
const KEYEVENTF_UNICODE: u32 = 0x0004;

#[cfg(target_os = "windows")]
#[repr(C)]
#[derive(Copy, Clone)]
struct WinMouseInput {
    dx: i32,
    dy: i32,
    mouse_data: u32,
    flags: u32,
    time: u32,
    extra_info: usize,
}

#[cfg(target_os = "windows")]
#[repr(C)]
#[derive(Copy, Clone)]
struct WinKeyboardInput {
    virtual_key: u16,
    scan_code: u16,
    flags: u32,
    time: u32,
    extra_info: usize,
}

#[cfg(target_os = "windows")]
#[repr(C)]
#[derive(Copy, Clone)]
struct WinHardwareInput {
    message: u32,
    param_low: u16,
    param_high: u16,
}

#[cfg(target_os = "windows")]
#[repr(C)]
#[derive(Copy, Clone)]
union WinInputUnion {
    mouse: WinMouseInput,
    keyboard: WinKeyboardInput,
    hardware: WinHardwareInput,
}

#[cfg(target_os = "windows")]
#[repr(C)]
#[derive(Copy, Clone)]
struct WinInput {
    input_type: u32,
    data: WinInputUnion,
}

#[cfg(target_os = "windows")]
#[link(name = "user32")]
unsafe extern "system" {
    fn SendInput(input_count: u32, inputs: *const WinInput, input_size: i32) -> u32;
}

#[cfg(target_os = "windows")]
fn make_unicode_input(unit: u16, flags: u32) -> WinInput {
    WinInput {
        input_type: INPUT_KEYBOARD,
        data: WinInputUnion {
            keyboard: WinKeyboardInput {
                virtual_key: 0,
                scan_code: unit,
                flags,
                time: 0,
                extra_info: 0,
            },
        },
    }
}

#[cfg(target_os = "windows")]
pub fn insert_text_windows(text: &str) -> Result<usize> {
    if text.is_empty() {
        return Ok(0);
    }

    let utf16: Vec<u16> = text.encode_utf16().collect();
    let input_size = std::mem::size_of::<WinInput>() as i32;

    for unit in &utf16 {
        let down = make_unicode_input(*unit, KEYEVENTF_UNICODE);

        let down_result = unsafe { SendInput(1, &down, input_size) };

        if down_result != 1 {
            bail!(
                "SendInput key-down failed: {}",
                std::io::Error::last_os_error()
            );
        }

        let up = make_unicode_input(*unit, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP);

        let up_result = unsafe { SendInput(1, &up, input_size) };

        if up_result != 1 {
            bail!(
                "SendInput key-up failed: {}",
                std::io::Error::last_os_error()
            );
        }
    }

    Ok(utf16.len())
}

#[cfg(not(target_os = "windows"))]
pub fn insert_text_windows(_text: &str) -> Result<usize> {
    bail!("Native text insertion is Windows-only.")
}

pub fn insertion_text(transcript: &str) -> String {
    format!("{} ", transcript.trim_end())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_control_numbers() {
        let m = "END;samples=320;frames=1;notify_failures=0";

        assert_eq!(parse_control_number(m, "samples"), Some(320));

        assert_eq!(parse_control_number(m, "frames"), Some(1));
    }

    #[test]
    fn trailing_space_contract() {
        assert_eq!(insertion_text("hello"), "hello ");
    }
}
