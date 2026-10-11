#include "spectral.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kCrossfade = 0.012f;   // seconds either side of the splice
constexpr float kShapeTime = 0.008f;   // how fast timbre follows breath
constexpr float kSettleTime = 0.03f;   // easing from the attack's spectrum to breath's
constexpr float kDriftCents = 3.0f;    // a player never holds pitch perfectly still
constexpr float kPulseNoise = 0.6f;    // breath noise modulated by the reed's opening

// Breath picks a point between the recorded dynamics: 0 is pp, 1 mf, 2 ff.
// The softest breath that sounds fully is pp; mf sits near half breath.
float dynamicOf(float drive)
{
    const float above = std::clamp((drive - 0.12f) / 0.88f, 0.0f, 1.0f);
    return 2.0f * std::pow(above, 0.9f);
}

// Below pp the tone fades out faster than the air does, as on a real horn.
// Above it breath still adds a little level on top of the recorded dynamics,
// so a breath controller has the wide, continuous range players expect.
float toneGain(float drive)
{
    drive = std::max(drive, 0.0f);
    const float onset = std::pow(std::min(1.0f, drive / 0.12f), 1.5f);
    return onset * std::clamp(std::sqrt(drive / 0.55f), 0.45f, 1.35f);
}
float airGain(float drive) { return std::pow(std::min(1.0f, std::max(drive, 0.0f) / 0.12f), 0.6f); }

} // namespace

void SpectralVoice::setSampleRate(float sampleRate)
{
    m_rate = sampleRate;
    // Each band filter's response to unit white noise, measured once per
    // rate, so the profile's noise levels can be reproduced exactly.
    static float calibratedRate = 0.0f;
    static std::array<float, kBands> units{};
    for (int b = 0; b < kBands; ++b) {
        const float lo = Profile::kBandEdges[size_t(b)], hi = Profile::kBandEdges[size_t(b) + 1];
        const float centre = std::sqrt(lo * hi);
        m_bands[size_t(b)].set(std::min(centre, 0.45f * sampleRate), centre / (hi - lo), sampleRate);
    }
    if (calibratedRate != sampleRate) {
        for (int b = 0; b < kBands; ++b) {
            if (Profile::kBandEdges[size_t(b) + 1] > 0.45f * sampleRate) {
                units[size_t(b)] = 0.0f;
                continue;
            }
            dsp::Svf probe = m_bands[size_t(b)];
            dsp::Noise white;
            double energy = 0.0;
            const int samples = int(sampleRate);
            for (int i = 0; i < samples; ++i) {
                const float y = probe.bandpass(white.next() * 1.7320508f);
                energy += double(y) * y;
            }
            units[size_t(b)] = float(std::sqrt(energy / samples));
        }
        calibratedRate = sampleRate;
    }
    m_bandUnit = units;
}

float SpectralVoice::envelope(const ProfileTone &tone, float hz) const
{
    const auto &h = tone.harmonics;
    const float u = hz / tone.f0; // position in the recorded tone's harmonic series
    if (u <= 1.0f)
        return h.front();
    const int count = int(h.size());
    if (u >= float(count))
        return u > float(count) + 1.0f ? 0.0f : h.back() * (float(count) + 1.0f - u);
    const int i = int(u);
    const float t = u - float(i);
    const float a = std::max(h[size_t(i - 1)], 1e-7f), b = std::max(h[size_t(i)], 1e-7f);
    return std::exp(std::log(a) + (std::log(b) - std::log(a)) * t); // straight lines in dB
}

void SpectralVoice::targets(float drive, float hz, float *out) const
{
    const float x = dynamicOf(drive);
    const int lo = std::min(int(x), Profile::kDynamics - 2);
    const float frac = x - float(lo);
    const int dataNote = m_note + m_shift;
    const ProfileTone &a = m_profile->tone(dataNote, lo), &b = m_profile->tone(dataNote, lo + 1);
    // A shifted profile is a bigger instrument: its resonances sit lower, so
    // each harmonic reads the recorded spectrum `shift` semitones higher up.
    const float lookup = std::exp2(float(m_shift) / 12.0f);
    for (int k = 0; k < m_count; ++k) {
        const float f = float(k + 1) * hz * lookup;
        const float ea = std::max(envelope(a, f), 1e-7f), eb = std::max(envelope(b, f), 1e-7f);
        out[k] = std::exp(std::log(ea) + (std::log(eb) - std::log(ea)) * frac);
    }
}

float SpectralVoice::toneRms(const float *amps, int count) const
{
    double sum = 0.0;
    for (int k = 0; k < count; ++k)
        sum += 0.5 * double(amps[k]) * amps[k];
    return float(std::sqrt(sum));
}

void SpectralVoice::start(std::shared_ptr<const Profile> profile, int shift, int note, float velocity, float drive,
                          bool tongued)
{
    m_profile = std::move(profile);
    if (!m_profile)
        return;
    m_shift = shift;
    m_note = note;
    m_active = true;
    m_dip = 1.0f;
    m_burst = 0.0f;
    m_elapsed = 0.0f;
    m_attack = nullptr;
    m_settling = false;

    const float hz = dsp::midiToHz(float(note));
    m_count = std::min(kMaxHarmonics, int(0.45f * m_rate / hz));
    std::array<float, kMaxHarmonics> target{};
    targets(std::max(drive, 0.05f), hz, target.data());
    for (auto &b : m_bands)
        b.reset();

    if (!tongued) {
        // Under a continuing breath (an instrument switch mid-phrase) there is
        // no tongue: the new tone simply swells in.
        for (int k = 0; k < kMaxHarmonics; ++k) {
            const float phase = float(k) * 2.39996f; // golden-angle spread, no peaky sum
            m_re[size_t(k)] = std::cos(phase);
            m_im[size_t(k)] = std::sin(phase);
            m_amp[size_t(k)] = 0.0f;
            m_shape[size_t(k)] = k < m_count ? target[size_t(k)] : 0.0f;
        }
        return;
    }

    // Velocity is how hard the note was tongued: it chooses whose attack
    // starts the note, the soft one, the ordinary one or the accented one.
    const int layer = velocity < 0.35f ? 0 : velocity < 0.85f ? 1 : 2;
    const ProfileTone &tone = m_profile->tone(note + shift, layer);
    m_attack = &tone;
    m_attackPos = 0.0;
    m_attackRate = hz / tone.f0; // also corrects the recording's tuning
    m_splice = m_profile->spliceSeconds / m_attackRate;

    // Scale the recording so it lands at the loudness the breath asks for.
    const float wanted = toneRms(target.data(), m_count) * toneGain(drive);
    const float recorded = toneRms(tone.harmonics.data(), int(tone.harmonics.size()));
    m_attackGain = std::clamp(wanted / std::max(recorded, 1e-6f), 0.05f, 4.0f);

    // Start every harmonic where it must be at the splice, so the hand-over
    // continues the recorded waveform instead of beating against it. The
    // spectrum is held relative to the breath gain at onset, which is kept
    // away from zero so a note tongued before the breath arrives can't blow up.
    const float g = std::max(toneGain(drive), 0.2f);
    for (int k = 0; k < kMaxHarmonics; ++k) {
        const bool recordedHarmonic = k < int(tone.splicePhase.size());
        const float phase = recordedHarmonic
                                ? tone.splicePhase[size_t(k)] - dsp::kTwoPi * float(k + 1) * hz * m_splice
                                : float(k) * 2.39996f;
        m_re[size_t(k)] = std::cos(phase);
        m_im[size_t(k)] = std::sin(phase);
        m_amp[size_t(k)] = 0.0f;
        m_shape[size_t(k)] = recordedHarmonic && k < m_count ? tone.spliceAmp[size_t(k)] * m_attackGain / g : 0.0f;
    }
    m_settling = true;
}

void SpectralVoice::slur(int note)
{
    if (!m_active || note == m_note)
        return;
    const int interval = std::abs(note - m_note);
    m_note = note;
    // The bore takes a moment to settle into the new standing wave: a brief
    // dip in tone and a breath of turbulence, bigger for wider leaps.
    m_dip = 1.0f - 0.3f * std::min(1.0f, float(interval) / 7.0f);
    m_burst = 0.15f + 0.5f * std::min(1.0f, float(interval) / 4.0f);
    // A slur during the attack leaves the recording behind straight away.
    if (m_attack && m_elapsed < m_splice + kCrossfade)
        m_splice = m_elapsed + kCrossfade;
}

void SpectralVoice::render(float *out, int n, const float *drive, const float *pitch)
{
    if (!m_active || !m_profile || n <= 0)
        return;
    const float duration = float(n) / m_rate;
    const float d = drive[n - 1];

    // Slow, small wandering of pitch, as from a living embouchure.
    m_driftTimer -= duration;
    if (m_driftTimer <= 0.0f) {
        m_driftTarget = m_noise.next() * kDriftCents / 100.0f;
        m_driftTimer = 0.08f + 0.12f * (0.5f + 0.5f * m_noise.next());
    }
    m_drift += (m_driftTarget - m_drift) * (1.0f - std::exp(-duration / 0.06f));
    const float hz = dsp::midiToHz(pitch[n / 2] + m_drift);

    m_count = std::min(kMaxHarmonics, int(0.45f * m_rate / hz));
    std::array<float, kMaxHarmonics> target{};
    targets(d, hz, target.data());

    const bool handedOver = !m_attack || m_elapsed > m_splice + kCrossfade;
    if (handedOver) {
        const float alpha = 1.0f - std::exp(-duration / (m_settling ? kSettleTime : kShapeTime));
        for (int k = 0; k < kMaxHarmonics; ++k)
            m_shape[size_t(k)] += ((k < m_count ? target[size_t(k)] : 0.0f) - m_shape[size_t(k)]) * alpha;
        if (m_settling && m_elapsed > m_splice + kCrossfade + 0.15f)
            m_settling = false;
    }
    const float gain = toneGain(d) * m_dip;
    m_dip += (1.0f - m_dip) * (1.0f - std::exp(-duration / 0.02f));

    // Harmonics: each a phasor turned by its own rotation, amplitudes eased
    // linearly across the block.
    float acc[64] = {}, fundamental[64] = {};
    const float w = dsp::kTwoPi * hz / m_rate;
    const float c1 = std::cos(w), s1 = std::sin(w);
    float ck = 1.0f, sk = 0.0f;
    float loudest = 0.0f;
    for (int k = 0; k < kMaxHarmonics; ++k) {
        const float c = ck * c1 - sk * s1, s = ck * s1 + sk * c1;
        ck = c;
        sk = s;
        const float next = k < m_count ? m_shape[size_t(k)] * gain : 0.0f;
        float a = m_amp[size_t(k)];
        if (a == 0.0f && next == 0.0f)
            continue;
        const float step = (next - a) / float(n);
        float re = m_re[size_t(k)], im = m_im[size_t(k)];
        for (int i = 0; i < n; ++i) {
            const float r = re * c - im * s;
            im = re * s + im * c;
            re = r;
            a += step;
            acc[i] += a * im;
            if (k == 0)
                fundamental[i] = re;
        }
        // Keep each phasor on the unit circle despite rounding.
        const float norm = 1.0f / std::sqrt(re * re + im * im);
        m_re[size_t(k)] = re * norm;
        m_im[size_t(k)] = im * norm;
        m_amp[size_t(k)] = next;
        loudest = std::max(loudest, next);
    }

    // Breath noise in twelve bands at the recorded levels.
    {
        const float x = dynamicOf(d);
        const int lo = std::min(int(x), Profile::kDynamics - 2);
        const float frac = x - float(lo);
        const ProfileTone &a = m_profile->tone(m_note + m_shift, lo), &b = m_profile->tone(m_note + m_shift, lo + 1);
        const float air = airGain(d) * (1.0f + 3.0f * m_burst);
        const float alpha = 1.0f - std::exp(-duration / 0.01f);
        for (int band = 0; band < kBands; ++band) {
            float wanted = 0.0f;
            if (m_bandUnit[size_t(band)] > 0.0f) {
                const float na = std::max(a.noise[size_t(band)], 1e-14f), nb = std::max(b.noise[size_t(band)], 1e-14f);
                const float density = std::exp(std::log(na) + (std::log(nb) - std::log(na)) * frac);
                const float width = Profile::kBandEdges[size_t(band) + 1] - Profile::kBandEdges[size_t(band)];
                wanted = std::sqrt(density * width) / m_bandUnit[size_t(band)] * air;
            }
            m_bandGain[size_t(band)] += (wanted - m_bandGain[size_t(band)]) * alpha;
        }
        m_burst *= std::exp(-duration / 0.015f);
    }

    const float fadeStart = m_splice - kCrossfade, fadeLength = 2.0f * kCrossfade;
    for (int i = 0; i < n; ++i) {
        const float white = m_noise.next() * 1.7320508f;
        float air = 0.0f;
        for (int band = 0; band < kBands; ++band)
            if (m_bandGain[size_t(band)] > 0.0f)
                air += m_bands[size_t(band)].bandpass(white) * m_bandGain[size_t(band)];
        air *= 1.0f + kPulseNoise * fundamental[i];

        float synth = acc[i] + air;
        if (m_attack) {
            const float t = m_elapsed + float(i) / m_rate;
            const float blend = std::clamp((t - fadeStart) / fadeLength, 0.0f, 1.0f);
            float recorded = 0.0f;
            const auto &samples = m_attack->attack;
            const size_t at = size_t(m_attackPos);
            if (at + 1 < samples.size()) {
                const float f = float(m_attackPos - double(at));
                recorded = (float(samples[at]) * (1.0f - f) + float(samples[at + 1]) * f) * m_profile->attackGain;
            }
            m_attackPos += m_attackRate;
            synth = synth * blend + recorded * m_attackGain * (1.0f - blend);
        }
        out[i] += synth;
    }
    m_elapsed += duration;
    if (m_attack && m_elapsed > m_splice + kCrossfade)
        m_attack = nullptr;

    m_level = gain;
    if (!m_attack && d < 0.002f && loudest < 1e-5f)
        m_active = false;
}
