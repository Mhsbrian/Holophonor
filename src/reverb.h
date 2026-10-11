// A Schroeder–Moorer reverb after Jezar's public-domain Freeverb: eight
// damped combs into four allpasses per channel. Buffers are sized once in the
// constructor, so processing never allocates.
#pragma once

#include <array>
#include <vector>

class Reverb {
public:
    explicit Reverb(float sampleRate);

    void setRoom(float size, float damping);
    void clear();

    // Adds the wet signal for `in` to `left` and `right`.
    void process(float in, float &left, float &right);

private:
    struct Comb {
        std::vector<float> buffer;
        int index = 0;
        float store = 0.0f;
        float process(float in, float feedback, float damp1, float damp2);
    };
    struct Allpass {
        std::vector<float> buffer;
        int index = 0;
        float process(float in);
    };

    std::array<Comb, 8> m_combL, m_combR;
    std::array<Allpass, 4> m_allpassL, m_allpassR;
    std::vector<float> m_predelay;
    int m_predelayIndex = 0;
    float m_feedback = 0.84f;
    float m_damp1 = 0.2f, m_damp2 = 0.8f;
};
