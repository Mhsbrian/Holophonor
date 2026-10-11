// Measures a timbre profile from chromatic-scale recordings.
//
//   profile <name> <out.prof> <file.f32> <dynamic 0|1|2> <first note> <last note> [...]
//
// Each input is 48 kHz mono float32 holding one note per held tone, rising
// chromatically from <first note> to <last note>, as the University of Iowa
// Musical Instrument Samples are recorded. For every note it finds the tone,
// measures its pitch, the steady amplitude of each harmonic, the breath noise
// between the harmonics, and keeps the first moments as the tongued attack.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../../src/profile.h"

namespace {

constexpr double kRate = 48000.0;
constexpr int kFrame = 4096;
constexpr double kAttackSeconds = 0.12;
constexpr double kSpliceSeconds = 0.09;
constexpr int kMaxHarmonics = 120;
constexpr double kMaxHarmonicHz = 16000.0;
// Attacks are stored 12 dB below full scale, leaving room for the level
// smoothing and dynamic reshaping below to raise them without clipping.
constexpr double kAttackHeadroom = 0.25;

double midiToHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

std::vector<float> readF32(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<float> samples;
    float v;
    while (in.read(reinterpret_cast<char *>(&v), sizeof v))
        samples.push_back(v);
    return samples;
}

void fft(std::vector<std::complex<double>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * M_PI / double(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

// YIN, searching only near the expected pitch so octave errors can't win.
double pitch(const std::vector<float> &x, size_t start, int window, double expected)
{
    const int minLag = int(kRate / (expected * 1.06)), maxLag = int(kRate / (expected / 1.06)) + 1;
    if (start + size_t(window + maxLag + 2) > x.size())
        return 0.0;
    std::vector<double> d(size_t(maxLag) + 2, 0.0);
    for (int tau = 1; tau <= maxLag + 1; ++tau) {
        double s = 0.0;
        for (int j = 0; j < window; ++j) {
            const double diff = x[start + size_t(j)] - x[start + size_t(j + tau)];
            s += diff * diff;
        }
        d[size_t(tau)] = s;
    }
    int best = minLag;
    for (int tau = minLag; tau <= maxLag; ++tau)
        if (d[size_t(tau)] < d[size_t(best)])
            best = tau;
    const double a = d[size_t(best - 1)], b = d[size_t(best)], c = d[size_t(best + 1)];
    const double shift = (a - 2 * b + c) != 0.0 ? std::clamp(0.5 * (a - c) / (a - 2 * b + c), -1.0, 1.0) : 0.0;
    const double f0 = kRate / (best + shift);
    return std::fabs(std::log2(f0 / expected)) < 0.08 ? f0 : 0.0;
}

// Amplitude and phase of a sinusoid at `hz`, Hann-windowed around `centre`.
std::complex<double> partial(const std::vector<float> &x, double centre, int length, double hz)
{
    const long start = long(centre) - length / 2;
    std::complex<double> sum(0.0);
    double weight = 0.0;
    for (int n = 0; n < length; ++n) {
        const long i = start + n;
        if (i < 0 || i >= long(x.size()))
            continue;
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * n / length);
        const double t = double(i) - centre;
        sum += double(x[size_t(i)]) * w * std::polar(1.0, -2.0 * M_PI * hz * t / kRate);
        weight += w;
    }
    // A sine of amplitude A at the centre reads as (A/2) * weight, phase
    // relative to cosine; shift by 90 degrees to report the sine phase.
    return weight > 0.0 ? sum * (2.0 / weight) * std::complex<double>(0.0, 1.0) : 0.0;
}

struct Segment {
    size_t onset = 0, end = 0;
};

std::vector<Segment> segment(const std::vector<float> &x, double fraction, size_t gapFrames)
{
    const size_t hop = size_t(kRate * 0.01);
    std::vector<double> db;
    for (size_t i = 0; i + hop <= x.size(); i += hop) {
        double e = 0.0;
        for (size_t j = 0; j < hop; ++j)
            e += double(x[i + j]) * x[i + j];
        db.push_back(10.0 * std::log10(e / double(hop) + 1e-12));
    }
    std::vector<double> sorted = db;
    std::sort(sorted.begin(), sorted.end());
    // Scales played almost legato never reach silence; then the gaps are only
    // dips, so the floor is taken well below the playing level instead.
    const double peak = sorted[sorted.size() * 99 / 100];
    const double floor = std::min(sorted[sorted.size() / 10], peak - 30.0);
    const double threshold = floor + std::max(12.0, (peak - floor) * fraction);

    std::vector<Segment> segments;
    bool inside = false;
    size_t start = 0, below = 0;
    for (size_t f = 0; f < db.size(); ++f) {
        if (db[f] > threshold) {
            if (!inside) {
                inside = true;
                start = f;
            }
            below = 0;
        } else if (inside && ++below > gapFrames) { // enough quiet ends a tone
            inside = false;
            if ((f - below - start) * hop > size_t(kRate * 0.3))
                segments.push_back({start * hop, (f - below) * hop});
        }
    }
    if (inside && (db.size() - start) * hop > size_t(kRate * 0.3))
        segments.push_back({start * hop, db.size() * hop});
    return segments;
}

// The sample where the attack really begins: the first that rises above a
// tenth of the tone's steady level, found within the coarse onset frame.
size_t refineOnset(const std::vector<float> &x, const Segment &s, double steadyRms)
{
    const size_t from = s.onset > 960 ? s.onset - 960 : 0;
    for (size_t i = from; i < s.end; ++i)
        if (std::fabs(x[i]) > 0.1 * steadyRms * std::sqrt(2.0))
            return i > 96 ? i - 96 : 0; // 2 ms of lead-in
    return s.onset;
}

double median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

ProfileTone measure(const std::vector<float> &x, const Segment &s, int note, int dynamic, double unitNoise)
{
    ProfileTone tone;
    tone.note = note;
    tone.dynamic = dynamic;
    const double expected = midiToHz(note);

    const double length = double(s.end - s.onset);
    double from = double(s.onset) + std::min(0.25 * kRate, length * 0.3);
    double to = double(s.end) - std::min(0.15 * kRate, length * 0.2);
    if (to - from < kFrame)
        from = double(s.onset) + length * 0.25, to = double(s.end) - length * 0.25;

    std::vector<double> pitches;
    std::vector<std::vector<double>> amps(kMaxHarmonics);
    std::array<std::vector<double>, 12> noise;
    double rms = 0.0;
    int frames = 0;
    for (double c = from + kFrame / 2; c + kFrame / 2 < to; c += 0.04 * kRate) {
        const double f0 = pitch(x, size_t(c) - kFrame / 4, kFrame / 2, expected);
        if (f0 <= 0.0)
            continue;
        pitches.push_back(f0);
        for (int k = 1; k <= kMaxHarmonics && k * f0 < kMaxHarmonicHz; ++k)
            amps[size_t(k - 1)].push_back(std::abs(partial(x, c, kFrame, k * f0)));

        // Breath noise: the spectrum with every harmonic's neighbourhood removed.
        std::vector<std::complex<double>> spectrum(kFrame);
        double windowPower = 0.0;
        for (int n = 0; n < kFrame; ++n) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * n / kFrame);
            spectrum[size_t(n)] = x[size_t(c) - kFrame / 2 + size_t(n)] * w;
            windowPower += w * w;
            rms += double(x[size_t(c) - kFrame / 2 + size_t(n)]) * x[size_t(c) - kFrame / 2 + size_t(n)];
        }
        fft(spectrum);
        for (int b = 0; b < 12; ++b) {
            double sum = 0.0;
            int bins = 0;
            for (int k = 1; k < kFrame / 2; ++k) {
                const double hz = k * kRate / kFrame;
                if (hz < Profile::kBandEdges[size_t(b)] || hz >= Profile::kBandEdges[size_t(b) + 1])
                    continue;
                const double nearest = std::round(hz / f0) * f0;
                if (std::fabs(hz - nearest) < 0.3 * f0 + 30.0)
                    continue;
                sum += std::norm(spectrum[size_t(k)]) / windowPower;
                ++bins;
            }
            if (bins > 0)
                noise[size_t(b)].push_back(sum / bins * unitNoise);
        }
        ++frames;
    }
    tone.f0 = float(median(pitches));
    for (int k = 0; k < kMaxHarmonics && (k + 1) * tone.f0 < kMaxHarmonicHz; ++k) {
        // A harmonic measured in only a few frames is a measurement accident.
        if (amps[size_t(k)].size() * 2 < pitches.size())
            break;
        tone.harmonics.push_back(float(median(amps[size_t(k)])));
    }
    for (int b = 0; b < 12; ++b)
        tone.noise[size_t(b)] = float(median(noise[size_t(b)]));

    // The attack: from the true onset, long enough to cover the tongue's
    // release and the reed settling into its steady vibration.
    const double steadyRms = frames > 0 ? std::sqrt(rms / (frames * double(kFrame))) : 0.0;
    const size_t onset = refineOnset(x, s, steadyRms);
    const size_t attackLength = size_t(kAttackSeconds * kRate);
    for (size_t i = 0; i < attackLength && onset + i < x.size(); ++i)
        tone.attack.push_back(
            int16_t(std::clamp(double(x[onset + i]) * 32767.0 * kAttackHeadroom, -32767.0, 32767.0)));

    // Phases and amplitudes where synthesis takes over from the recording,
    // so the hand-over continues the same waveform.
    const double splice = double(onset) + kSpliceSeconds * kRate;
    const int window = std::max(int(4.0 * kRate / tone.f0), 512);
    for (size_t k = 0; k < tone.harmonics.size(); ++k) {
        const auto p = partial(x, splice, window, (double(k) + 1.0) * tone.f0);
        tone.splicePhase.push_back(float(std::arg(p)));
        tone.spliceAmp.push_back(float(std::abs(p)));
    }
    return tone;
}

// What the noise measurement reads for unit-variance white noise, so band
// values come out as variance per hertz.
double calibrateNoise()
{
    std::vector<float> white(size_t(kRate * 2));
    uint32_t state = 1;
    for (float &v : white) {
        state = state * 1664525u + 1013904223u;
        v = float((double(state) / 4294967296.0 - 0.5) * std::sqrt(12.0));
    }
    std::vector<std::complex<double>> spectrum(kFrame);
    double windowPower = 0.0, sum = 0.0;
    int bins = 0;
    for (int frame = 0; frame < 16; ++frame) {
        for (int n = 0; n < kFrame; ++n) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * n / kFrame);
            spectrum[size_t(n)] = white[size_t(frame * kFrame + n)] * w;
            if (frame == 0)
                windowPower += w * w;
        }
        fft(spectrum);
        for (int k = 1; k < kFrame / 2; ++k) {
            sum += std::norm(spectrum[size_t(k)]) / windowPower;
            ++bins;
        }
    }
    const double reading = sum / bins;     // for variance 1 spread over 0..rate/2
    return (2.0 / kRate) / reading;        // scales a reading to variance per Hz
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 9 || (argc - 3) % 4 != 0) {
        std::fprintf(stderr, "usage: profile <name> <out.prof> <file.f32> <dynamic> <first> <last> ...\n");
        return 2;
    }
    const double unitNoise = calibrateNoise();
    std::map<std::pair<int, int>, ProfileTone> tones;
    int low = 127, high = 0;
    for (int i = 3; i < argc; i += 4) {
        const std::vector<float> x = readF32(argv[i]);
        const int dynamic = std::atoi(argv[i + 1]), first = std::atoi(argv[i + 2]), last = std::atoi(argv[i + 3]);
        // Recordings differ in noise floor and in how long the rests are;
        // try a few thresholds until the tones and the file's range agree.
        const int wanted = last - first + 1;
        std::vector<Segment> segments;
        for (size_t gap : {8, 4, 2})
            for (double fraction : {0.3, 0.2, 0.4, 0.15, 0.5, 0.25, 0.35, 0.6}) {
                if (int(segments.size()) == wanted)
                    break;
                segments = segment(x, fraction, gap);
            }
        if (int(segments.size()) != wanted) {
            std::fprintf(stderr, "%s: found %zu tones, expected %d; skipping the file\n", argv[i], segments.size(),
                         wanted);
            continue;
        }
        for (int n = 0; n < wanted; ++n) {
            if (tones.count({first + n, dynamic}))
                continue; // an earlier file already has this tone
            ProfileTone tone = measure(x, segments[size_t(n)], first + n, dynamic, unitNoise);
            const double cents = 1200.0 * std::log2(tone.f0 / midiToHz(first + n));
            std::fprintf(stderr, "%s note %d dyn %d: f0 %.2f Hz (%+.0f c), %zu harmonics\n", argv[i], first + n,
                         dynamic, tone.f0, cents, tone.harmonics.size());
            if (!(std::fabs(cents) <= 60.0) || tone.harmonics.empty()) {
                std::fprintf(stderr, "  not the expected note; skipped\n");
                continue;
            }
            tones[{first + n, dynamic}] = std::move(tone);
            low = std::min(low, first + n);
            high = std::max(high, first + n);
        }
    }
    if (tones.empty()) {
        std::fprintf(stderr, "no tones measured\n");
        return 1;
    }

    Profile profile;
    profile.name = argv[1];
    profile.spliceSeconds = float(kSpliceSeconds);
    profile.lowNote = low;
    profile.highNote = high;
    // How this instrument's dynamics differ, harmonic by harmonic, learned
    // from the notes recorded at both: a gap filled from another dynamic gets
    // that difference, so a missing pp is softer and darker, not a copy of mf.
    constexpr int kLearned = 40;
    auto difference = [&](int to, int from) {
        std::vector<double> curve(kLearned, 0.0);
        std::vector<std::vector<double>> samples(kLearned);
        std::vector<double> attackRatios;
        for (int note = low; note <= high; ++note) {
            auto a = tones.find({note, to}), b = tones.find({note, from});
            if (a == tones.end() || b == tones.end())
                continue;
            for (int k = 0; k < kLearned; ++k)
                if (k < int(a->second.harmonics.size()) && k < int(b->second.harmonics.size()))
                    samples[size_t(k)].push_back(20.0 * std::log10((a->second.harmonics[size_t(k)] + 1e-9) /
                                                                   (b->second.harmonics[size_t(k)] + 1e-9)));
        }
        for (int k = 0; k < kLearned; ++k)
            curve[size_t(k)] = samples[size_t(k)].empty() ? (k ? curve[size_t(k - 1)] : 0.0) : median(samples[size_t(k)]);
        return curve;
    };
    auto reshape = [&](ProfileTone tone, const std::vector<double> &curve) {
        double before = 0.0, after = 0.0;
        for (size_t k = 0; k < tone.harmonics.size(); ++k) {
            const double gain = std::pow(10.0, curve[std::min(k, curve.size() - 1)] / 20.0);
            before += double(tone.harmonics[k]) * tone.harmonics[k];
            tone.harmonics[k] = float(tone.harmonics[k] * gain);
            tone.spliceAmp[k] = float(tone.spliceAmp[k] * gain);
            after += double(tone.harmonics[k]) * tone.harmonics[k];
        }
        const double level = std::sqrt(after / std::max(before, 1e-18));
        for (float &n : tone.noise)
            n = float(n * level * level);
        for (int16_t &v : tone.attack)
            v = int16_t(std::clamp(double(v) * level, -32767.0, 32767.0));
        return tone;
    };

    // Fill gaps in the grid: the same note at the nearest dynamic, else the
    // nearest note at the same dynamic. The tone keeps its own measured f0,
    // so playback still lands on the right pitch.
    int filled = 0;
    for (int note = low; note <= high; ++note)
        for (int d = 0; d < Profile::kDynamics; ++d) {
            auto it = tones.find({note, d});
            for (int step = 1; it == tones.end() && step < Profile::kDynamics; ++step)
                for (int other : {d - step, d + step})
                    if (it == tones.end() && other >= 0 && other < Profile::kDynamics)
                        it = tones.find({note, other});
            for (int step = 1; it == tones.end() && step <= high - low; ++step)
                for (int other : {note - step, note + step})
                    if (it == tones.end())
                        it = tones.find({other, d});
            if (!(it->first.first == note && it->first.second == d))
                ++filled;
            ProfileTone tone = it->first.first == note && it->first.second != d
                                   ? reshape(it->second, difference(d, it->first.second))
                                   : it->second;
            tone.note = note;
            tone.dynamic = d;
            profile.tones.push_back(std::move(tone));
        }
    if (filled)
        std::fprintf(stderr, "filled %d missing tones from their neighbours\n", filled);

    // Even out loudness from note to note. A player's level drifts across the
    // range and a microphone favours some notes; at each dynamic, follow a
    // smooth line through the recorded levels instead.
    auto rmsOf = [](const ProfileTone &t) {
        double sum = 0.0;
        for (float a : t.harmonics)
            sum += 0.5 * double(a) * a;
        return std::sqrt(sum);
    };
    const int notes = high - low + 1;
    for (int d = 0; d < Profile::kDynamics; ++d) {
        std::vector<double> levels(static_cast<size_t>(notes));
        for (int i = 0; i < notes; ++i)
            levels[size_t(i)] = 20.0 * std::log10(rmsOf(profile.tones[size_t(i * Profile::kDynamics + d)]) + 1e-12);
        for (int i = 0; i < notes; ++i) {
            std::vector<double> neighbours;
            for (int j = std::max(0, i - 3); j <= std::min(notes - 1, i + 3); ++j)
                neighbours.push_back(levels[size_t(j)]);
            const double correction = std::clamp(median(neighbours) - levels[size_t(i)], -6.0, 6.0);
            const double scale = std::pow(10.0, correction / 20.0);
            ProfileTone &t = profile.tones[size_t(i * Profile::kDynamics + d)];
            for (float &a : t.harmonics)
                a = float(a * scale);
            for (float &a : t.spliceAmp)
                a = float(a * scale);
            for (float &n : t.noise)
                n = float(n * scale * scale);
            for (int16_t &v : t.attack)
                v = int16_t(std::clamp(double(v) * scale, -32767.0, 32767.0));
        }
    }

    // Most instruments get much louder toward the top of their range at the
    // same marking (a horn by almost 20 dB). Keep some of that rise, but take
    // most of it out: on a breath controller the player's breath should
    // decide the level, not which octave they happen to be in.
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = 0; i < notes; ++i) {
            const double y = 20.0 * std::log10(rmsOf(profile.tones[size_t(i * Profile::kDynamics + 1)]) + 1e-12);
            sx += i, sy += y, sxx += double(i) * i, sxy += i * y;
        }
        const double slope = notes > 1 ? (notes * sxy - sx * sy) / (notes * sxx - sx * sx) : 0.0;
        for (int i = 0; i < notes; ++i) {
            const double correction = std::clamp(-0.6 * slope * (i - (notes - 1) / 2.0), -8.0, 8.0);
            const double scale = std::pow(10.0, correction / 20.0);
            for (int d = 0; d < Profile::kDynamics; ++d) {
                ProfileTone &t = profile.tones[size_t(i * Profile::kDynamics + d)];
                for (float &a : t.harmonics)
                    a = float(a * scale);
                for (float &a : t.spliceAmp)
                    a = float(a * scale);
                for (float &n : t.noise)
                    n = float(n * scale * scale);
                for (int16_t &v : t.attack)
                    v = int16_t(std::clamp(double(v) * scale, -32767.0, 32767.0));
            }
        }
    }

    // Scale so the instrument's typical mf sits at the same level as every
    // other profile's; dynamics and register keep their recorded relation.
    std::vector<double> mfLevels;
    for (const auto &t : profile.tones)
        if (t.dynamic == 1)
            mfLevels.push_back(rmsOf(t));
    const double gain = 0.12 / std::max(median(mfLevels), 1e-9);
    for (auto &t : profile.tones) {
        for (float &a : t.harmonics)
            a = float(a * gain);
        for (float &a : t.spliceAmp)
            a = float(a * gain);
        for (float &n : t.noise)
            n = float(n * gain * gain);
    }
    profile.attackGain = float(gain / (32767.0 * kAttackHeadroom));

    const std::vector<uint8_t> bytes = profile.serialize();
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    std::fprintf(stderr, "wrote %s: notes %d–%d, %zu bytes\n", argv[2], low, high, bytes.size());
    return out ? 0 : 1;
}
