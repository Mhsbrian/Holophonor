// The Holophonor's voice. A breath controller (an EWI) plays one legato lead
// voice whose loudness and brightness follow breath continuously; keyboards
// and the computer keys play polyphonic voices with an envelope instead.
//
// Threads: the MIDI thread and the UI thread each own one input queue; the
// audio thread owns everything else and publishes a snapshot for visuals.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "control.h"
#include "dsp.h"
#include "instruments.h"
#include "reverb.h"

class Synth {
public:
    static constexpr int kMaxVoices = 12;
    static constexpr int kBendRange = 2; // semitones each way, the EWI's bend plates

    enum BreathSource { NoBreath, BreathCc2, BreathCc11, BreathCc7, BreathAftertouch };

    // What the visuals need, published once per audio block.
    struct Snapshot {
        float pitch = 60.0f; // played pitch in semitones, with bend and vibrato
        float level = 0.0f;  // how hard the sounding voice is driven, 0..1
        float rms = 0.0f;    // output loudness
        uint32_t serial = 0; // increments on every new note, legato included
        int note = -1;       // latest played note, -1 before the first
        int breathSource = NoBreath;
        bool sounding = false;
    };

    explicit Synth(float sampleRate = 48000.0f);

    float sampleRate() const { return m_sampleRate; }

    // Producers: exactly one thread each.
    bool pushMidi(const ControlEvent &event) { return m_midiQueue.push(event); }
    bool pushUi(const ControlEvent &event) { return m_uiQueue.push(event); }

    // Any thread. Takes effect at the start of the next block.
    void selectPreset(int index) { m_requestedPreset.store(index, std::memory_order_relaxed); }
    int presetIndex() const { return m_requestedPreset.load(std::memory_order_relaxed); }
    void setVibratoScale(float scale) { m_vibratoScale.store(scale, std::memory_order_relaxed); }

    Snapshot snapshot() const;

    // Audio thread: drains both queues, then renders interleaved stereo.
    void render(float *interleaved, int frames);

    // Audio thread. Public so tests can drive the synth without queues.
    void handle(const ControlEvent &event);

private:
    struct Voice {
        const Preset *preset = nullptr;
        bool active = false;
        bool held = false;
        bool sustained = false; // released while the sustain pedal was down
        bool lead = false;      // the mono breath voice
        bool killed = false;    // fading out fast after a preset switch
        ControlEvent::Source source = ControlEvent::Midi;
        int note = 0;
        float pitch = 60.0f, target = 60.0f; // sounding semitones, transpose included
        float velocity = 0.0f;
        float env = 0.0f;
        float drive = 0.0f;
        float age = 0.0f;
        float vibratoNow = 0.0f; // last vibrato offset, for the snapshot
        uint64_t startedAt = 0;
        dsp::Noise noise;

        dsp::Phasor osc, osc2, subOsc, shimmerA, shimmerB, vibrato;
        dsp::Svf filter, noiseFilter;
        std::array<dsp::Svf, 3> formants;

        // Karplus–Strong string.
        std::array<float, 4096> line{};
        int write = 0;
        int exciteLeft = 0;
        float excite = 0.0f, lastSample = 0.0f, loopGain = 0.999f;

        // FM.
        dsp::Phasor carrier, modulator;
    };

    void noteOn(int note, int velocity, ControlEvent::Source source);
    void noteOff(int note, ControlEvent::Source source);
    void allNotesOff();
    void breath(BreathSource source, int value);
    bool breathMode(ControlEvent::Source source);

    void articulateLead(int note, bool legato);
    Voice &allocate(int note, ControlEvent::Source source);
    void start(Voice &voice, int note, float velocity, ControlEvent::Source source);
    void release(Voice &voice);
    void applyPreset();

    float renderWind(Voice &v, const float *breath, const float *bend, float *dry, float *reverb, float *chorus, int n);
    float renderPluck(Voice &v, const float *bend, float *dry, float *reverb, float *chorus, int n);
    float renderFm(Voice &v, const float *bend, float *dry, float *reverb, float *chorus, int n);
    void renderBlock(float *out, int n);
    void publish();

    float m_sampleRate;
    SpscQueue<ControlEvent, 1024> m_midiQueue;
    SpscQueue<ControlEvent, 1024> m_uiQueue;
    std::atomic<int> m_requestedPreset;
    std::atomic<float> m_vibratoScale{1.0f};

    const Preset *m_preset = nullptr;
    int m_activePreset = -1;
    std::array<Voice, kMaxVoices> m_voices;
    int m_lead = -1;
    std::array<int, 16> m_stack{};
    int m_stackSize = 0;
    uint64_t m_clock = 0;    // samples rendered
    uint64_t m_starts = 0;   // voice starts, for stealing the oldest

    BreathSource m_breathSource = NoBreath;
    float m_breathTarget = 0.0f, m_breath = 0.0f;
    uint64_t m_lastBreathAt = 0;
    float m_bendTarget = 0.0f, m_bend = 0.0f;
    float m_modWheel = 0.0f;
    bool m_sustain = false;

    Reverb m_reverb;
    std::array<float, 4096> m_chorusLine{};
    int m_chorusWrite = 0;
    dsp::Phasor m_chorusLfoA, m_chorusLfoB;
    dsp::DcBlocker m_dcLeft, m_dcRight;
    float m_rms = 0.0f;

    std::atomic<float> m_outPitch{60.0f}, m_outLevel{0.0f}, m_outRms{0.0f};
    std::atomic<uint32_t> m_outSerial{0};
    std::atomic<int> m_outNote{-1}, m_outBreathSource{NoBreath};
    std::atomic<bool> m_outSounding{false};
    uint32_t m_serial = 0;
    int m_lastNote = -1;
    float m_lastPitch = 60.0f;
};
