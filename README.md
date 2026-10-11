# Holophonor

Breathe into an EWI and paint with sound.

A sheet of raw xuan paper fills the window. Plug in an EWI and it's found
straight away. Breathe, and the instrument sounds while a brush writes the
melody across the sheet. You choose the instrument at the top left and how
the music is drawn at the top right. That's all the interface there is.

```
┌──────────────────────────────────────────────────────────────────────┐
│ SIGNATURE                                                     VISUAL │
│ Holophonor ⌄                                                  ⌄ Sumi │
│                                                                      │
│                          ╭─╮                                         │
│            ╭──╮        ╭─╯ ╰──╮                                      │
│   ━━━━━━━━╯  ╰──    ━━╯      ╰━━━━            ▣  ← the seal, once a  │
│                                                    phrase settles    │
│                                                                      │
│ A4  +03¢  ━━━━━━──  breath · cc2                    ● EWI USB        │
└──────────────────────────────────────────────────────────────────────┘
```

Built for Arch Linux and Omarchy: C++ and Qt Quick, PipeWire for sound, the
ALSA sequencer for MIDI, and the Omarchy theme for its colours.

## Run it

```sh
bin/build                   # qmake6 + make → build/holophonor
build/holophonor --demo     # hear and see a short phrase without an EWI
bin/test                    # the test suite, offscreen
bin/install                 # makepkg -fsi: installs it into the app launcher
```

You need `qt6-base`, `qt6-declarative`, `qt6-wayland`, `libpipewire` and
`alsa-lib`, all of which Omarchy already has. The fonts are Noto Serif
Display, iA Writer Mono and Noto Serif CJK (for the seal). If one is missing,
Qt substitutes another font.

## Plug in an EWI

Any class-compliant USB MIDI device is connected as soon as the kernel
creates its port. That covers the Akai EWI USB, EWI4000s and EWI5000 over USB,
as well as most keyboards and interfaces. Names containing "EWI" get the
accent dot and are preferred. You don't need to connect anything by hand.

Breath is read from **CC2, CC11, CC7 or channel aftertouch**, whichever
arrives first, because EWIs can be set to send any of them. If notes arrive
with no breath behind them, the synth falls back to envelopes. That happens
when the EWI is unplugged and a keyboard takes over.

| EWI gesture       | MIDI it sends            | What Holophonor does                        |
|-------------------|--------------------------|---------------------------------------------|
| Breath            | CC2 / CC11 / CC7 / AT    | Loudness and brightness, continuously       |
| Fingering         | Note on / off            | One legato voice, last-note priority, glide |
| Bend plates       | Pitch bend               | ±2 semitones                                |
| Bite / mod        | CC1                      | Extra vibrato                               |

## Play without one

When no menu is open, the computer keyboard plays too:

```
 W E   T Y U   O P        black keys
A S D F G H J K L ; '     C D E F G A B C D E F   (A = middle C)
Z / X  octave down / up
```

Keyboard notes are polyphonic and use an envelope, and they still play while
an EWI is connected.

## Visuals

| Key | Mode       | What it draws                                                              |
|-----|------------|----------------------------------------------------------------------------|
| 1   | Sumi       | One ink brush: pitch is height, breath is weight, rests leave space, and a vermilion seal is stamped when a phrase settles |
| 2   | Bloom      | A watercolour pool for each note. The circle of fifths picks the pigment from your Omarchy theme, and the melody walks the brush |
| 3   | Graphite   | An engraved score line, with breath as hatching and every note named      |
| 4   | Projection | The Holophonor's light: a damped harmonograph of the interval from the phrase's first note, drawn in your theme's accent |

Press `Ctrl S` to save the painting to `~/Pictures/Holophonor/`. `Space`
starts a fresh sheet. Long performances sink into the paper as faint ghosts,
so they never turn into a smear.

## Instruments

28 voices, all modelled rather than sampled. Each one responds to breath
continuously:

- **Signature**: Holophonor, an oboe core with glassy upper partials that beat slowly against each other
- **Woodwind**: Oboe, Clarinet, Flute, Alto Sax, Tenor Sax, Bassoon
- **Brass**: Trumpet, French Horn, Trombone
- **Strings**: Violin, Cello, String Ensemble
- **Voice**: Choir Aah, Whistle
- **World Winds**: Shakuhachi, Pan Flute, Ocarina
- **Synth**: Analog Lead, Glass Pad, Sub Bass, Aether
- **Plucked**: Harp, Nylon Guitar, Koto (Karplus–Strong)
- **Keys & Mallets**: Electric Piano, Glass Bells, Marimba (two-operator FM)

Search matches names, families, tags and initials, so "reed", "brass" and
"frhn" all work.

## Shortcuts

| Key               | Action                                  |
|-------------------|-----------------------------------------|
| `/` or `Ctrl K`   | Search instruments                      |
| `[` `]`           | Previous / next instrument              |
| `V`               | Choose a visual                         |
| `1` – `4`         | Sumi · Bloom · Graphite · Projection    |
| `Space`           | A fresh sheet                           |
| `Ctrl S`          | Save the painting                       |
| `Ctrl D`          | Play the demo phrase                    |
| `A W S E D … K`   | Play from the keyboard                  |
| `Z` `X`           | Octave down / up                        |
| `Esc`             | Silence everything                      |
| `F11`             | Full screen                             |
| `?`               | Show every shortcut                     |
| `Ctrl Q`          | Quit                                    |

Quit is `Ctrl Q` rather than Omarchy's usual `Q`. Q sits right next to the
playing keys, and a stray keypress shouldn't end a performance.

## Command line

```
holophonor [--instrument <id>] [--visual sumi|bloom|graphite|projection]
           [--demo] [--silent] [--size WxH] [--screenshot <png> --after <ms>]
```

`--silent` runs the synth without sending sound to PipeWire. Combined with
`QT_QPA_PLATFORM=offscreen` and `--screenshot`, it renders the app headless.

## How it's built

```
 EWI ─USB─▶ ALSA sequencer ──▶ MidiInput thread ──┐  lock-free SPSC queues
                                                   ├──▶ Synth (PipeWire RT thread)
 keyboard / demo ─────────────▶ UI thread ─────────┘      │ 128 frames · 48 kHz
                                                          ├──▶ PipeWire ──▶ speakers
                                                          ▼
                                         atomic snapshot (pitch, breath, note)
                                                          │ 60 Hz
                                                          ▼
                              InkPainter (QImage) ──▶ InkCanvas (scene graph texture)
```

- `src/synth.*` holds the voices, the breath and legato logic, and a chorus and
  Freeverb-style reverb. Nothing on the audio thread locks or allocates.
- `src/instruments.*` is the catalog: a set of numbers per preset for the Wind,
  Pluck and FM engines.
- `src/midiinput.*` uses the ALSA sequencer. It subscribes to `System:Announce`
  so hot-plugging works, and its auto-connect policy is kernel clients plus
  anything named EWI.
- `src/audioengine.*` is the PipeWire stream. If PipeWire is unreachable it
  falls back to a silent clock and the window says why.
- `src/inkpainter.*` and `src/paper.*` hold the four visual modes and the
  generated paper.
- `src/*.qml` is the window, the two menus and the shortcut card.

## Tests

`bin/test` runs 109 cases offscreen. It covers:

- Every instrument renders finite, unclipped and audible output.
- Every harmonic instrument is in tune within 12 cents at A4 and A5 (YIN).
- Breath shapes loudness, legato falls back to the held note, and pitch bend
  covers ±2 semitones.
- Keys play while an EWI is latched, and a silent breath source falls back to
  envelopes.
- A virtual "EWI Test Rig" hot-plugged on the real ALSA sequencer is
  connected, heard and released.
- Instrument search works.
- Painting puts ink where the pitch sits, and a fresh sheet and Projection's
  phosphor both return exactly to their ground.
- The theme is followed.
