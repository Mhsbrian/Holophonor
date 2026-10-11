// A timbre profile: what a real instrument sounds like, note by note and at
// three dynamics, measured from recordings by tools/profile. The synth plays
// it back as harmonics and breath noise, so pitch, loudness and brightness
// follow the player's breath continuously instead of a recording's.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ProfileTone {
    int note = 0;            // MIDI note as recorded (concert pitch)
    int dynamic = 0;         // 0 pp, 1 mf, 2 ff
    float f0 = 0.0f;         // measured fundamental, Hz
    std::vector<float> harmonics;   // steady-state amplitude of harmonic k+1
    std::vector<float> splicePhase; // phase of each harmonic where the attack hands over
    std::vector<float> spliceAmp;   // amplitude of each harmonic at that point
    std::array<float, 12> noise{};  // breath noise, variance per Hz in each band
    std::vector<int16_t> attack;    // the recorded tongued attack, 48 kHz
};

struct Profile {
    static constexpr int kDynamics = 3;
    static constexpr std::array<float, 13> kBandEdges{100, 200, 400, 700, 1000, 1500, 2200,
                                                      3200, 4500, 6500, 9000, 12500, 18000};

    std::string name;
    float spliceSeconds = 0.09f; // where attacks hand over to synthesis
    float attackGain = 1.0f / 32767.0f; // attack sample value = int16 * attackGain
    int lowNote = 0, highNote = 0;
    std::vector<ProfileTone> tones; // indexed by (note - lowNote) * 3 + dynamic

    const ProfileTone &tone(int note, int dynamic) const;

    // Parses the binary format written by tools/profile. Returns null on a
    // malformed buffer.
    static std::shared_ptr<const Profile> parse(const uint8_t *data, size_t size);
    std::vector<uint8_t> serialize() const;
};
