// Plays a measured Profile as a living tone. A tongued note starts with the
// real recorded attack and hands over, in phase, to a bank of harmonics
// whose levels come from the recordings at pp, mf and ff: breath moves the
// timbre between them continuously, the way blowing harder brightens a real
// horn. Breath noise rides on the reed's own pulse, slurs change note
// without a glide, and a tiny drift in pitch keeps it from sounding machined.
#pragma once

#include <array>
#include <memory>

#include "dsp.h"
#include "profile.h"

class SpectralVoice {
public:
    static constexpr int kMaxHarmonics = 120;
    static constexpr int kBands = 12;

    void setSampleRate(float sampleRate);

    // `shift` plays the profile's tones `shift` semitones higher and slows
    // them back down: a larger instrument of the same family (a tenor from an
    // alto). `drive` and `pitch` are the expression at the moment of attack.
    void start(std::shared_ptr<const Profile> profile, int shift, int note, float velocity, float drive,
               bool tongued);

    // A change of fingering under a continuous breath: no new attack, no glide.
    void slur(int note);

    // Adds `n` samples. `drive` is breath (0..1) and `pitch` the sounding
    // pitch in semitones, bend and vibrato included, per sample.
    void render(float *out, int n, const float *drive, const float *pitch);

    bool active() const { return m_active; }
    float level() const { return m_level; }

private:
    float envelope(const ProfileTone &tone, float hz) const;
    void targets(float drive, float hz, float *out) const;
    float toneRms(const float *amps, int count) const;

    float m_rate = 48000.0f;
    std::array<float, kBands> m_bandUnit{};
    std::array<dsp::Svf, kBands> m_bands;
    std::array<float, kBands> m_bandGain{};

    std::shared_ptr<const Profile> m_profile;
    int m_shift = 0;
    int m_note = 60;
    bool m_active = false;

    std::array<float, kMaxHarmonics> m_re{}, m_im{}, m_amp{}, m_shape{};
    int m_count = 0;

    // The recorded attack, played from the profile and crossfaded away.
    const ProfileTone *m_attack = nullptr;
    double m_attackPos = 0.0;
    float m_attackRate = 1.0f, m_attackGain = 0.0f;
    float m_splice = 0.0f;    // seconds of playback where synthesis takes over
    float m_elapsed = 0.0f;   // seconds since the attack began
    bool m_settling = false;  // harmonics still easing from the attack's spectrum

    float m_dip = 1.0f, m_burst = 0.0f; // the brief instability of a slur
    float m_drift = 0.0f, m_driftTarget = 0.0f, m_driftTimer = 0.0f;
    float m_level = 0.0f;
    dsp::Noise m_noise;
};
