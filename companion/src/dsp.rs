use std::collections::VecDeque;
use std::f32::consts::PI;

const OUT_RATE: f32 = 32_000.0;

#[derive(Clone, Copy)]
struct Biquad {
    b0: f32,
    b1: f32,
    b2: f32,
    a1: f32,
    a2: f32,
    x1: f32,
    x2: f32,
    y1: f32,
    y2: f32,
}

impl Biquad {
    fn process(&mut self, x0: f32) -> f32 {
        let y0 = self.b0 * x0 + self.b1 * self.x1 + self.b2 * self.x2
            - self.a1 * self.y1
            - self.a2 * self.y2;

        self.x2 = self.x1;
        self.x1 = x0;
        self.y2 = self.y1;
        self.y1 = y0;
        y0
    }

    fn highpass(fc: f32, q: f32) -> Self {
        let w0 = 2.0 * PI * fc / OUT_RATE;
        let c = w0.cos();
        let s = w0.sin();
        let alpha = s / (2.0 * q);

        let a0 = 1.0 + alpha;

        Self {
            b0: ((1.0 + c) / 2.0) / a0,
            b1: (-(1.0 + c)) / a0,
            b2: ((1.0 + c) / 2.0) / a0,
            a1: (-2.0 * c) / a0,
            a2: (1.0 - alpha) / a0,
            x1: 0.0,
            x2: 0.0,
            y1: 0.0,
            y2: 0.0,
        }
    }

    fn peaking(fc: f32, q: f32, gain_db: f32) -> Self {
        let a = 10.0_f32.powf(gain_db / 40.0);
        let w0 = 2.0 * PI * fc / OUT_RATE;
        let c = w0.cos();
        let s = w0.sin();
        let alpha = s / (2.0 * q);

        let b0 = 1.0 + alpha * a;
        let b1 = -2.0 * c;
        let b2 = 1.0 - alpha * a;
        let a0 = 1.0 + alpha / a;
        let a1 = -2.0 * c;
        let a2 = 1.0 - alpha / a;

        Self {
            b0: b0 / a0,
            b1: b1 / a0,
            b2: b2 / a0,
            a1: a1 / a0,
            a2: a2 / a0,
            x1: 0.0,
            x2: 0.0,
            y1: 0.0,
            y2: 0.0,
        }
    }

    fn bandpass(fc: f32, q: f32) -> Self {
        let w0 = 2.0 * PI * fc / OUT_RATE;
        let c = w0.cos();
        let s = w0.sin();
        let alpha = s / (2.0 * q);
        let a0 = 1.0 + alpha;

        Self {
            b0: alpha / a0,
            b1: 0.0,
            b2: -alpha / a0,
            a1: (-2.0 * c) / a0,
            a2: (1.0 - alpha) / a0,
            x1: 0.0,
            x2: 0.0,
            y1: 0.0,
            y2: 0.0,
        }
    }
}

struct DynamicBand {
    band: Biquad,
    threshold: f32,
    ratio: f32,
    max_reduction_db: f32,
    attack_a: f32,
    release_a: f32,
    env: f32,
    gain: f32,
}

impl DynamicBand {
    fn new(
        center: f32,
        q: f32,
        threshold_db: f32,
        ratio: f32,
        max_reduction_db: f32,
        attack_ms: f32,
        release_ms: f32,
    ) -> Self {
        Self {
            band: Biquad::bandpass(center, q),
            threshold: 10.0_f32.powf(threshold_db / 20.0),
            ratio,
            max_reduction_db,
            attack_a: (-1.0 / (OUT_RATE * attack_ms / 1000.0)).exp(),
            release_a: (-1.0 / (OUT_RATE * release_ms / 1000.0)).exp(),
            env: 0.0,
            gain: 1.0,
        }
    }

    fn process(&mut self, x: f32) -> f32 {
        let b = self.band.process(x);
        let level = b.abs();

        let a = if level > self.env {
            self.attack_a
        } else {
            self.release_a
        };

        self.env = a * self.env + (1.0 - a) * level;

        let reduction_db = if self.env > self.threshold {
            let over_db = 20.0 * (self.env / self.threshold).max(1e-9).log10();

            (over_db * (1.0 - 1.0 / self.ratio)).min(self.max_reduction_db)
        } else {
            0.0
        };

        let target_gain = 10.0_f32.powf(-reduction_db / 20.0);
        let ga = if target_gain < self.gain {
            self.attack_a
        } else {
            self.release_a
        };

        self.gain = ga * self.gain + (1.0 - ga) * target_gain;

        x + (self.gain - 1.0) * b
    }
}

pub struct SpeakerDsp {
    hp: Biquad,
    mud: Biquad,
    bass_guard: DynamicBand,
    vocal_guard: DynamicBand,
    prev_input: f32,
    resample_phase: f32,
    limiter_queue: VecDeque<f32>,
    limiter_gain: f32,
}

impl SpeakerDsp {
    pub fn new(_input_rate: usize, _output_rate: usize) -> Self {
        Self {
            hp: Biquad::highpass(220.0, 1.0 / 2.0_f32.sqrt()),
            mud: Biquad::peaking(500.0, 1.0, -1.0),
            bass_guard: DynamicBand::new(260.0, 0.85, -20.0, 3.0, 3.0, 5.0, 140.0),
            vocal_guard: DynamicBand::new(2800.0, 0.75, -18.0, 2.0, 3.0, 8.0, 100.0),
            prev_input: 0.0,
            resample_phase: 0.0,
            limiter_queue: VecDeque::new(),
            limiter_gain: 1.0,
        }
    }

    pub fn process(&mut self, input: &[f32], input_rate: usize) -> Vec<i16> {
        let resampled = self.resample_linear(input, input_rate);

        let makeup = 10.0_f32.powf(7.0 / 20.0);

        let mut processed = Vec::with_capacity(resampled.len());
        for s in resampled {
            let mut x = self.hp.process(s);
            x = self.mud.process(x);
            x = self.bass_guard.process(x);
            x = self.vocal_guard.process(x);
            x *= makeup;
            processed.push(x);
        }

        let limited = self.lookahead_limit(&processed);

        limited
            .into_iter()
            .map(|x| {
                let y = x.clamp(-0.8912509, 0.8912509);
                (y * 32767.0).round().clamp(-32768.0, 32767.0) as i16
            })
            .collect()
    }

    fn resample_linear(&mut self, input: &[f32], input_rate: usize) -> Vec<f32> {
        if input.is_empty() {
            return Vec::new();
        }
        if input_rate == 32_000 {
            return input.to_vec();
        }

        let step = input_rate as f32 / 32_000.0;
        let mut src = Vec::with_capacity(input.len() + 1);
        src.push(self.prev_input);
        src.extend_from_slice(input);

        let mut out = Vec::new();
        let mut pos = self.resample_phase;

        while pos + 1.0 < src.len() as f32 {
            let i = pos.floor() as usize;
            let frac = pos - i as f32;
            let v = src[i] * (1.0 - frac) + src[i + 1] * frac;
            out.push(v);
            pos += step;
        }

        self.resample_phase = pos - (src.len() as f32 - 1.0);
        self.prev_input = *src.last().unwrap_or(&0.0);
        out
    }

    fn lookahead_limit(&mut self, input: &[f32]) -> Vec<f32> {
        const LOOKAHEAD: usize = 160; // 5 ms @ 32k
        const CEILING: f32 = 0.8912509; // -1 dBFS
        let release_a = (-1.0 / (OUT_RATE * 0.080)).exp();

        let mut out = Vec::with_capacity(input.len());

        for &x in input {
            self.limiter_queue.push_back(x);

            if self.limiter_queue.len() <= LOOKAHEAD {
                continue;
            }

            let future_peak = self
                .limiter_queue
                .iter()
                .map(|v| v.abs())
                .fold(0.0f32, f32::max);

            let desired = if future_peak <= CEILING {
                1.0
            } else {
                CEILING / future_peak.max(1e-9)
            };

            if desired < self.limiter_gain {
                self.limiter_gain = desired;
            } else {
                self.limiter_gain = release_a * self.limiter_gain + (1.0 - release_a) * desired;
            }

            let oldest = self.limiter_queue.pop_front().unwrap();
            out.push(oldest * self.limiter_gain);
        }

        out
    }
}
