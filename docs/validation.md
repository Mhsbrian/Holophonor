# Foundation validation

Checked on October 11, 2026, on x86_64 Arch/Omarchy, Rust 1.98, Hyprland 0.56.2, with the desktop's ALSA default output routed through PipeWire. No physical EWI was available for this run.

## Results

| Check | Result |
| --- | --- |
| Release build | `cargo build --release --locked` succeeds; stripped executable approximately 14 MiB |
| Formatting and static checks | `cargo fmt --check`; Clippy with warnings denied |
| Device-independent tests | 12 pass: MIDI parsing, calibration, 7/14-bit breath, pitch bend, sustain, overlapping notes, panic, synthesis silence/release/pitch/output bounds, and canvas bounds/freeze |
| Virtual MIDI integration | Pass: ALSA discovery, note/breath/bend transmission, panic latch, stale callback protection, active sensing timeout, disconnect, and automatic reconnect |
| Local audio integration | Pass: silent stream, 48,000 Hz, actual 256-frame callbacks, 188 callbacks in a one-second test, zero reported stream errors |
| Native interface | Visually inspected captures of idle, playing, both menus, connections, and 760 × 560 layout; Tab/Enter opens both selectors |
| Desktop entry | `desktop-file-validate` passes; shell scripts and PKGBUILD pass `bash -n` |
| License inventory | `cargo about generate --locked --offline --fail --format json` succeeds for Linux; full notices retained in `THIRD_PARTY_LICENSES.md` |
| Offline synthesis | 12 seconds of stereo PCM rendered in 35.19 ms in one release run, including PCM conversion: 0.29% of real time |
| Sound sample | Valid 48 kHz / 16-bit / stereo WAV; all four sections nonzero; overall peak 0.2183, no PCM clipping |

The 256-frame buffer takes 5.33 ms at 48 kHz. This measures callback buffer duration; it excludes MIDI transport, scheduling, the sound server, and converter/device latency. The offline timing measures the voice renderer on this machine, not UI CPU use, latency, or a minimum hardware requirement. Reported stream errors do not provide a complete platform-wide underrun count.

## Reproduce

Run `bin/test` for device-independent checks. Run `cargo test --test linux_devices -- --ignored --test-threads=1 --nocapture` for the optional local device checks. The MIDI test creates its own temporary virtual output, selects it explicitly, and closes it when done. The audio test opens silence for one second.

Create the comparison sample with `cargo run --release --example render_voice -- artifacts/voices.wav`. Each three-second section plays a different voice in this order: Amber reed, Porcelain flute, Velvet cello, Soft halo. This is an offline sample, not an EWI recording.

Capture a native window with `./target/release/holophonor --silent --demo --capture artifacts/canvas.png`. Capture mode seeds a phrase of preview graphics for consistent inspection; device labels still show the actual connection state. No mock hardware status is used. The window closes after the PNG is saved.

## Hardware and listening work still required

Confirm the intended EWI model, CC mapping, connection, audio interface, and comfortable breath range. Test actual breath attacks, legato fingering, sustained passages, vibrato/pitch bend, reconnect behavior, and panic while playing. Measure end-to-end audio latency and jitter, long-session underruns, memory and CPU usage, and graphics frame timing on representative modest hardware.

Listen to the voices through the intended output and compare expressiveness and usefulness. These first voices are original harmonic synthesis; acoustic realism has not been validated. Audio removal/recovery is implemented but only normal local stream startup was exercised. Recording/export, accompaniment, physical modeling, sample libraries, MIDI 2.0/MPE, and generated model imagery are not included in this increment.
