#include "synth.h"

#include <algorithm>
#include <cmath>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace {

constexpr int kBlock = 32;
constexpr float kWetGain = 0.8f;
constexpr float kMasterGain = 0.9f;
constexpr float kChorusGain = 0.7f;
constexpr float kBreathSmoothing = 0.006f; // seconds; EWIs send breath in coarse steps
constexpr float kBendSmoothing = 0.004f;
constexpr float kBreathTimeout = 2.0f; // seconds of breath silence before a note counts as keyboard
constexpr float kKillTime = 0.015f;    // fade for voices cut by a preset switch

float readFractional(const float *line, int size, float position)
{
    while (position < 0.0f)
        position += float(size);
    const int i0 = int(position) % size;
    const int i1 = (i0 + 1) % size;
    const float frac = position - std::floor(position);
    return line[i0] + (line[i1] - line[i0]) * frac;
}

} // namespace

Synth::Synth(float sampleRate)
    : m_sampleRate(sampleRate), m_requestedPreset(instruments::defaultIndex()), m_reverb(sampleRate)
{
    for (size_t i = 0; i < m_voices.size(); ++i) {
        m_voices[i].noise.state = 0x9E3779B9u * uint32_t(i + 1);
        m_voices[i].spectral.setSampleRate(sampleRate);
    }
    applyPreset();
}

Synth::Snapshot Synth::snapshot() const
{
    Snapshot s;
    s.pitch = m_outPitch.load(std::memory_order_relaxed);
    s.level = m_outLevel.load(std::memory_order_relaxed);
    s.rms = m_outRms.load(std::memory_order_relaxed);
    s.serial = m_outSerial.load(std::memory_order_relaxed);
    s.note = m_outNote.load(std::memory_order_relaxed);
    s.breathSource = m_outBreathSource.load(std::memory_order_relaxed);
    s.sounding = m_outSounding.load(std::memory_order_relaxed);
    return s;
}

void Synth::render(float *interleaved, int frames)
{
#if defined(__SSE__)
    // Flush denormals: decaying tails would otherwise crawl through the
    // slow path of the FPU just before they fall silent.
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    ControlEvent event;
    while (m_midiQueue.pop(event))
        handle(event);
    while (m_uiQueue.pop(event))
        handle(event);
    if (m_requestedPreset.load(std::memory_order_relaxed) != m_activePreset)
        applyPreset();

    for (int done = 0; done < frames;) {
        const int n = std::min(kBlock, frames - done);
        renderBlock(interleaved + 2 * done, n);
        done += n;
    }
    publish();
}

void Synth::applyPreset()
{
    const auto &catalog = instruments::catalog();
    m_activePreset = m_requestedPreset.load(std::memory_order_relaxed);
    const int index = m_activePreset >= 0 && m_activePreset < int(catalog.size()) ? m_activePreset
                                                                                  : instruments::defaultIndex();
    const bool first = m_preset == nullptr;
    m_preset = &catalog[index];
    if (first)
        return;

    // Fade the old instrument out quickly instead of morphing its voices.
    for (auto &voice : m_voices)
        if (voice.active) {
            voice.killed = true;
            voice.held = voice.sustained = voice.lead = false;
        }
    m_lead = -1;

    // A player who switches instruments mid-phrase keeps sounding.
    if (m_breathSource != NoBreath && m_stackSize > 0 &&
        (m_preset->engine == Engine::Wind || m_preset->engine == Engine::Spectral))
        articulateLead(m_stack[m_stackSize - 1], false, 1.0f, false);
}

void Synth::handle(const ControlEvent &event)
{
    switch (event.type) {
    case ControlEvent::NoteOn:
        if (event.value == 0)
            noteOff(event.number, event.source);
        else
            noteOn(event.number, event.value, event.source);
        break;
    case ControlEvent::NoteOff:
        noteOff(event.number, event.source);
        break;
    case ControlEvent::Controller:
        switch (event.number) {
        case 1:
            m_modWheel = std::clamp(event.value / 127.0f, 0.0f, 1.0f);
            break;
        case 2:
            breath(BreathCc2, event.value);
            break;
        case 5:
            m_portamentoTime = 0.6f * std::pow(std::clamp(event.value / 127.0f, 0.0f, 1.0f), 2.0f);
            break;
        case 65:
            m_portamento = event.value >= 64;
            break;
        case 7:
            breath(BreathCc7, event.value);
            break;
        case 11:
            breath(BreathCc11, event.value);
            break;
        case 64:
            m_sustain = event.value >= 64;
            if (!m_sustain)
                for (auto &voice : m_voices)
                    if (voice.sustained)
                        release(voice);
            break;
        case 120:
        case 123:
            allNotesOff();
            break;
        default:
            break;
        }
        break;
    case ControlEvent::ChannelPressure:
        breath(BreathAftertouch, event.value);
        break;
    case ControlEvent::PitchBend:
        m_bendTarget = std::clamp(event.value / 8192.0f, -1.0f, 1.0f) * float(kBendRange);
        break;
    case ControlEvent::AllNotesOff:
        allNotesOff();
        break;
    }
}

// The first breath-like controller to arrive becomes the breath source. EWIs
// can be set to send CC2, CC11, CC7 or channel aftertouch, so all are heard.
void Synth::breath(BreathSource source, int value)
{
    if (m_breathSource == NoBreath)
        m_breathSource = source;
    if (source != m_breathSource)
        return;
    m_breathTarget = std::clamp(value / 127.0f, 0.0f, 1.0f);
    m_lastBreathAt = m_clock;
}

bool Synth::breathMode(ControlEvent::Source source)
{
    if (source != ControlEvent::Midi || m_breathSource == NoBreath)
        return false;
    // A note with no breath behind it, long after the last breath message,
    // means the breath controller has gone. Envelopes beat silence.
    if (m_breathTarget <= 0.0f && float(m_clock - m_lastBreathAt) > kBreathTimeout * m_sampleRate) {
        m_breathSource = NoBreath;
        return false;
    }
    return true;
}

void Synth::noteOn(int note, int velocity, ControlEvent::Source source)
{
    const bool sustained = m_preset->engine == Engine::Wind || m_preset->engine == Engine::Spectral;
    if (sustained && breathMode(source)) {
        auto end = std::remove(m_stack.begin(), m_stack.begin() + m_stackSize, note);
        m_stackSize = int(end - m_stack.begin());
        if (m_stackSize < int(m_stack.size()))
            m_stack[m_stackSize++] = note;
        const bool legato = m_stackSize > 1 && m_lead >= 0 && m_voices[m_lead].active && m_voices[m_lead].held;
        articulateLead(note, legato, velocity / 127.0f);
        return;
    }
    Voice &voice = allocate(note, source);
    start(voice, note, velocity / 127.0f, source);
    ++m_serial;
    m_lastNote = note;
}

void Synth::noteOff(int note, ControlEvent::Source source)
{
    auto stackEnd = m_stack.begin() + m_stackSize;
    if (source == ControlEvent::Midi && std::find(m_stack.begin(), stackEnd, note) != stackEnd) {
        m_stackSize = int(std::remove(m_stack.begin(), stackEnd, note) - m_stack.begin());
        if (m_lead >= 0) {
            Voice &lead = m_voices[m_lead];
            if (m_stackSize > 0) {
                // Last-note priority: fall back to the newest key still held.
                if (lead.note == note)
                    articulateLead(m_stack[m_stackSize - 1], true);
            } else {
                lead.held = false;
            }
        }
        return;
    }
    for (auto &voice : m_voices)
        if (voice.active && voice.held && !voice.lead && voice.note == note && voice.source == source) {
            if (m_sustain) {
                voice.held = false;
                voice.sustained = true;
            } else {
                release(voice);
            }
        }
}

void Synth::allNotesOff()
{
    m_stackSize = 0;
    m_sustain = false;
    for (auto &voice : m_voices)
        release(voice);
}

void Synth::release(Voice &voice)
{
    voice.held = false;
    voice.sustained = false;
}

void Synth::articulateLead(int note, bool legato, float velocity, bool tongued)
{
    if (legato && m_lead >= 0) {
        // A slur: the same breath, a new fingering. The pitch moves at once
        // (glide only if the player turned portamento on); a measured
        // instrument also gets the bore's brief settling into the new note.
        Voice &lead = m_voices[m_lead];
        lead.note = note;
        lead.target = float(note + lead.preset->transpose);
        lead.held = true;
        if (lead.preset->engine == Engine::Spectral)
            lead.spectral.slur(note + lead.preset->transpose);
    } else {
        Voice *lead = nullptr;
        const bool measured = m_preset->engine == Engine::Spectral;
        if (!measured && m_lead >= 0 && m_voices[m_lead].active && !m_voices[m_lead].killed) {
            // The previous note is still ringing out: re-articulate it so the
            // tail flows into the new note instead of cutting.
            lead = &m_voices[m_lead];
        } else {
            // A tongued note on a measured instrument stops the previous tone
            // the way the tongue stops the reed, then starts with a real attack.
            if (measured && m_lead >= 0 && m_voices[m_lead].active) {
                Voice &previous = m_voices[m_lead];
                previous.killed = true;
                previous.held = previous.lead = false;
            }
            m_lead = -1;
            lead = &allocate(note, ControlEvent::Midi);
            start(*lead, note, velocity, ControlEvent::Midi, tongued);
            m_lead = int(lead - m_voices.data());
        }
        lead->lead = true;
        lead->held = true;
        lead->note = note;
        lead->pitch = lead->target = float(note + lead->preset->transpose);
        lead->age = 0.0f;
    }
    ++m_serial;
    m_lastNote = note;
}

Synth::Voice &Synth::allocate(int note, ControlEvent::Source source)
{
    // A repeated key on a sustained instrument reuses its voice, so the
    // envelope continues instead of stacking a second copy of the note.
    if (m_preset->engine == Engine::Wind)
        for (auto &voice : m_voices)
            if (voice.active && !voice.lead && !voice.killed && voice.note == note && voice.source == source)
                return voice;
    for (auto &voice : m_voices)
        if (!voice.active)
            return voice;

    // Steal the oldest released voice, else the oldest voice; never the lead.
    Voice *best = nullptr;
    for (size_t i = 0; i < m_voices.size(); ++i) {
        Voice &voice = m_voices[i];
        if (int(i) == m_lead)
            continue;
        const bool released = !voice.held && !voice.sustained;
        if (!best) {
            best = &voice;
            continue;
        }
        const bool bestReleased = !best->held && !best->sustained;
        if ((released && !bestReleased) || (released == bestReleased && voice.startedAt < best->startedAt))
            best = &voice;
    }
    return *best;
}

float Synth::glideTime(const Preset &preset) const { return m_portamento ? m_portamentoTime : preset.portamento; }

void Synth::start(Voice &voice, int note, float velocity, ControlEvent::Source source, bool tongued)
{
    const Preset &p = *m_preset;
    const bool continuing = voice.active && voice.preset == &p && p.engine == Engine::Wind;
    if (int(&voice - m_voices.data()) == m_lead)
        m_lead = -1;

    voice.preset = &p;
    voice.active = true;
    voice.held = true;
    voice.sustained = voice.lead = voice.killed = false;
    voice.source = source;
    voice.note = note;
    voice.pitch = voice.target = float(note + p.transpose);
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice.age = 0.0f;
    voice.startedAt = ++m_starts;

    if (!continuing) {
        voice.env = voice.drive = 0.0f;
        voice.filter.reset();
        voice.noiseFilter.reset();
        for (size_t i = 0; i < voice.formants.size(); ++i) {
            voice.formants[i].reset();
            if (p.formants[i].hz > 0.0f)
                voice.formants[i].set(p.formants[i].hz, p.formants[i].q, m_sampleRate);
        }
    }

    if (p.engine == Engine::Pluck) {
        std::fill(voice.line.begin(), voice.line.end(), 0.0f);
        voice.write = 0;
        voice.exciteLeft = int(m_sampleRate / dsp::midiToHz(voice.pitch)) + 1;
        voice.excite = voice.lastSample = 0.0f;
        voice.env = 1.0f;
    } else if (p.engine == Engine::Fm) {
        voice.carrier.phase = voice.modulator.phase = 0.0f;
        voice.exciteLeft = int(0.002f * m_sampleRate); // attack, in samples
        voice.env = 0.0f;
    } else if (p.engine == Engine::Spectral) {
        // How hard the note is attacked. Wind controllers send velocity from
        // the breath at the moment of tonguing (the Aerophone sends the CC2
        // peak), so it is the better measure than breath, which may still be
        // rising; from the keys, velocity is all there is.
        const bool breathing = source == ControlEvent::Midi && m_breathSource != NoBreath;
        const float drive = breathing ? std::max(m_breathTarget, voice.velocity) : 0.35f + 0.65f * voice.velocity;
        voice.env = 0.0f;
        voice.spectral.start(instruments::profile(p.profile), p.profileShift, note + p.transpose, voice.velocity,
                             drive, tongued);
    }
}

float Synth::renderSpectral(Voice &v, const float *breath, const float *bend, float *dry, float *reverb,
                            float *chorus, int n)
{
    const Preset &p = *v.preset;
    const float sr = m_sampleRate;
    const float glide = dsp::smoothing(glideTime(p), sr);
    // The recorded attack shapes the onset, so the gate itself opens at once.
    const float attack = dsp::smoothing(0.002f, sr);
    const float fall = dsp::smoothing(v.killed ? kKillTime : p.release / 3.0f, sr);
    // A breath player makes their own vibrato with the bite sensor (it
    // arrives as pitch bend); only keyboard notes get the preset's, delayed.
    const float delayRamp = std::clamp((v.age - p.vibratoDelay) / 0.4f, 0.0f, 1.0f);
    const float automatic = v.lead ? 0.0f : p.vibratoDepth * delayRamp * m_vibratoScale.load(std::memory_order_relaxed);
    const float depth = automatic + m_modWheel * 0.5f;
    const float vibratoDt = p.vibratoRate / sr;

    float drive[32] = {}, pitch[32] = {}, out[32] = {};
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        v.pitch += (v.target - v.pitch) * glide;
        const float phase = v.vibrato.step(vibratoDt);
        v.vibratoNow = depth != 0.0f ? dsp::sine(phase) * depth : 0.0f;
        const float gate = (v.held || v.sustained) && !v.killed ? 1.0f : 0.0f;
        v.env += (gate - v.env) * (gate > v.env ? attack : fall);
        if (v.killed)
            v.drive *= 1.0f - fall;
        else
            v.drive = v.lead ? breath[i] * v.env : v.env * (0.35f + 0.65f * v.velocity);
        // Vibrato is never pitch alone: the tone swells and darkens with it.
        drive[i] = v.drive * (1.0f + 0.25f * v.vibratoNow);
        pitch[i] = v.pitch + bend[i] + v.vibratoNow;
        peak = std::max(peak, v.drive);
    }
    v.spectral.render(out, n, drive, pitch);
    for (int i = 0; i < n; ++i) {
        const float sample = out[i] * p.level;
        dry[i] += sample;
        reverb[i] += sample * p.reverb;
        chorus[i] += sample * p.chorus;
    }
    v.age += float(n) / sr;

    const bool silent = !v.spectral.active() || (v.killed && v.drive < 1e-4f);
    if (!v.held && !v.sustained && v.env < 1e-4f && silent) {
        v.active = false;
        if (int(&v - m_voices.data()) == m_lead)
            m_lead = -1;
    }
    return peak;
}

float Synth::renderWind(Voice &v, const float *breath, const float *bend, float *dry, float *reverb, float *chorus,
                        int n)
{
    const Preset &p = *v.preset;
    const float sr = m_sampleRate;
    const float glide = dsp::smoothing(glideTime(p), sr);
    const float attack = dsp::smoothing(v.lead ? 0.004f : p.attack, sr);
    const float fall = dsp::smoothing(v.killed ? kKillTime : p.release / 3.0f, sr);
    const float delayRamp = std::clamp((v.age - p.vibratoDelay) / 0.4f, 0.0f, 1.0f);
    // Breath players bring their own vibrato through the bite sensor.
    const float automatic = v.lead ? 0.0f : p.vibratoDepth * delayRamp * m_vibratoScale.load(std::memory_order_relaxed);
    const float depth = automatic + m_modWheel * 0.5f;
    const float vibratoDt = p.vibratoRate / sr;
    const float detune = std::exp2(p.detuneCents / 1200.0f);

    // Filters update at block rate: cheap, and far below the audible zipper rate.
    const float f0 = dsp::midiToHz(v.pitch + bend[0]);
    v.filter.set(std::max(f0 * (p.cutoff + p.breathCutoff * std::pow(v.drive, 1.5f)), 120.0f), p.resonance, sr);
    if (p.noise > 0.0f)
        v.noiseFilter.set(f0 * p.noiseCenter, p.noiseQ, sr);

    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        v.pitch += (v.target - v.pitch) * glide;
        const float vibratoPhase = v.vibrato.step(vibratoDt);
        v.vibratoNow = depth != 0.0f ? dsp::sine(vibratoPhase) * depth : 0.0f;
        const float dt = std::min(dsp::midiToHz(v.pitch + bend[i] + v.vibratoNow) / sr, 0.45f);

        const float gate = (v.held || v.sustained) && !v.killed ? 1.0f : 0.0f;
        v.env += (gate - v.env) * (gate > v.env ? attack : fall);
        if (v.killed)
            v.drive *= 1.0f - fall; // fade from wherever it was, without a jump
        else
            v.drive = v.lead ? breath[i] * v.env : v.env * (0.35f + 0.65f * v.velocity);

        const float phase = v.osc.step(dt);
        float x = 0.0f;
        if (p.saw > 0.0f)
            x += p.saw * dsp::bandlimitedSaw(phase, dt);
        if (p.pulse > 0.0f)
            x += p.pulse * dsp::bandlimitedPulse(phase, dt, p.pulseWidth);
        if (p.sine > 0.0f)
            x += p.sine * dsp::sine(phase);
        if (p.detunedSaw > 0.0f) {
            const float dt2 = std::min(dt * detune, 0.45f);
            x += p.detunedSaw * dsp::bandlimitedSaw(v.osc2.step(dt2), dt2);
        }
        if (p.sub > 0.0f)
            x += p.sub * dsp::sine(v.subOsc.step(dt * 0.5f));
        if (p.noise > 0.0f)
            x += 2.0f * p.noise * v.noiseFilter.bandpass(v.noise.next()) * (0.4f + 0.6f * v.drive);

        float y = v.filter.lowpass(x);
        if (p.formantMix > 0.0f) {
            float formants = 0.0f;
            for (size_t k = 0; k < p.formants.size(); ++k)
                if (p.formants[k].gain > 0.0f)
                    formants += p.formants[k].gain * v.formants[k].bandpass(y);
            y = y * (1.0f - p.formantMix) + formants * p.formantMix * 1.5f;
        }
        if (p.shimmer > 0.0f) {
            // Two slightly mistuned upper partials beat slowly against each
            // other: the glassy, projected quality of the Holophonor.
            const float a = dsp::sine(v.shimmerA.step(std::min(dt * 2.0f * 1.0007f, 0.45f)));
            const float b = dsp::sine(v.shimmerB.step(std::min(dt * 3.0f * 0.9994f, 0.45f)));
            y += p.shimmer * v.drive * (0.5f * a + 0.3f * b);
        }

        const float out = y * std::pow(v.drive, p.breathCurve) * p.level;
        dry[i] += out;
        reverb[i] += out * p.reverb;
        chorus[i] += out * p.chorus;
        peak = std::max(peak, v.drive);
    }
    v.age += float(n) / sr;

    if (!v.held && !v.sustained && v.env < 1e-4f) {
        v.active = false;
        if (int(&v - m_voices.data()) == m_lead)
            m_lead = -1;
    }
    return peak;
}

float Synth::renderPluck(Voice &v, const float *bend, float *dry, float *reverb, float *chorus, int n)
{
    const Preset &p = *v.preset;
    const int size = int(v.line.size());
    // The loop's averaging filter: more weight on the newest sample keeps
    // more of the highs ringing, which is what makes a koto brighter than nylon.
    const float a = 0.5f + 0.4f * p.brightness;
    const float f0 = dsp::midiToHz(v.pitch + bend[0]);
    const float period = std::clamp(m_sampleRate / f0, 4.0f, float(size - 4));
    const float readDelay = period - (1.0f - a); // the filter adds (1 - a) samples
    const bool ringing = (v.held || v.sustained) && !v.killed;
    const float t60 = v.killed ? kKillTime : ringing ? p.decay : p.release;
    v.loopGain = std::pow(0.001f, 1.0f / (t60 * f0));
    const float exciteCoefficient = 0.15f + 0.85f * p.brightness * (0.5f + 0.5f * v.velocity);

    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float x = readFractional(v.line.data(), size, float(v.write) - readDelay);
        const float y = (a * x + (1.0f - a) * v.lastSample) * v.loopGain;
        v.lastSample = x;
        float in = 0.0f;
        if (v.exciteLeft > 0) {
            v.excite += exciteCoefficient * (v.noise.next() - v.excite);
            in = v.excite * v.velocity;
            --v.exciteLeft;
        }
        v.line[v.write] = y + in;
        if (++v.write >= size)
            v.write = 0;

        const float out = x * p.level;
        dry[i] += out;
        reverb[i] += out * p.reverb;
        chorus[i] += out * p.chorus;
        peak = std::max(peak, std::fabs(x));
    }
    v.age += float(n) / m_sampleRate;
    v.env = peak;
    v.drive = std::min(1.0f, peak * 3.0f);
    if (v.exciteLeft == 0 && peak < 1e-4f)
        v.active = false;
    return v.drive;
}

float Synth::renderFm(Voice &v, const float *bend, float *dry, float *reverb, float *chorus, int n)
{
    const Preset &p = *v.preset;
    const float f0 = dsp::midiToHz(v.pitch + bend[0]);
    const float carrierDt = std::min(f0 / m_sampleRate, 0.45f);
    const float modulatorDt = std::min(f0 * p.fmRatio / m_sampleRate, 0.45f);
    const bool ringing = (v.held || v.sustained) && !v.killed;
    const float decay = dsp::decayPerSample(v.killed ? kKillTime : ringing ? p.decay : p.release, m_sampleRate);
    const float attackSamples = 0.002f * m_sampleRate;
    // The modulation index falls faster than the amplitude: a bright strike
    // that mellows, the way a struck tine or bar does.
    const float index = (p.fmSustainIndex + (p.fmIndex - p.fmSustainIndex) *
                                                std::exp(-v.age / std::max(p.fmIndexDecay, 0.001f))) *
                        (0.5f + 0.5f * v.velocity);
    const float gain = (0.3f + 0.7f * v.velocity) * p.level;

    for (int i = 0; i < n; ++i) {
        if (v.exciteLeft > 0) {
            v.env = 1.0f - float(v.exciteLeft) / attackSamples;
            --v.exciteLeft;
        } else {
            v.env *= decay;
        }
        const float modulator = dsp::sine(v.modulator.step(modulatorDt));
        const float out = std::sin(dsp::kTwoPi * v.carrier.step(carrierDt) + index * modulator) * v.env * gain;
        dry[i] += out;
        reverb[i] += out * p.reverb;
        chorus[i] += out * p.chorus;
    }
    v.age += float(n) / m_sampleRate;
    v.drive = v.env * (0.3f + 0.7f * v.velocity);
    if (v.exciteLeft == 0 && v.env < 1e-4f)
        v.active = false;
    return v.drive;
}

void Synth::renderBlock(float *out, int n)
{
    float breath[kBlock], bend[kBlock];
    float dry[kBlock] = {}, reverb[kBlock] = {}, chorus[kBlock] = {};
    const float breathCoefficient = dsp::smoothing(kBreathSmoothing, m_sampleRate);
    const float bendCoefficient = dsp::smoothing(kBendSmoothing, m_sampleRate);
    for (int i = 0; i < n; ++i) {
        m_breath += (m_breathTarget - m_breath) * breathCoefficient;
        m_bend += (m_bendTarget - m_bend) * bendCoefficient;
        breath[i] = m_breath;
        bend[i] = m_bend;
    }

    for (auto &voice : m_voices) {
        if (!voice.active)
            continue;
        switch (voice.preset->engine) {
        case Engine::Spectral:
            renderSpectral(voice, breath, bend, dry, reverb, chorus, n);
            break;
        case Engine::Wind:
            renderWind(voice, breath, bend, dry, reverb, chorus, n);
            break;
        case Engine::Pluck:
            renderPluck(voice, bend, dry, reverb, chorus, n);
            break;
        case Engine::Fm:
            renderFm(voice, bend, dry, reverb, chorus, n);
            break;
        }
    }

    const int chorusSize = int(m_chorusLine.size());
    float energy = 0.0f;
    for (int i = 0; i < n; ++i) {
        // Two slowly drifting taps, one per side, widen the chorus send.
        m_chorusLine[m_chorusWrite] = chorus[i];
        const float delayA = (0.012f + 0.003f * dsp::sine(m_chorusLfoA.step(0.25f / m_sampleRate))) * m_sampleRate;
        const float delayB = (0.014f + 0.0035f * dsp::sine(m_chorusLfoB.step(0.31f / m_sampleRate))) * m_sampleRate;
        const float chorusLeft = readFractional(m_chorusLine.data(), chorusSize, float(m_chorusWrite) - delayA);
        const float chorusRight = readFractional(m_chorusLine.data(), chorusSize, float(m_chorusWrite) - delayB);
        if (++m_chorusWrite >= chorusSize)
            m_chorusWrite = 0;

        float left = dry[i] + chorusLeft * kChorusGain;
        float right = dry[i] + chorusRight * kChorusGain;
        m_reverb.process(reverb[i] * kWetGain, left, right);
        left = dsp::softClip(m_dcLeft.process(left) * kMasterGain);
        right = dsp::softClip(m_dcRight.process(right) * kMasterGain);
        out[2 * i] = left;
        out[2 * i + 1] = right;
        energy += 0.5f * (left * left + right * right);
    }
    const float blockRms = std::sqrt(energy / float(n));
    m_rms += (blockRms - m_rms) * dsp::smoothing(0.05f, m_sampleRate / float(n));
    m_clock += uint64_t(n);
}

void Synth::publish()
{
    const Voice *shown = m_lead >= 0 && m_voices[m_lead].active ? &m_voices[m_lead] : nullptr;
    if (!shown)
        for (const auto &voice : m_voices)
            if (voice.active && !voice.killed && (!shown || voice.startedAt > shown->startedAt))
                shown = &voice;

    float level = 0.0f;
    if (shown) {
        m_lastPitch = shown->pitch - float(shown->preset->transpose) + m_bend + shown->vibratoNow;
        level = shown->drive;
    }
    m_outPitch.store(m_lastPitch, std::memory_order_relaxed);
    m_outLevel.store(level, std::memory_order_relaxed);
    m_outRms.store(m_rms, std::memory_order_relaxed);
    m_outSerial.store(m_serial, std::memory_order_relaxed);
    m_outNote.store(m_lastNote, std::memory_order_relaxed);
    m_outBreathSource.store(m_breathSource, std::memory_order_relaxed);
    m_outSounding.store(shown != nullptr && level > 0.01f, std::memory_order_relaxed);
}
