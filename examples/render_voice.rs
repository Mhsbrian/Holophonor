//! Offline sound check and DSP timing; never represents hardware latency.
use holophonor::{
    midi::Performance,
    synth::{Synth, Voice},
};
use std::{io::Write, time::Instant};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "voice.wav".into());
    let rate = 48000_u32;
    let frames = rate * 12;
    let mut synth = Synth::new(rate as f32);
    let mut samples = Vec::with_capacity(frames as usize * 4);
    let started = Instant::now();
    for i in 0..frames {
        if i % 256 == 0 {
            let time = i as f32 / rate as f32;
            let part = (time / 3.0) as usize;
            let fraction = time % 3.0 / 3.0;
            synth.prepare(
                Performance {
                    note: [60, 64, 67, 72][part.min(3)],
                    gate: fraction < 0.9,
                    breath: (fraction * std::f32::consts::PI).sin().max(0.0) * 0.8,
                    expression: 1.0,
                    velocity: 1.0,
                    bend: (time * 4.0).sin() * 0.05,
                },
                Voice::ALL[part.min(3)],
                0.5,
                2.0,
                false,
            );
        }
        for sample in synth.next_sample() {
            samples.extend_from_slice(&((sample * 32767.0) as i16).to_le_bytes());
        }
    }
    let elapsed = started.elapsed();
    let mut file = std::fs::File::create(path)?;
    let data_len = samples.len() as u32;
    file.write_all(b"RIFF")?;
    file.write_all(&(data_len + 36).to_le_bytes())?;
    file.write_all(b"WAVEfmt ")?;
    file.write_all(&16_u32.to_le_bytes())?;
    file.write_all(&1_u16.to_le_bytes())?;
    file.write_all(&2_u16.to_le_bytes())?;
    file.write_all(&rate.to_le_bytes())?;
    file.write_all(&(rate * 4).to_le_bytes())?;
    file.write_all(&4_u16.to_le_bytes())?;
    file.write_all(&16_u16.to_le_bytes())?;
    file.write_all(b"data")?;
    file.write_all(&data_len.to_le_bytes())?;
    file.write_all(&samples)?;
    println!(
        "Rendered 12 s of stereo audio in {:.2} ms ({:.2}% of real time, including sample conversion).",
        elapsed.as_secs_f64() * 1000.0,
        elapsed.as_secs_f64() / 12.0 * 100.0
    );
    Ok(())
}
