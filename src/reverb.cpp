#include "reverb.h"

#include <algorithm>

namespace {

// Freeverb's tunings are in samples at 44.1 kHz; the right channel is offset
// by a small spread so the tail decorrelates into a wide stereo image.
constexpr std::array<int, 8> kCombTuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> kAllpassTuning{556, 441, 341, 225};
constexpr int kStereoSpread = 23;
constexpr float kInputGain = 0.015f;
constexpr float kPredelaySeconds = 0.022f;

int scaled(int samples, float sampleRate) { return std::max(1, int(samples * sampleRate / 44100.0f)); }

} // namespace

Reverb::Reverb(float sampleRate)
{
    for (size_t i = 0; i < kCombTuning.size(); ++i) {
        m_combL[i].buffer.assign(scaled(kCombTuning[i], sampleRate), 0.0f);
        m_combR[i].buffer.assign(scaled(kCombTuning[i] + kStereoSpread, sampleRate), 0.0f);
    }
    for (size_t i = 0; i < kAllpassTuning.size(); ++i) {
        m_allpassL[i].buffer.assign(scaled(kAllpassTuning[i], sampleRate), 0.0f);
        m_allpassR[i].buffer.assign(scaled(kAllpassTuning[i] + kStereoSpread, sampleRate), 0.0f);
    }
    // A short predelay keeps the attack of each note clear of its own tail.
    m_predelay.assign(std::max(1, int(kPredelaySeconds * sampleRate)), 0.0f);
    setRoom(0.86f, 0.35f);
}

void Reverb::setRoom(float size, float damping)
{
    m_feedback = 0.7f + 0.28f * std::clamp(size, 0.0f, 1.0f);
    m_damp1 = 0.4f * std::clamp(damping, 0.0f, 1.0f);
    m_damp2 = 1.0f - m_damp1;
}

void Reverb::clear()
{
    for (auto *bank : {&m_combL, &m_combR})
        for (auto &comb : *bank) {
            std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0f);
            comb.store = 0.0f;
        }
    for (auto *bank : {&m_allpassL, &m_allpassR})
        for (auto &allpass : *bank)
            std::fill(allpass.buffer.begin(), allpass.buffer.end(), 0.0f);
    std::fill(m_predelay.begin(), m_predelay.end(), 0.0f);
}

float Reverb::Comb::process(float in, float feedback, float damp1, float damp2)
{
    const float out = buffer[index];
    store = out * damp2 + store * damp1;
    buffer[index] = in + store * feedback;
    if (++index >= int(buffer.size()))
        index = 0;
    return out;
}

float Reverb::Allpass::process(float in)
{
    const float delayed = buffer[index];
    buffer[index] = in + delayed * 0.5f;
    if (++index >= int(buffer.size()))
        index = 0;
    return delayed - in;
}

void Reverb::process(float in, float &left, float &right)
{
    const float delayed = m_predelay[m_predelayIndex];
    m_predelay[m_predelayIndex] = in * kInputGain;
    if (++m_predelayIndex >= int(m_predelay.size()))
        m_predelayIndex = 0;

    float l = 0.0f, r = 0.0f;
    for (size_t i = 0; i < m_combL.size(); ++i) {
        l += m_combL[i].process(delayed, m_feedback, m_damp1, m_damp2);
        r += m_combR[i].process(delayed, m_feedback, m_damp1, m_damp2);
    }
    for (size_t i = 0; i < m_allpassL.size(); ++i) {
        l = m_allpassL[i].process(l);
        r = m_allpassR[i].process(r);
    }
    left += l;
    right += r;
}
