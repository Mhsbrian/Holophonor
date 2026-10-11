use crate::midi::Performance;
use std::f32::consts::TAU;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum Voice {
    #[default]
    Reed,
    Flute,
    Cello,
    Halo,
}

impl Voice {
    pub const ALL: [Self; 4] = [Self::Reed, Self::Flute, Self::Cello, Self::Halo];
    pub fn name(self) -> &'static str {
        match self {
            Self::Reed => "Amber reed",
            Self::Flute => "Porcelain flute",
            Self::Cello => "Velvet cello",
            Self::Halo => "Soft halo",
        }
    }
    pub fn description(self) -> &'static str {
        match self {
            Self::Reed => "Warm, woody · an intimate voice",
            Self::Flute => "Airy, delicate · room to breathe",
            Self::Cello => "Dark, resonant · a slow bloom",
            Self::Halo => "Round, weightless · a quiet orbit",
        }
    }
    pub fn family(self) -> &'static str {
        match self {
            Self::Reed => "REEDS",
            Self::Flute => "WINDS",
            Self::Cello => "STRINGS",
            Self::Halo => "ATMOSPHERES",
        }
    }
    pub fn from_index(index: u32) -> Self {
        Self::ALL.get(index as usize).copied().unwrap_or_default()
    }
}

/// Fixed, band-limited harmonic voices. Every audio sample has bounded work;
/// delay storage is allocated before the stream starts.
pub struct Synth {
    sample_rate: f32,
    phase: f32,
    frequency: f32,
    amplitude: f32,
    target_frequency: f32,
    target_amplitude: f32,
    target_harmonics: [f32; 12],
    glide: f32,
    attack: f32,
    release: f32,
    air: f32,
    muted: bool,
    flush_remaining: usize,
    harmonics: [f32; 12],
    delay: Vec<f32>,
    cursor: usize,
    noise: u32,
    tone: f32,
}

impl Synth {
    pub fn new(sample_rate: f32) -> Self {
        Self {
            sample_rate,
            phase: 0.0,
            frequency: 440.0,
            amplitude: 0.0,
            target_frequency: 440.0,
            target_amplitude: 0.0,
            target_harmonics: [0.0; 12],
            glide: 1.0 - (-1.0 / (0.004 * sample_rate)).exp(),
            attack: 1.0 - (-1.0 / (0.008 * sample_rate)).exp(),
            release: 1.0 - (-1.0 / (0.018 * sample_rate)).exp(),
            air: 0.012,
            muted: false,
            flush_remaining: 0,
            harmonics: [0.0; 12],
            delay: vec![0.0; (sample_rate * 0.137) as usize],
            cursor: 0,
            noise: 0x4a38b,
            tone: 0.0,
        }
    }

    pub fn sample(
        &mut self,
        state: Performance,
        voice: Voice,
        gain: f32,
        bend_range: f32,
        muted: bool,
    ) -> [f32; 2] {
        self.prepare(state, voice, gain, bend_range, muted);
        self.next_sample()
    }

    /// Expensive control calculations run once per buffer, with constant work.
    pub fn prepare(
        &mut self,
        state: Performance,
        voice: Voice,
        gain: f32,
        bend_range: f32,
        muted: bool,
    ) {
        if muted && !self.muted {
            self.amplitude = 0.0;
            self.flush_remaining = self.delay.len();
        }
        self.muted = muted;
        self.target_frequency =
            440.0 * 2.0_f32.powf((state.note as f32 - 69.0 + state.bend * bend_range) / 12.0);
        self.target_amplitude = if state.gate {
            state.breath.powf(1.35) * state.expression * gain.clamp(0.0, 0.8)
        } else {
            0.0
        };
        self.air = if voice == Voice::Flute { 0.05 } else { 0.012 };
        let brightness = 0.2 + 0.8 * state.breath;
        for (i, target) in self.target_harmonics.iter_mut().enumerate() {
            let harmonic = (i + 1) as f32;
            let base = match voice {
                Voice::Reed => {
                    if i % 2 == 0 {
                        1.0 / harmonic.powf(1.4)
                    } else {
                        0.28 / harmonic.powf(1.6)
                    }
                }
                Voice::Flute => match i {
                    0 => 1.0,
                    1 => 0.14,
                    2 => 0.06,
                    _ => 0.0,
                },
                Voice::Cello => 0.8 / harmonic.powf(1.25),
                Voice::Halo => match i {
                    0 => 0.8,
                    1 => 0.18,
                    3 => 0.08,
                    _ => 0.0,
                },
            };
            *target = if self.target_frequency * harmonic < self.sample_rate * 0.45 {
                base * brightness.powi(i as i32)
            } else {
                0.0
            };
        }
    }

    pub fn next_sample(&mut self) -> [f32; 2] {
        if self.muted {
            self.delay[self.cursor] = 0.0;
            self.flush_remaining = self.flush_remaining.saturating_sub(1);
            self.cursor = (self.cursor + 1) % self.delay.len();
            return [0.0; 2];
        }
        self.frequency += (self.target_frequency - self.frequency) * self.glide;
        let smoothing = if self.target_amplitude > self.amplitude {
            self.attack
        } else {
            self.release
        };
        self.amplitude += (self.target_amplitude - self.amplitude) * smoothing;
        let mut signal = 0.0;
        for (i, weight) in self.harmonics.iter_mut().enumerate() {
            let harmonic = (i + 1) as f32;
            *weight += (self.target_harmonics[i] - *weight) * self.glide;
            signal += (self.phase * harmonic).sin() * *weight;
        }
        self.phase = (self.phase + TAU * self.frequency / self.sample_rate) % TAU;
        self.noise ^= self.noise << 13;
        self.noise ^= self.noise >> 17;
        self.noise ^= self.noise << 5;
        let noise = self.noise as f32 / u32::MAX as f32 * 2.0 - 1.0;
        self.tone += (noise - self.tone) * 0.14;
        let dry = (signal * 0.48 + self.tone * self.air) * self.amplitude;
        let wet = if self.flush_remaining > 0 {
            0.0
        } else {
            self.delay[self.cursor]
        };
        self.flush_remaining = self.flush_remaining.saturating_sub(1);
        self.delay[self.cursor] = dry + wet * 0.22;
        self.cursor = (self.cursor + 1) % self.delay.len();
        [(dry + wet * 0.13).tanh(), (dry + wet * 0.19).tanh()]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn note() -> Performance {
        Performance {
            note: 69,
            gate: true,
            breath: 0.8,
            expression: 1.0,
            velocity: 1.0,
            bend: 0.0,
        }
    }

    #[test]
    fn silence_release_panic_and_bounded_output() {
        for rate in [44100.0, 48000.0, 96000.0] {
            for voice in Voice::ALL {
                let mut synth = Synth::new(rate);
                for _ in 0..100 {
                    assert_eq!(
                        synth.sample(Performance::default(), voice, 0.5, 2.0, false),
                        [0.0; 2]
                    );
                }
                let mut peak = 0.0_f32;
                for _ in 0..(rate as usize / 2) {
                    let sample = synth.sample(note(), voice, 0.8, 2.0, false);
                    assert!(sample.iter().all(|s| s.is_finite() && s.abs() < 1.0));
                    peak = peak.max(sample[0].abs());
                }
                assert!(peak > 0.1);
                assert_eq!(synth.sample(note(), voice, 0.8, 2.0, true), [0.0; 2]);
                let mut released = note();
                released.gate = false;
                let mut tail = [0.0; 2];
                for _ in 0..rate as usize {
                    tail = synth.sample(released, voice, 0.8, 2.0, false);
                }
                assert!(tail[0].abs() < 0.0001);
            }
        }
    }

    #[test]
    fn pitch_tracks_a440_and_bend() {
        let mut synth = Synth::new(48000.0);
        let mut crossings = 0;
        let mut previous = 0.0;
        for i in 0..96000 {
            let value = synth.sample(note(), Voice::Flute, 0.5, 2.0, false)[0];
            if i >= 48000 && previous <= 0.0 && value > 0.0 {
                crossings += 1;
            }
            previous = value;
        }
        assert!((crossings - 440_i32).abs() <= 2, "{crossings} crossings");
        let mut bent = note();
        bent.bend = 1.0;
        for _ in 0..4800 {
            synth.sample(bent, Voice::Flute, 0.5, 2.0, false);
        }
        assert!((synth.frequency - 493.883).abs() < 0.1);
    }
}
