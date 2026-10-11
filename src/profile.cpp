#include "profile.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr char kMagic[4] = {'H', 'P', 'R', '1'};

class Writer {
public:
    void bytes(const void *p, size_t n)
    {
        const auto *b = static_cast<const uint8_t *>(p);
        out.insert(out.end(), b, b + n);
    }
    template <typename T> void value(T v) { bytes(&v, sizeof v); }
    template <typename T> void array(const std::vector<T> &v)
    {
        value<uint32_t>(uint32_t(v.size()));
        bytes(v.data(), v.size() * sizeof(T));
    }
    std::vector<uint8_t> out;
};

class Reader {
public:
    Reader(const uint8_t *data, size_t size) : m_data(data), m_size(size) {}
    bool bytes(void *p, size_t n)
    {
        if (m_at + n > m_size)
            return false;
        std::memcpy(p, m_data + m_at, n);
        m_at += n;
        return true;
    }
    template <typename T> bool value(T &v) { return bytes(&v, sizeof v); }
    template <typename T> bool array(std::vector<T> &v, uint32_t limit)
    {
        uint32_t n = 0;
        if (!value(n) || n > limit)
            return false;
        v.resize(n);
        return bytes(v.data(), n * sizeof(T));
    }

private:
    const uint8_t *m_data;
    size_t m_size;
    size_t m_at = 0;
};

} // namespace

const ProfileTone &Profile::tone(int note, int dynamic) const
{
    note = std::clamp(note, lowNote, highNote);
    dynamic = std::clamp(dynamic, 0, kDynamics - 1);
    return tones[size_t((note - lowNote) * kDynamics + dynamic)];
}

std::vector<uint8_t> Profile::serialize() const
{
    Writer w;
    w.bytes(kMagic, 4);
    w.value<uint32_t>(uint32_t(name.size()));
    w.bytes(name.data(), name.size());
    w.value(spliceSeconds);
    w.value(attackGain);
    w.value<int32_t>(lowNote);
    w.value<int32_t>(highNote);
    for (const ProfileTone &t : tones) {
        w.value<int32_t>(t.note);
        w.value<int32_t>(t.dynamic);
        w.value(t.f0);
        w.array(t.harmonics);
        w.array(t.splicePhase);
        w.array(t.spliceAmp);
        w.bytes(t.noise.data(), sizeof t.noise);
        w.array(t.attack);
    }
    return w.out;
}

std::shared_ptr<const Profile> Profile::parse(const uint8_t *data, size_t size)
{
    Reader r(data, size);
    char magic[4];
    if (!r.bytes(magic, 4) || std::memcmp(magic, kMagic, 4) != 0)
        return nullptr;
    auto profile = std::make_shared<Profile>();
    uint32_t nameSize = 0;
    if (!r.value(nameSize) || nameSize > 256)
        return nullptr;
    profile->name.resize(nameSize);
    if (!r.bytes(profile->name.data(), nameSize) || !r.value(profile->spliceSeconds) ||
        !r.value(profile->attackGain) || !r.value(profile->lowNote) || !r.value(profile->highNote))
        return nullptr;
    if (profile->lowNote < 0 || profile->highNote > 127 || profile->highNote < profile->lowNote)
        return nullptr;
    const int count = (profile->highNote - profile->lowNote + 1) * kDynamics;
    profile->tones.resize(size_t(count));
    for (ProfileTone &t : profile->tones) {
        if (!r.value(t.note) || !r.value(t.dynamic) || !r.value(t.f0) || !r.array(t.harmonics, 512) ||
            !r.array(t.splicePhase, 512) || !r.array(t.spliceAmp, 512) || !r.bytes(t.noise.data(), sizeof t.noise) ||
            !r.array(t.attack, 48000))
            return nullptr;
        if (t.f0 <= 0.0f || t.splicePhase.size() != t.harmonics.size() || t.spliceAmp.size() != t.harmonics.size())
            return nullptr;
    }
    return profile;
}
