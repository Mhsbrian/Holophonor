use crate::{
    midi::{Interpreter, Performance, Profile},
    synth::{Synth, Voice},
};
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::{
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering},
    },
    thread,
    time::{Duration, Instant},
};

pub struct Shared {
    pub midi: AtomicU64,
    pub preview: AtomicU64,
    pub preview_on: AtomicBool,
    pub panic_epoch: AtomicU32,
    resumed_epoch: AtomicU32,
    pub profile: AtomicU64,
    pub voice: AtomicU32,
    pub gain: AtomicU32,
    pub bend_range: AtomicU32,
    pub peak: AtomicU32,
    pub callbacks: AtomicU64,
    pub buffer_frames: AtomicU32,
    pub audio_errors: AtomicU32,
    pub midi_messages: AtomicU64,
    stop: AtomicBool,
    // These locks are used only by the device worker and UI, never by audio.
    pub midi_status: Mutex<MidiStatus>,
    pub requested_port: Mutex<Option<String>>,
}

#[derive(Clone, Default)]
pub struct MidiStatus {
    pub ports: Vec<String>,
    pub connected: Option<String>,
    pub error: Option<String>,
}

impl Default for Shared {
    fn default() -> Self {
        Self {
            midi: AtomicU64::new(Performance::default().pack()),
            preview: AtomicU64::new(Performance::default().pack()),
            preview_on: AtomicBool::new(false),
            panic_epoch: AtomicU32::new(0),
            resumed_epoch: AtomicU32::new(0),
            profile: AtomicU64::new(Profile::default().pack()),
            voice: AtomicU32::new(0),
            gain: AtomicU32::new(0.5_f32.to_bits()),
            bend_range: AtomicU32::new(2.0_f32.to_bits()),
            peak: AtomicU32::new(0),
            callbacks: AtomicU64::new(0),
            buffer_frames: AtomicU32::new(0),
            audio_errors: AtomicU32::new(0),
            midi_messages: AtomicU64::new(0),
            stop: AtomicBool::new(false),
            midi_status: Mutex::new(MidiStatus::default()),
            requested_port: Mutex::new(None),
        }
    }
}

impl Shared {
    pub fn performance(&self) -> Performance {
        let muted = self.is_muted();
        let word = if self.preview_on.load(Ordering::Relaxed) {
            &self.preview
        } else {
            &self.midi
        };
        let mut state = Performance::unpack(word.load(Ordering::Relaxed));
        if muted {
            state.gate = false;
            state.breath = 0.0;
        }
        state
    }
    pub fn panic(&self) {
        self.preview_on.store(false, Ordering::Relaxed);
        self.panic_epoch.fetch_add(1, Ordering::SeqCst);
    }
    pub fn is_muted(&self) -> bool {
        self.panic_epoch.load(Ordering::SeqCst) != self.resumed_epoch.load(Ordering::SeqCst)
    }
    pub fn resume(&self, epoch: u32) {
        // A callback that started before panic can only acknowledge its old
        // epoch. It cannot undo a later panic, even if the threads interleave.
        self.resumed_epoch.store(epoch, Ordering::SeqCst);
    }
}

pub struct MidiWorker {
    shared: Arc<Shared>,
    worker: Option<thread::JoinHandle<()>>,
}

impl MidiWorker {
    pub fn start(shared: Arc<Shared>) -> Self {
        let controls = shared.clone();
        let worker = thread::spawn(move || midi_loop(controls));
        Self {
            shared,
            worker: Some(worker),
        }
    }
}

impl Drop for MidiWorker {
    fn drop(&mut self) {
        self.shared.stop.store(true, Ordering::Relaxed);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

pub fn choose_port(ports: &[String], requested: Option<&str>) -> Option<usize> {
    if let Some(requested) = requested {
        return ports.iter().position(|p| p == requested);
    }
    let usable = |name: &str| !name.to_lowercase().contains("midi through");
    ports
        .iter()
        .position(|name| {
            let lower = name.to_lowercase();
            usable(name)
                && ["ewi", "aerophone", "sylphyo", "nurad", "wind"]
                    .iter()
                    .any(|key| lower.contains(key))
        })
        .or_else(|| ports.iter().position(|name| usable(name)))
}

fn midi_loop(shared: Arc<Shared>) {
    let origin = Instant::now();
    let last_message = Arc::new(AtomicU64::new(0));
    let active_sensing = Arc::new(AtomicBool::new(false));
    let mut connection = None;
    let mut connected_name: Option<String> = None;
    let mut last_scan = Instant::now() - Duration::from_secs(2);
    while !shared.stop.load(Ordering::Relaxed) {
        if active_sensing.load(Ordering::Relaxed)
            && origin.elapsed().as_millis() as u64 > last_message.load(Ordering::Relaxed) + 350
        {
            shared.panic();
            active_sensing.store(false, Ordering::Relaxed);
            shared.midi_status.lock().unwrap().error =
                Some("Controller stopped sending. Start a fresh note to resume.".into());
        }
        if last_scan.elapsed() >= Duration::from_secs(1) {
            last_scan = Instant::now();
            let mut input = match midir::MidiInput::new("Holophonor discovery") {
                Ok(input) => input,
                Err(error) => {
                    connection.take();
                    connected_name = None;
                    shared
                        .midi
                        .store(Performance::default().pack(), Ordering::Relaxed);
                    shared.midi_status.lock().unwrap().error = Some(error.to_string());
                    shared.midi_status.lock().unwrap().connected = None;
                    thread::sleep(Duration::from_millis(50));
                    continue;
                }
            };
            input.ignore(midir::Ignore::None);
            let ports = input.ports();
            let names: Vec<_> = ports
                .iter()
                .map(|port| {
                    input
                        .port_name(port)
                        .unwrap_or_else(|_| "Unnamed MIDI input".into())
                })
                .collect();
            let requested = shared.requested_port.lock().unwrap().clone();
            let selected = choose_port(&names, requested.as_deref());
            let selected_name = selected.map(|i| names[i].clone());
            if connected_name != selected_name {
                // Closing the old connection joins its callback before replacing
                // the snapshot, so a disconnected device cannot revive a note.
                connection.take();
                shared
                    .midi
                    .store(Performance::default().pack(), Ordering::Relaxed);
                active_sensing.store(false, Ordering::Relaxed);
                connected_name = None;
            }
            let mut error = None;
            if connection.is_none()
                && let Some(index) = selected
            {
                let controls = shared.clone();
                let last = last_message.clone();
                let sensing = active_sensing.clone();
                let mut interpreter =
                    Interpreter::new(Profile::unpack(shared.profile.load(Ordering::Relaxed)));
                let mut epoch = shared.panic_epoch.load(Ordering::Relaxed);
                match input.connect(
                    &ports[index],
                    "Holophonor EWI",
                    move |_, message, _| {
                        let profile = Profile::unpack(controls.profile.load(Ordering::Relaxed));
                        let new_epoch = controls.panic_epoch.load(Ordering::Relaxed);
                        if profile != interpreter.profile || new_epoch != epoch {
                            interpreter = Interpreter::new(profile);
                            epoch = new_epoch;
                        }
                        last.store(origin.elapsed().as_millis() as u64, Ordering::Relaxed);
                        if message.first() == Some(&0xfe) {
                            sensing.store(true, Ordering::Relaxed);
                        }
                        let articulation = interpreter.message(message);
                        controls.midi_messages.fetch_add(1, Ordering::Relaxed);
                        controls
                            .midi
                            .store(interpreter.state.pack(), Ordering::Relaxed);
                        if articulation {
                            controls.preview_on.store(false, Ordering::Relaxed);
                            controls.resume(epoch);
                        }
                    },
                    (),
                ) {
                    Ok(opened) => {
                        connection = Some(opened);
                        connected_name = selected_name;
                    }
                    Err(failure) => error = Some(failure.to_string()),
                }
            }
            *shared.midi_status.lock().unwrap() = MidiStatus {
                ports: names,
                connected: connected_name.clone(),
                error,
            };
        }
        thread::sleep(Duration::from_millis(50));
    }
    drop(connection);
    shared
        .midi
        .store(Performance::default().pack(), Ordering::Relaxed);
}

pub struct AudioEngine {
    pub status: String,
    pub sample_rate: u32,
    pub device_name: String,
    stream: Option<cpal::Stream>,
    shared: Arc<Shared>,
    last_check: Instant,
    seen_errors: u32,
}

impl AudioEngine {
    pub fn new(shared: Arc<Shared>) -> Self {
        let mut engine = Self {
            status: "Opening audio…".into(),
            sample_rate: 0,
            device_name: String::new(),
            stream: None,
            shared,
            last_check: Instant::now(),
            seen_errors: 0,
        };
        engine.reopen();
        engine
    }

    pub fn ready(&self) -> bool {
        self.stream.is_some()
    }

    /// Device discovery and stream construction occur on the UI side. Audio
    /// errors signal atomically; the audio callback never formats error text.
    pub fn poll(&mut self) {
        if self.last_check.elapsed() < Duration::from_secs(2) {
            return;
        }
        self.last_check = Instant::now();
        let errors = self.shared.audio_errors.load(Ordering::Relaxed);
        let current = cpal::default_host()
            .default_output_device()
            .and_then(|d| d.description().ok())
            .map(|d| d.name().to_owned());
        if self.stream.is_none()
            || errors != self.seen_errors
            || current.as_deref() != Some(&self.device_name)
        {
            self.reopen();
            self.seen_errors = errors;
        }
    }

    fn reopen(&mut self) {
        self.stream.take();
        self.shared.peak.store(0, Ordering::Relaxed);
        match self.open() {
            Ok(stream) => {
                self.stream = Some(stream);
                self.status = "Audio ready".into();
            }
            Err(error) => {
                self.status = format!("Audio unavailable · {error}");
            }
        }
    }

    fn open(&mut self) -> Result<cpal::Stream, String> {
        let device = cpal::default_host()
            .default_output_device()
            .ok_or("No output device")?;
        self.device_name = device
            .description()
            .map_err(|e| e.to_string())?
            .name()
            .to_owned();
        let supported = device.default_output_config().map_err(|e| e.to_string())?;
        self.sample_rate = supported.sample_rate();
        let mut config = supported.config();
        // Prefer a small supported buffer; fall back to the host's default if
        // fixed buffers are rejected. No claim is made about end-to-end latency.
        if let cpal::SupportedBufferSize::Range { min, max } = *supported.buffer_size() {
            config.buffer_size = cpal::BufferSize::Fixed(256_u32.clamp(min, max));
        }
        let build = |config: &cpal::StreamConfig| match supported.sample_format() {
            cpal::SampleFormat::F32 => build_stream::<f32>(&device, config, self.shared.clone()),
            cpal::SampleFormat::I16 => build_stream::<i16>(&device, config, self.shared.clone()),
            cpal::SampleFormat::U16 => build_stream::<u16>(&device, config, self.shared.clone()),
            format => Err(format!("Unsupported sample format: {format}")),
        };
        let stream = build(&config).or_else(|_| {
            config.buffer_size = cpal::BufferSize::Default;
            build(&config)
        })?;
        stream.play().map_err(|e| e.to_string())?;
        Ok(stream)
    }
}

fn build_stream<T: cpal::SizedSample + cpal::FromSample<f32>>(
    device: &cpal::Device,
    config: &cpal::StreamConfig,
    shared: Arc<Shared>,
) -> Result<cpal::Stream, String> {
    let channels = config.channels as usize;
    let mut synth = Synth::new(config.sample_rate as f32);
    let errors = shared.clone();
    device
        .build_output_stream(
            *config,
            move |output: &mut [T], _| {
                let state = shared.performance();
                let voice = Voice::from_index(shared.voice.load(Ordering::Relaxed));
                let gain = f32::from_bits(shared.gain.load(Ordering::Relaxed));
                let bend = f32::from_bits(shared.bend_range.load(Ordering::Relaxed));
                let muted = shared.is_muted();
                synth.prepare(state, voice, gain, bend, muted);
                let mut peak = 0.0_f32;
                for frame in output.chunks_mut(channels) {
                    let stereo = synth.next_sample();
                    peak = peak.max(stereo[0].abs()).max(stereo[1].abs());
                    for (channel, sample) in frame.iter_mut().enumerate() {
                        let value = if channels == 1 {
                            (stereo[0] + stereo[1]) * 0.5
                        } else {
                            stereo[channel % 2]
                        };
                        *sample = T::from_sample(value);
                    }
                }
                shared.peak.store(peak.to_bits(), Ordering::Relaxed);
                shared
                    .buffer_frames
                    .store((output.len() / channels) as u32, Ordering::Relaxed);
                shared.callbacks.fetch_add(1, Ordering::Relaxed);
            },
            move |_| {
                errors.audio_errors.fetch_add(1, Ordering::Relaxed);
            },
            None,
        )
        .map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn discovery_prefers_wind_controllers_and_respects_explicit_selection() {
        let ports = vec![
            "Midi Through:0".into(),
            "Keyboard".into(),
            "Akai EWI USB".into(),
        ];
        assert_eq!(choose_port(&ports, None), Some(2));
        assert_eq!(choose_port(&ports, Some("Keyboard")), Some(1));
        assert_eq!(choose_port(&ports, Some("Absent device")), None);
        assert_eq!(choose_port(&ports[..1], None), None);
    }
    #[test]
    fn panic_overrides_both_sources() {
        let shared = Shared::default();
        let active = Performance {
            gate: true,
            breath: 1.0,
            ..Performance::default()
        };
        shared.midi.store(active.pack(), Ordering::Relaxed);
        assert!(shared.performance().gate);
        shared.preview.store(active.pack(), Ordering::Relaxed);
        shared.preview_on.store(true, Ordering::Relaxed);
        shared.panic();
        assert!(!shared.performance().gate);
        assert_eq!(shared.performance().breath, 0.0);
    }
}
