#![cfg(target_os = "linux")]

use holophonor::{
    engine::{AudioEngine, MidiWorker, Shared},
    midi::Performance,
};
use midir::{MidiOutput, os::unix::VirtualOutput};
use std::{
    sync::{Arc, atomic::Ordering},
    thread,
    time::{Duration, Instant},
};

fn until(mut predicate: impl FnMut() -> bool) {
    let deadline = Instant::now() + Duration::from_secs(5);
    while !predicate() {
        assert!(Instant::now() < deadline, "device condition timed out");
        thread::sleep(Duration::from_millis(10));
    }
}

#[test]
#[ignore = "requires the local ALSA sequencer; creates a temporary virtual controller"]
fn virtual_midi_discovery_expression_panic_disconnect_and_reconnect() {
    let shared = Arc::new(Shared::default());
    let port_name = format!("Holophonor test EWI {}", std::process::id());
    let mut output = MidiOutput::new(&port_name)
        .unwrap()
        .create_virtual(&port_name)
        .unwrap();
    let discovery = midir::MidiInput::new("Holophonor test discovery").unwrap();
    let name = discovery
        .ports()
        .iter()
        .filter_map(|p| discovery.port_name(p).ok())
        .find(|n| n.contains(&port_name))
        .unwrap();
    *shared.requested_port.lock().unwrap() = Some(name.clone());
    let worker = MidiWorker::start(shared.clone());
    until(|| shared.midi_status.lock().unwrap().connected.as_ref() == Some(&name));
    output.send(&[0x90, 69, 100]).unwrap();
    output.send(&[0xb0, 2, 96]).unwrap();
    output.send(&[0xe0, 127, 127]).unwrap();
    until(|| {
        let state = shared.performance();
        state.gate && state.note == 69 && state.breath > 0.7 && state.bend == 1.0
    });
    let stale_epoch = shared.panic_epoch.load(Ordering::SeqCst);
    shared.panic();
    shared.resume(stale_epoch);
    assert!(
        shared.is_muted(),
        "a pre-panic callback must not resume sound"
    );
    output.send(&[0xb0, 2, 127]).unwrap();
    thread::sleep(Duration::from_millis(100));
    assert!(!shared.performance().gate);
    output.send(&[0x90, 72, 100]).unwrap();
    until(|| shared.performance().gate && !shared.is_muted());
    // Active sensing is a disconnect signal only for devices that send it.
    output.send(&[0xfe]).unwrap();
    until(|| shared.is_muted());
    output.send(&[0x90, 74, 100]).unwrap();
    until(|| !shared.is_muted());
    drop(output);
    until(|| shared.midi_status.lock().unwrap().connected.is_none());
    assert!(!shared.performance().gate);
    // ALSA includes its client id in port names; an automatically chosen port
    // is rediscovered even when that id changes after reconnection.
    *shared.requested_port.lock().unwrap() = None;
    let output = MidiOutput::new(&port_name)
        .unwrap()
        .create_virtual(&port_name)
        .unwrap();
    until(|| {
        shared
            .midi_status
            .lock()
            .unwrap()
            .connected
            .as_ref()
            .is_some_and(|n| n.contains(&port_name))
    });
    drop(worker);
    drop(output);
}

#[test]
#[ignore = "requires a local audio output; opens a silent stream for one second"]
fn local_audio_negotiates_and_runs_without_stream_errors() {
    let shared = Arc::new(Shared::default());
    shared
        .midi
        .store(Performance::default().pack(), Ordering::Relaxed);
    let audio = AudioEngine::new(shared.clone());
    assert!(audio.ready(), "{}", audio.status);
    thread::sleep(Duration::from_secs(1));
    assert!(shared.callbacks.load(Ordering::Relaxed) > 1);
    assert!(shared.buffer_frames.load(Ordering::Relaxed) > 0);
    assert_eq!(shared.audio_errors.load(Ordering::Relaxed), 0);
    println!(
        "{} Hz, {} frames, {} callbacks, no stream errors",
        audio.sample_rate,
        shared.buffer_frames.load(Ordering::Relaxed),
        shared.callbacks.load(Ordering::Relaxed)
    );
}
