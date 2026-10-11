// The instrument catalog. Each preset is a small set of numbers for one of
// four voice engines. The acoustic winds play profiles measured from real
// recordings (Spectral); the rest are modelled. Every voice responds
// continuously to breath.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

struct Profile;

enum class Engine {
    Spectral, // a real instrument's measured harmonics and breath noise (src/spectral.h)
    Wind,  // band-limited oscillators, breath noise, breath-driven filter and formants
    Pluck, // Karplus–Strong string
    Fm,    // two-operator FM for keys and mallets
};

struct Formant {
    float hz = 0.0f;
    float gain = 0.0f;
    float q = 4.0f;
};

struct Preset {
    std::string id;
    std::string name;
    std::string family;
    std::string tags;
    Engine engine = Engine::Wind;

    int transpose = 0;        // semitones added to the played note
    float portamento = 0.0f;  // seconds; only instruments that glide by nature
    std::string profile;      // Spectral: the measured profile this voice plays
    int profileShift = 0;     // Spectral: play a bigger instrument of the family

    // Wind oscillators, as mix levels.
    float saw = 0.0f, pulse = 0.0f, pulseWidth = 0.5f, sine = 0.0f, sub = 0.0f;
    float detunedSaw = 0.0f, detuneCents = 0.0f;
    float noise = 0.0f, noiseCenter = 2.0f, noiseQ = 1.0f; // centre in multiples of f0

    // Low-pass cutoff in multiples of the fundamental; breath opens it further.
    float cutoff = 4.0f, breathCutoff = 8.0f, resonance = 0.7f;
    std::array<Formant, 3> formants{};
    float formantMix = 0.0f;
    float shimmer = 0.0f; // glassy upper partials, the Holophonor's signature

    float vibratoRate = 5.0f, vibratoDepth = 0.0f, vibratoDelay = 0.3f; // Hz, semitones, s
    float attack = 0.02f, release = 0.12f;                              // seconds
    float breathCurve = 1.3f; // amplitude = drive^curve
    float level = 0.5f;

    // Pluck and FM: T60 of the natural decay and brightness of the excitation.
    float decay = 3.0f, brightness = 0.6f;
    float fmRatio = 1.0f, fmIndex = 1.0f, fmIndexDecay = 0.4f, fmSustainIndex = 0.2f;

    float reverb = 0.25f, chorus = 0.0f; // sends, 0..1
};

namespace instruments {

const std::vector<Preset> &catalog();
int indexOf(const std::string &id); // -1 when unknown
int defaultIndex();

// Profiles are attached once at startup, before audio runs, and only read
// afterwards, so the audio thread can look them up without locking.
void attachProfile(const std::string &id, std::shared_ptr<const Profile> profile);
std::shared_ptr<const Profile> profile(const std::string &id);

} // namespace instruments
