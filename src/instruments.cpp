#include "instruments.h"

#include <functional>
#include <map>

#include "profile.h"

namespace {

Preset make(const char *id, const char *name, const char *family, const char *tags, Engine engine,
            const std::function<void(Preset &)> &shape)
{
    Preset p;
    p.id = id;
    p.name = name;
    p.family = family;
    p.tags = tags;
    p.engine = engine;
    shape(p);
    return p;
}

// Formant centres come from published vowel and body-resonance tables for
// each instrument family; they set the character, the oscillators the pitch.
std::vector<Preset> build()
{
    std::vector<Preset> c;

    c.push_back(make("holophonor", "Holophonor", "Signature", "oboe hologram glass shimmer futurama breath",
                     Engine::Wind, [](Preset &p) {
                         p.pulse = 0.55f, p.pulseWidth = 0.22f, p.saw = 0.25f, p.sine = 0.2f;
                         p.cutoff = 3.0f, p.breathCutoff = 9.0f, p.resonance = 0.8f;
                         p.formants = {{{1100, 1.0f, 5}, {2700, 0.5f, 6}, {}}};
                         p.formantMix = 0.45f, p.shimmer = 0.35f;
                         p.vibratoRate = 5.2f, p.vibratoDepth = 0.12f, p.vibratoDelay = 0.35f;
                         p.attack = 0.03f, p.release = 0.25f, p.level = 0.62f;
                         p.reverb = 0.55f, p.chorus = 0.35f;
                     }));

    // The acoustic winds play profiles measured from the University of Iowa
    // Musical Instrument Samples (see tools/profile). They sound exactly the
    // note they receive: wind controllers send concert pitch and players set
    // octave and transposition on the instrument. Vibrato here is only for
    // the computer keys; a breath player brings their own with the bite.
    auto measured = [](const char *profile, float vibrato, float reverb) {
        return [=](Preset &p) {
            p.profile = profile;
            p.vibratoRate = 5.3f, p.vibratoDepth = vibrato, p.vibratoDelay = 0.45f;
            p.release = 0.08f, p.level = 0.35f, p.reverb = reverb;
        };
    };
    c.push_back(make("soprano-sax", "Soprano Sax", "Woodwind", "saxophone reed jazz bright straight", Engine::Spectral,
                     measured("soprano-sax", 0.12f, 0.2f)));
    c.push_back(make("alto-sax", "Alto Sax", "Woodwind", "saxophone reed jazz smooth", Engine::Spectral,
                     measured("alto-sax", 0.12f, 0.2f)));
    // Tenor and baritone are built from the alto recordings, played as
    // instruments a fifth and an octave larger: the resonances move down too.
    c.push_back(make("tenor-sax", "Tenor Sax", "Woodwind", "saxophone reed jazz breathy warm", Engine::Spectral,
                     [&](Preset &p) {
                         measured("alto-sax", 0.13f, 0.2f)(p);
                         p.profileShift = 7;
                     }));
    c.push_back(make("baritone-sax", "Baritone Sax", "Woodwind", "saxophone reed jazz low bari", Engine::Spectral,
                     [&](Preset &p) {
                         measured("alto-sax", 0.1f, 0.18f)(p);
                         p.profileShift = 12;
                     }));
    c.push_back(make("clarinet", "Clarinet", "Woodwind", "single reed orchestral jazz hollow", Engine::Spectral,
                     measured("clarinet", 0.0f, 0.22f)));
    c.push_back(make("flute", "Flute", "Woodwind", "orchestral airy concert", Engine::Spectral,
                     measured("flute", 0.16f, 0.3f)));
    c.push_back(make("oboe", "Oboe", "Woodwind", "double reed orchestral nasal", Engine::Spectral,
                     measured("oboe", 0.1f, 0.25f)));
    c.push_back(make("bassoon", "Bassoon", "Woodwind", "double reed orchestral low", Engine::Spectral,
                     measured("bassoon", 0.06f, 0.25f)));

    c.push_back(make("trumpet", "Trumpet", "Brass", "brass bright jazz fanfare", Engine::Spectral,
                     measured("trumpet", 0.07f, 0.25f)));
    c.push_back(make("french-horn", "French Horn", "Brass", "brass horn mellow orchestral", Engine::Spectral,
                     [&](Preset &p) {
                         measured("french-horn", 0.0f, 0.4f)(p);
                         p.level = 0.26f; // a horn's ff is far above its mf
                     }));
    c.push_back(make("trombone", "Trombone", "Brass", "brass slide low jazz", Engine::Spectral,
                     [&](Preset &p) {
                         measured("trombone", 0.05f, 0.3f)(p);
                         p.level = 0.28f;
                     }));

    c.push_back(make("violin", "Violin", "Strings", "bowed string orchestral solo", Engine::Wind, [](Preset &p) {
        p.saw = 0.85f, p.detunedSaw = 0.3f, p.detuneCents = 3.0f;
        p.cutoff = 3.0f, p.breathCutoff = 10.0f, p.resonance = 0.6f;
        p.formants = {{{500, 0.6f, 3}, {1500, 0.5f, 3}, {3000, 0.7f, 4}}};
        p.formantMix = 0.45f;
        p.vibratoRate = 5.8f, p.vibratoDepth = 0.22f, p.vibratoDelay = 0.25f;
        p.attack = 0.06f, p.level = 0.47f, p.reverb = 0.4f;
    }));
    c.push_back(make("cello", "Cello", "Strings", "bowed string orchestral low warm", Engine::Wind, [](Preset &p) {
        p.transpose = -12;
        p.saw = 0.85f, p.detunedSaw = 0.3f, p.detuneCents = 3.0f;
        p.cutoff = 3.0f, p.breathCutoff = 9.0f, p.resonance = 0.6f;
        p.formants = {{{250, 0.8f, 3}, {1000, 0.5f, 3}, {2400, 0.4f, 4}}};
        p.formantMix = 0.45f;
        p.vibratoRate = 5.3f, p.vibratoDepth = 0.2f, p.vibratoDelay = 0.3f;
        p.attack = 0.07f, p.level = 0.40f, p.reverb = 0.4f;
    }));
    c.push_back(make("strings", "String Ensemble", "Strings", "section orchestra lush pad", Engine::Wind,
                     [](Preset &p) {
                         p.saw = 0.6f, p.detunedSaw = 0.6f, p.detuneCents = 9.0f;
                         p.cutoff = 2.5f, p.breathCutoff = 6.0f;
                         p.vibratoDepth = 0.08f, p.attack = 0.15f, p.release = 0.5f;
                         p.level = 0.36f, p.reverb = 0.6f, p.chorus = 0.6f;
                     }));

    c.push_back(make("choir", "Choir Aah", "Voice", "vocal choir ah vowel ethereal", Engine::Wind, [](Preset &p) {
        p.saw = 0.9f, p.pulse = 0.2f, p.pulseWidth = 0.3f;
        p.cutoff = 6.0f, p.breathCutoff = 4.0f;
        p.formants = {{{800, 1.0f, 8}, {1150, 0.6f, 9}, {2900, 0.35f, 10}}};
        p.formantMix = 0.9f, p.noise = 0.03f;
        p.vibratoDepth = 0.15f, p.attack = 0.12f, p.release = 0.4f;
        p.level = 1.20f, p.reverb = 0.65f, p.chorus = 0.5f;
    }));
    c.push_back(make("whistle", "Whistle", "Voice", "whistling pure tin", Engine::Wind, [](Preset &p) {
        p.sine = 1.0f, p.cutoff = 1.1f, p.breathCutoff = 1.0f;
        p.noise = 0.08f, p.noiseCenter = 1.0f, p.noiseQ = 3.0f;
        p.vibratoRate = 6.0f, p.vibratoDepth = 0.15f, p.vibratoDelay = 0.2f;
        p.level = 0.24f, p.reverb = 0.35f;
    }));

    c.push_back(make("shakuhachi", "Shakuhachi", "World Winds", "japanese bamboo flute breathy zen", Engine::Wind,
                     [](Preset &p) {
                         p.sine = 0.7f, p.saw = 0.2f;
                         p.cutoff = 1.6f, p.breathCutoff = 3.0f;
                         p.noise = 0.45f, p.noiseCenter = 1.5f, p.noiseQ = 0.7f;
                         p.vibratoRate = 4.2f, p.vibratoDepth = 0.22f, p.vibratoDelay = 0.6f;
                         p.attack = 0.06f, p.level = 0.39f, p.reverb = 0.45f;
                     }));
    c.push_back(make("pan-flute", "Pan Flute", "World Winds", "panpipes andes airy", Engine::Wind, [](Preset &p) {
        p.sine = 0.9f, p.pulse = 0.1f;
        p.cutoff = 1.5f, p.breathCutoff = 2.0f;
        p.noise = 0.3f, p.noiseCenter = 3.0f, p.noiseQ = 1.2f;
        p.vibratoDepth = 0.06f, p.attack = 0.03f, p.level = 0.23f, p.reverb = 0.45f;
    }));
    c.push_back(make("ocarina", "Ocarina", "World Winds", "vessel flute clay pure", Engine::Wind, [](Preset &p) {
        p.sine = 1.0f, p.pulse = 0.05f;
        p.cutoff = 1.2f, p.breathCutoff = 1.5f, p.noise = 0.06f;
        p.vibratoRate = 5.5f, p.vibratoDepth = 0.08f;
        p.level = 0.22f, p.reverb = 0.35f;
    }));

    c.push_back(make("analog-lead", "Analog Lead", "Synth", "synthesizer lead mono saw", Engine::Wind,
                     [](Preset &p) {
                         p.portamento = 0.05f;
                         p.saw = 0.7f, p.pulse = 0.5f, p.pulseWidth = 0.45f;
                         p.detunedSaw = 0.4f, p.detuneCents = 7.0f;
                         p.cutoff = 1.5f, p.breathCutoff = 18.0f, p.resonance = 2.0f;
                         p.vibratoRate = 5.5f, p.vibratoDepth = 0.1f, p.vibratoDelay = 0.5f;
                         p.level = 0.43f, p.reverb = 0.25f, p.chorus = 0.2f;
                     }));
    c.push_back(make("glass-pad", "Glass Pad", "Synth", "pad ambient shimmer slow", Engine::Wind, [](Preset &p) {
        p.saw = 0.5f, p.detunedSaw = 0.5f, p.detuneCents = 12.0f, p.sine = 0.3f;
        p.cutoff = 2.0f, p.breathCutoff = 5.0f, p.shimmer = 0.4f;
        p.attack = 0.3f, p.release = 1.2f;
        p.level = 0.48f, p.reverb = 0.75f, p.chorus = 0.7f;
    }));
    c.push_back(make("sub-bass", "Sub Bass", "Synth", "bass low sine deep", Engine::Wind, [](Preset &p) {
        p.transpose = -24;
        p.sine = 0.8f, p.saw = 0.35f, p.sub = 0.3f;
        p.cutoff = 1.2f, p.breathCutoff = 5.0f, p.resonance = 1.2f;
        p.level = 0.37f, p.reverb = 0.1f;
    }));
    c.push_back(make("aether", "Aether", "Synth", "theremin sine eerie glide", Engine::Wind, [](Preset &p) {
        p.portamento = 0.15f;
        p.sine = 1.0f, p.pulse = 0.04f, p.cutoff = 1.5f, p.breathCutoff = 1.0f;
        p.vibratoRate = 6.0f, p.vibratoDepth = 0.3f, p.vibratoDelay = 0.15f;
        p.level = 0.22f, p.reverb = 0.5f;
    }));

    c.push_back(make("harp", "Harp", "Plucked", "concert harp string pluck", Engine::Pluck, [](Preset &p) {
        p.decay = 4.5f, p.brightness = 0.55f, p.release = 1.5f;
        p.level = 0.80f, p.reverb = 0.5f;
    }));
    c.push_back(make("nylon-guitar", "Nylon Guitar", "Plucked", "classical guitar string pluck", Engine::Pluck,
                     [](Preset &p) {
                         p.transpose = -12;
                         p.decay = 3.0f, p.brightness = 0.4f, p.release = 0.4f;
                         p.level = 0.90f, p.reverb = 0.3f;
                     }));
    c.push_back(make("koto", "Koto", "Plucked", "japanese zither string pluck bright", Engine::Pluck, [](Preset &p) {
        p.decay = 2.5f, p.brightness = 0.75f, p.release = 0.6f;
        p.level = 0.80f, p.reverb = 0.4f;
    }));

    c.push_back(make("electric-piano", "Electric Piano", "Keys & Mallets", "rhodes keys tine fm", Engine::Fm,
                     [](Preset &p) {
                         p.fmRatio = 1.0f, p.fmIndex = 2.2f, p.fmIndexDecay = 0.5f, p.fmSustainIndex = 0.3f;
                         p.decay = 2.5f, p.release = 0.35f;
                         p.level = 0.50f, p.reverb = 0.3f, p.chorus = 0.4f;
                     }));
    c.push_back(make("glass-bells", "Glass Bells", "Keys & Mallets", "celesta bell chime fm sparkle", Engine::Fm,
                     [](Preset &p) {
                         p.transpose = 12;
                         p.fmRatio = 3.5f, p.fmIndex = 2.5f, p.fmIndexDecay = 0.8f, p.fmSustainIndex = 0.4f;
                         p.decay = 3.5f, p.release = 1.2f;
                         p.level = 0.35f, p.reverb = 0.55f;
                     }));
    c.push_back(make("marimba", "Marimba", "Keys & Mallets", "mallet wood percussion fm", Engine::Fm, [](Preset &p) {
        p.fmRatio = 4.0f, p.fmIndex = 1.6f, p.fmIndexDecay = 0.06f, p.fmSustainIndex = 0.0f;
        p.decay = 0.9f, p.release = 0.3f;
        p.level = 0.80f, p.reverb = 0.3f;
    }));

    return c;
}

} // namespace

namespace instruments {

const std::vector<Preset> &catalog()
{
    static const std::vector<Preset> presets = build();
    return presets;
}

int indexOf(const std::string &id)
{
    const auto &c = catalog();
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i].id == id)
            return int(i);
    return -1;
}

int defaultIndex() { return indexOf("holophonor"); }

namespace {
std::map<std::string, std::shared_ptr<const Profile>> &profiles()
{
    static std::map<std::string, std::shared_ptr<const Profile>> map;
    return map;
}
} // namespace

void attachProfile(const std::string &id, std::shared_ptr<const Profile> profile) { profiles()[id] = std::move(profile); }

std::shared_ptr<const Profile> profile(const std::string &id)
{
    const auto it = profiles().find(id);
    return it == profiles().end() ? nullptr : it->second;
}

} // namespace instruments
