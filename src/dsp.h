// Small, allocation-free DSP building blocks for the synth. Everything here
// runs on the realtime audio thread, so nothing may lock, allocate, or throw.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dsp {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float midiToHz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

// One-pole smoothing coefficient for a time constant in seconds.
inline float smoothing(float seconds, float sampleRate)
{
    if (seconds <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (seconds * sampleRate));
}

// Gain per sample that decays by 60 dB over the given time.
inline float decayPerSample(float t60, float sampleRate)
{
    return std::pow(0.001f, 1.0f / (std::max(t60, 0.001f) * sampleRate));
}

// PolyBLEP residual: subtracting it from a naive saw or pulse removes the
// aliasing a hard edge would otherwise fold back into the audible band.
inline float polyBlep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

struct Phasor {
    float phase = 0.0f;

    // Returns the phase before advancing, so callers read and step in one go.
    float step(float dt)
    {
        const float p = phase;
        phase += dt;
        if (phase >= 1.0f)
            phase -= std::floor(phase);
        return p;
    }
};

inline float bandlimitedSaw(float phase, float dt) { return 2.0f * phase - 1.0f - polyBlep(phase, dt); }

inline float bandlimitedPulse(float phase, float dt, float width)
{
    float second = phase + 1.0f - width;
    if (second >= 1.0f)
        second -= 1.0f;
    const float naive = phase < width ? 1.0f : -1.0f;
    // The pulse's mean is (2w - 1); removing it keeps narrow pulses centred.
    return naive + polyBlep(phase, dt) - polyBlep(second, dt) - (2.0f * width - 1.0f);
}

inline float sine(float phase) { return std::sin(kTwoPi * phase); }

// Topology-preserving state variable filter (Zavalishin / Simper). It stays
// stable while its cutoff is modulated, which breath control does constantly.
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

    void set(float cutoffHz, float q, float sampleRate)
    {
        cutoffHz = std::clamp(cutoffHz, 10.0f, sampleRate * 0.45f);
        const float g = std::tan(kPi * cutoffHz / sampleRate);
        k = 1.0f / std::max(q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() { ic1 = ic2 = 0.0f; }

    // Low-pass and peak-normalised band-pass outputs from one step.
    void process(float in, float &low, float &band)
    {
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = k * v1;
    }

    float lowpass(float in)
    {
        float low, band;
        process(in, low, band);
        return low;
    }

    float bandpass(float in)
    {
        float low, band;
        process(in, low, band);
        return band;
    }
};

struct OnePole {
    float z = 0.0f;
    float process(float in, float coefficient) { return z += coefficient * (in - z); }
};

// Removes DC so asymmetric waveforms and filter drift never push the output
// off centre.
struct DcBlocker {
    float x1 = 0.0f, y1 = 0.0f;
    float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

// xorshift32: deterministic, cheap, and good enough for audio noise.
struct Noise {
    uint32_t state = 0x9E3779B9u;
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) * (2.0f / 4294967295.0f) - 1.0f;
    }
};

// Clean below 0.7, then rounds smoothly into ±1 so loud phrases never clip.
inline float softClip(float x)
{
    const float a = std::fabs(x);
    if (a < 0.7f)
        return x;
    const float shaped = 0.7f + 0.3f * std::tanh((a - 0.7f) / 0.3f);
    return x < 0.0f ? -shaped : shaped;
}

} // namespace dsp
