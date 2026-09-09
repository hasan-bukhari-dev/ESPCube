use std::sync::mpsc::SyncSender;

use anyhow::{Context, Result, bail};
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{SampleFormat, Stream, StreamConfig};

#[derive(Debug, Clone)]
pub struct AudioBlock {
    pub samples: Vec<f32>,
    pub channels: usize,
    pub sample_rate: u32,
}

impl AudioBlock {
    pub fn to_mono_f32(&self) -> Vec<f32> {
        if self.channels <= 1 {
            return self.samples.clone();
        }

        let mut mono = Vec::with_capacity(self.samples.len() / self.channels);

        for frame in self.samples.chunks_exact(self.channels) {
            let mut sum = 0.0f32;

            for sample in frame {
                sum += *sample;
            }

            mono.push(sum / self.channels as f32);
        }

        mono
    }
}

#[derive(Debug, Clone)]
pub enum AudioMessage {
    Block(AudioBlock),
    Error(String),
}

pub struct LoopbackCapture {
    pub stream: Stream,
    pub device_name: String,
    pub sample_rate: u32,
}

pub fn start_system_loopback(tx: SyncSender<AudioMessage>) -> Result<LoopbackCapture> {
    let host = cpal::default_host();

    // Matches the proven D2D2 bridge: on CPAL's Windows WASAPI
    // backend, an input stream built from a render endpoint is loopback.
    let device = host
        .default_output_device()
        .context("Windows default output device not found")?;

    let name = device.name().unwrap_or_else(|_| "(unknown)".to_string());

    let supported = device
        .default_output_config()
        .context("Could not read default output format")?;

    let sample_format = supported.sample_format();
    let config: StreamConfig = supported.into();

    let channels = config.channels as usize;
    let sample_rate = config.sample_rate.0;

    let err_tx = tx.clone();

    let err_fn = move |err: cpal::StreamError| {
        let _ = err_tx.try_send(AudioMessage::Error(err.to_string()));
    };

    let stream = match sample_format {
        SampleFormat::F32 => {
            let tx = tx.clone();

            device.build_input_stream(
                &config,
                move |data: &[f32], _| {
                    let _ = tx.try_send(AudioMessage::Block(AudioBlock {
                        samples: data.to_vec(),
                        channels,
                        sample_rate,
                    }));
                },
                err_fn,
                None,
            )?
        }

        SampleFormat::I16 => {
            let tx = tx.clone();

            device.build_input_stream(
                &config,
                move |data: &[i16], _| {
                    let samples = data.iter().map(|v| *v as f32 / 32768.0).collect();

                    let _ = tx.try_send(AudioMessage::Block(AudioBlock {
                        samples,
                        channels,
                        sample_rate,
                    }));
                },
                err_fn,
                None,
            )?
        }

        SampleFormat::U16 => {
            let tx = tx.clone();

            device.build_input_stream(
                &config,
                move |data: &[u16], _| {
                    let samples = data
                        .iter()
                        .map(|v| (*v as f32 - 32768.0) / 32768.0)
                        .collect();

                    let _ = tx.try_send(AudioMessage::Block(AudioBlock {
                        samples,
                        channels,
                        sample_rate,
                    }));
                },
                err_fn,
                None,
            )?
        }

        other => {
            bail!("Unsupported Windows sample format: {other:?}")
        }
    };

    stream.play()?;

    Ok(LoopbackCapture {
        stream,
        device_name: name,
        sample_rate,
    })
}
