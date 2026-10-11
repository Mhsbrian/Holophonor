#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <alsa/asoundlib.h>

#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

#include "backend.h"
#include "inkpainter.h"
#include "instrumentmodel.h"
#include "instruments.h"
#include "midiinput.h"
#include "paper.h"
#include "synth.h"
#include "theme.h"

namespace {

constexpr float kRate = 48000.0f;

std::vector<float> render(Synth &synth, double seconds)
{
    std::vector<float> stereo(2 * 256), mono;
    for (int block = 0; block < int(seconds * kRate / 256); ++block) {
        synth.render(stereo.data(), 256);
        for (int i = 0; i < 256; ++i)
            mono.push_back(0.5f * (stereo[2 * i] + stereo[2 * i + 1]));
    }
    return mono;
}

float rms(const std::vector<float> &x, size_t from = 0)
{
    double energy = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        energy += double(x[i]) * x[i];
    return x.size() > from ? float(std::sqrt(energy / double(x.size() - from))) : 0.0f;
}

// YIN (de Cheveigné & Kawahara, 2002): cumulative mean normalised difference,
// first dip under the threshold, parabolic refinement.
float pitchOf(const std::vector<float> &x, size_t start, int window = 2048)
{
    const int minLag = int(kRate / 2500), maxLag = int(kRate / 40);
    std::vector<double> d(size_t(maxLag) + 2, 0.0), cmnd(size_t(maxLag) + 2, 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= maxLag + 1; ++tau) {
        double sum = 0.0;
        for (int j = 0; j < window; ++j) {
            const double diff = x[start + size_t(j)] - x[start + size_t(j + tau)];
            sum += diff * diff;
        }
        d[size_t(tau)] = sum;
        running += sum;
        cmnd[size_t(tau)] = running > 0.0 ? sum * tau / running : 1.0;
    }
    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
        if (cmnd[size_t(tau)] < 0.15) {
            while (tau + 1 <= maxLag && cmnd[size_t(tau + 1)] < cmnd[size_t(tau)])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
        return 0.0f;
    const double a = cmnd[size_t(best - 1)], b = cmnd[size_t(best)], c = cmnd[size_t(best + 1)];
    const double shift = (a - 2 * b + c) != 0.0 ? 0.5 * (a - c) / (a - 2 * b + c) : 0.0;
    return float(kRate / (best + shift));
}

float centsBetween(float f, float reference) { return 1200.0f * std::log2(f / reference); }

std::unique_ptr<Synth> synthWith(const std::string &id)
{
    auto synth = std::make_unique<Synth>(kRate);
    synth->setVibratoScale(0.0f);
    synth->selectPreset(instruments::indexOf(id));
    synth->render(nullptr, 0); // apply the preset before any note
    return synth;
}

Synth::Snapshot playing(float pitch, float level, uint32_t serial)
{
    Synth::Snapshot s;
    s.pitch = pitch;
    s.level = level;
    s.serial = serial;
    s.note = int(std::lround(pitch));
    s.sounding = true;
    return s;
}

int differingPixels(const QImage &a, const QImage &b, const std::function<bool(int, int)> &where)
{
    int count = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (where(x, y) && a.pixel(x, y) != b.pixel(x, y))
                ++count;
    return count;
}

} // namespace

class HolophonorTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keep QSettings away from the real ~/.config.
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toLocal8Bit());
    }

    // --- Synth -------------------------------------------------------------

    void everyPresetSpeaksCleanly_data()
    {
        QTest::addColumn<int>("preset");
        const auto &catalog = instruments::catalog();
        for (int i = 0; i < int(catalog.size()); ++i)
            QTest::newRow(catalog[size_t(i)].id.c_str()) << i;
    }

    void everyPresetSpeaksCleanly()
    {
        QFETCH(int, preset);
        auto synth = synthWith(instruments::catalog()[size_t(preset)].id);
        synth->handle(ControlEvent::controller(2, 100));
        synth->handle(ControlEvent::noteOn(69, 100));
        const auto out = render(*synth, 1.0);
        float peak = 0.0f;
        for (float v : out) {
            QVERIFY(std::isfinite(v));
            peak = std::max(peak, std::fabs(v));
        }
        QVERIFY2(peak < 1.0f, qPrintable(QString("peak %1").arg(peak)));
        QVERIFY2(rms(out, size_t(0.2 * kRate)) > 0.005f, "silent");
    }

    void presetsAreInTune_data()
    {
        QTest::addColumn<int>("preset");
        QTest::addColumn<int>("note");
        const auto &catalog = instruments::catalog();
        for (int i = 0; i < int(catalog.size()); ++i) {
            const Preset &p = catalog[size_t(i)];
            // Bells are inharmonic by design: their partials have no common period.
            if (p.engine == Engine::Fm && std::fmod(p.fmRatio, 1.0f) != 0.0f)
                continue;
            for (int note : {69, 81})
                QTest::newRow(qPrintable(QString("%1 %2").arg(p.id.c_str()).arg(note))) << i << note;
        }
    }

    void presetsAreInTune()
    {
        QFETCH(int, preset);
        QFETCH(int, note);
        const Preset &p = instruments::catalog()[size_t(preset)];
        auto synth = synthWith(p.id);
        synth->handle(ControlEvent::controller(2, 100));
        synth->handle(ControlEvent::noteOn(note, 100));
        const auto out = render(*synth, 0.4);
        // A sub-octave oscillator makes the true period twice as long.
        const float expected = dsp::midiToHz(float(note + p.transpose)) / (p.sub > 0.0f ? 2.0f : 1.0f);
        const float measured = pitchOf(out, size_t(0.25 * kRate));
        QVERIFY2(std::fabs(centsBetween(measured, expected)) < 12.0f,
                 qPrintable(QString("expected %1 Hz, measured %2 Hz").arg(expected).arg(measured)));
    }

    void breathShapesLoudness()
    {
        auto soft = synthWith("oboe");
        soft->handle(ControlEvent::controller(2, 30));
        soft->handle(ControlEvent::noteOn(69, 100));
        auto loud = synthWith("oboe");
        loud->handle(ControlEvent::controller(2, 110));
        loud->handle(ControlEvent::noteOn(69, 100));
        const float quiet = rms(render(*soft, 0.5), 4800), strong = rms(render(*loud, 0.5), 4800);
        QVERIFY2(strong > quiet * 2.0f, qPrintable(QString("%1 vs %2").arg(strong).arg(quiet)));
    }

    void firstBreathControllerWins()
    {
        auto synth = synthWith("flute");
        synth->handle(ControlEvent::pressure(90));
        synth->handle(ControlEvent::controller(2, 10));
        synth->render(nullptr, 0);
        QCOMPARE(synth->snapshot().breathSource, int(Synth::BreathAftertouch));
    }

    void legatoFallsBackToTheHeldNote()
    {
        auto synth = synthWith("holophonor");
        synth->handle(ControlEvent::controller(2, 100));
        synth->handle(ControlEvent::noteOn(60, 100));
        render(*synth, 0.2);
        synth->handle(ControlEvent::noteOn(64, 100));
        render(*synth, 0.3);
        QCOMPARE(synth->snapshot().note, 64);
        QVERIFY(std::fabs(synth->snapshot().pitch - 64.0f) < 0.05f);
        synth->handle(ControlEvent::noteOff(64));
        render(*synth, 0.3);
        QCOMPARE(synth->snapshot().note, 60);
        QVERIFY(std::fabs(synth->snapshot().pitch - 60.0f) < 0.05f);
        QVERIFY(synth->snapshot().sounding);
        QCOMPARE(synth->snapshot().serial, 3u);
    }

    void pitchBendReachesTwoSemitones()
    {
        auto synth = synthWith("clarinet");
        synth->handle(ControlEvent::controller(2, 100));
        synth->handle(ControlEvent::noteOn(60, 100));
        synth->handle(ControlEvent::pitchBend(8191));
        render(*synth, 0.3);
        QVERIFY(std::fabs(synth->snapshot().pitch - 62.0f) < 0.05f);
    }

    void keysPlayWithoutBreathAndRelease()
    {
        auto synth = synthWith("holophonor");
        synth->handle(ControlEvent::noteOn(60, 100, ControlEvent::Keys));
        QVERIFY(rms(render(*synth, 0.3), 4800) > 0.01f);
        QVERIFY(synth->snapshot().sounding);
        synth->handle(ControlEvent::noteOff(60, ControlEvent::Keys));
        render(*synth, 3.0);
        QVERIFY(!synth->snapshot().sounding);
    }

    void keysStillPlayWhileAnEwiIsLatched()
    {
        auto synth = synthWith("flute");
        synth->handle(ControlEvent::controller(2, 0)); // an EWI at rest
        synth->handle(ControlEvent::noteOn(67, 100, ControlEvent::Keys));
        QVERIFY(rms(render(*synth, 0.3), 4800) > 0.01f);
    }

    void silentBreathSourceFallsBackToEnvelopes()
    {
        auto synth = synthWith("oboe");
        synth->handle(ControlEvent::controller(2, 100));
        synth->handle(ControlEvent::controller(2, 0));
        render(*synth, 2.5);
        synth->handle(ControlEvent::noteOn(60, 100)); // a keyboard, long after the breath went quiet
        QVERIFY(rms(render(*synth, 0.3), 4800) > 0.01f);
        QCOMPARE(synth->snapshot().breathSource, int(Synth::NoBreath));
    }

    void switchingInstrumentsMidPhraseKeepsSounding()
    {
        auto synth = synthWith("oboe");
        synth->handle(ControlEvent::controller(2, 100));
        synth->handle(ControlEvent::noteOn(62, 100));
        render(*synth, 0.2);
        synth->selectPreset(instruments::indexOf("flute"));
        const auto out = render(*synth, 0.4);
        QVERIFY(rms(out, out.size() - 4800) > 0.01f);
        QVERIFY(synth->snapshot().sounding);
    }

    void allNotesOffSilences()
    {
        auto synth = synthWith("strings");
        for (int note : {60, 64, 67})
            synth->handle(ControlEvent::noteOn(note, 100, ControlEvent::Keys));
        render(*synth, 0.3);
        synth->handle(ControlEvent::allNotesOff());
        render(*synth, 3.0);
        QVERIFY(!synth->snapshot().sounding);
    }

    // --- Catalog and search ------------------------------------------------

    void catalogIdsAreUnique()
    {
        const auto &catalog = instruments::catalog();
        QVERIFY(catalog.size() >= 24);
        QSet<QString> ids;
        for (const auto &p : catalog)
            ids.insert(QString::fromStdString(p.id));
        QCOMPARE(ids.size(), qsizetype(catalog.size()));
        QCOMPARE(instruments::catalog()[size_t(instruments::defaultIndex())].name, std::string("Holophonor"));
    }

    void searchFindsByNameFamilyTagAndInitials()
    {
        InstrumentModel model;
        QCOMPARE(model.count(), int(instruments::catalog().size()));

        model.setQuery("sax");
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.data(model.index(0), InstrumentModel::NameRole).toString(), QString("Alto Sax"));

        model.setQuery("brass");
        QCOMPARE(model.count(), 3);

        model.setQuery("frhn");
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0), InstrumentModel::NameRole).toString(), QString("French Horn"));

        model.setQuery("double reed");
        QVERIFY(model.count() >= 2);

        model.setQuery("zzz");
        QCOMPARE(model.count(), 0);
    }

    // --- MIDI --------------------------------------------------------------

    void recognisesEwiNames()
    {
        QVERIFY(MidiInput::looksLikeEwi("EWI USB"));
        QVERIFY(MidiInput::looksLikeEwi("EWI5000 MIDI 1"));
        QVERIFY(MidiInput::looksLikeEwi("Akai EWI-Solo"));
        QVERIFY(!MidiInput::looksLikeEwi("Newick Synth"));
    }

    void autoConnectPolicy()
    {
        MidiPort port;
        port.client = 24;
        port.clientName = "Arturia KeyStep";
        port.portName = "KeyStep MIDI 1";
        port.kernelClient = true;
        port.readable = true;
        QVERIFY(MidiInput::shouldAutoConnect(port, 128));

        MidiPort through = port;
        through.client = 14;
        through.clientName = "Midi Through";
        through.portName = "Midi Through Port-0";
        QVERIFY(!MidiInput::shouldAutoConnect(through, 128));

        MidiPort app = port;
        app.client = 130;
        app.clientName = "Ardour";
        app.kernelClient = false;
        QVERIFY(!MidiInput::shouldAutoConnect(app, 128));

        MidiPort ewiApp = app;
        ewiApp.clientName = "EWI Bridge";
        QVERIFY(MidiInput::shouldAutoConnect(ewiApp, 128));

        MidiPort self = port;
        self.client = 128;
        QVERIFY(!MidiInput::shouldAutoConnect(self, 128));

        MidiPort writeOnly = port;
        writeOnly.readable = false;
        QVERIFY(!MidiInput::shouldAutoConnect(writeOnly, 128));
    }

    void translatesSequencerEvents()
    {
        snd_seq_event_t event;
        ControlEvent out;

        snd_seq_ev_clear(&event);
        snd_seq_ev_set_noteon(&event, 3, 64, 90);
        QVERIFY(MidiInput::translate(event, out));
        QCOMPARE(int(out.type), int(ControlEvent::NoteOn));
        QCOMPARE(int(out.number), 64);
        QCOMPARE(int(out.value), 90);

        snd_seq_ev_set_controller(&event, 0, 2, 77);
        QVERIFY(MidiInput::translate(event, out));
        QCOMPARE(int(out.type), int(ControlEvent::Controller));
        QCOMPARE(int(out.number), 2);
        QCOMPARE(int(out.value), 77);

        snd_seq_ev_set_pitchbend(&event, 0, -8192);
        QVERIFY(MidiInput::translate(event, out));
        QCOMPARE(int(out.value), -8192);

        snd_seq_ev_set_chanpress(&event, 0, 55);
        QVERIFY(MidiInput::translate(event, out));
        QCOMPARE(int(out.type), int(ControlEvent::ChannelPressure));

        snd_seq_ev_set_pgmchange(&event, 0, 5);
        QVERIFY(!MidiInput::translate(event, out));
    }

    // A virtual EWI appears on the sequencer after we started listening; it
    // must be connected, heard, and let go when it leaves.
    void hotPluggedEwiIsConnectedAndHeard()
    {
        std::mutex lock;
        std::vector<ControlEvent> received;
        MidiInput input([&](const ControlEvent &event) {
            std::lock_guard<std::mutex> guard(lock);
            received.push_back(event);
        });
        if (!input.start())
            QSKIP("The ALSA sequencer is not available here.");
        QSignalSpy connected(&input, &MidiInput::deviceConnected);
        QSignalSpy disconnected(&input, &MidiInput::deviceDisconnected);
        auto named = [](const QSignalSpy &spy, const QString &name) {
            return std::any_of(spy.begin(), spy.end(), [&](const QList<QVariant> &args) { return args.at(0) == name; });
        };

        snd_seq_t *rig = nullptr;
        QCOMPARE(snd_seq_open(&rig, "default", SND_SEQ_OPEN_OUTPUT, 0), 0);
        snd_seq_set_client_name(rig, "EWI Test Rig");
        const int port = snd_seq_create_simple_port(rig, "EWI Test Rig MIDI 1",
                                                    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                                    SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        QVERIFY(port >= 0);
        QTRY_VERIFY(named(connected, "EWI Test Rig"));

        snd_seq_event_t event;
        snd_seq_ev_clear(&event);
        snd_seq_ev_set_source(&event, port);
        snd_seq_ev_set_subs(&event);
        snd_seq_ev_set_direct(&event);
        snd_seq_ev_set_controller(&event, 0, 2, 90);
        snd_seq_event_output_direct(rig, &event);
        snd_seq_ev_set_noteon(&event, 0, 64, 100);
        snd_seq_event_output_direct(rig, &event);

        QTRY_VERIFY([&] {
            std::lock_guard<std::mutex> guard(lock);
            const bool breath = std::any_of(received.begin(), received.end(), [](const ControlEvent &e) {
                return e.type == ControlEvent::Controller && e.number == 2 && e.value == 90;
            });
            const bool note = std::any_of(received.begin(), received.end(), [](const ControlEvent &e) {
                return e.type == ControlEvent::NoteOn && e.number == 64;
            });
            return breath && note;
        }());

        snd_seq_close(rig);
        QTRY_VERIFY(named(disconnected, "EWI Test Rig"));
        input.stop();
    }

    // --- The app's state ---------------------------------------------------

    void computerKeysFollowTheAbletonLayout()
    {
        QCOMPARE(Backend::noteForKey(Qt::Key_A, 0), 60);
        QCOMPARE(Backend::noteForKey(Qt::Key_W, 0), 61);
        QCOMPARE(Backend::noteForKey(Qt::Key_K, 0), 72);
        QCOMPARE(Backend::noteForKey(Qt::Key_Apostrophe, 0), 77);
        QCOMPARE(Backend::noteForKey(Qt::Key_A, -1), 48);
        QCOMPARE(Backend::noteForKey(Qt::Key_Q, 0), -1);
        QCOMPARE(Backend::noteForKey(Qt::Key_Z, 0), -1);
    }

    void backendPlaysFromTheComputerKeyboard()
    {
        Backend backend;
        backend.start(true);
        QVERIFY(backend.keyDown(Qt::Key_A));
        QTRY_VERIFY(backend.snapshot().sounding);
        QCOMPARE(backend.snapshot().note, 60);
        QVERIFY(backend.keyUp(Qt::Key_A));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().sounding, 5000);
        QVERIFY(!backend.keyDown(Qt::Key_Q));
    }

    void backendRemembersTheInstrument()
    {
        {
            Backend backend;
            backend.setInstrument(instruments::indexOf("cello"));
            backend.setVisual(InkPainter::Graphite);
        }
        Backend again;
        QCOMPARE(again.instrumentName(), QString("Cello"));
        QCOMPARE(again.visual(), int(InkPainter::Graphite));
    }

    // --- Painting ----------------------------------------------------------

    void sumiInksWherePitchSits()
    {
        InkPainter painter;
        painter.resize(QSize(640, 400), 1.0);
        QVERIFY(!painter.step(Synth::Snapshot{}, 1.0 / 60.0)); // silence paints nothing
        for (int frame = 0; frame < 60; ++frame)
            painter.step(playing(69.0f, 0.8f, 1), 1.0 / 60.0);

        const QImage &image = painter.image();
        int darkest = 255, row = -1;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (const int g = qGray(image.pixel(x, y)); g < darkest) {
                    darkest = g;
                    row = y;
                }
        const int expected = int(400 * (0.86 - 0.72 * InkPainter::pitchPosition(69.0)));
        QVERIFY2(darkest < 80, qPrintable(QString("darkest %1").arg(darkest)));
        QVERIFY2(std::abs(row - expected) < 20, qPrintable(QString("row %1, expected %2").arg(row).arg(expected)));
    }

    void freshSheetReturnsExactlyToPaper()
    {
        InkPainter painter;
        painter.resize(QSize(320, 200), 1.0);
        for (int frame = 0; frame < 90; ++frame)
            painter.step(playing(64.0f, 0.9f, 1), 1.0 / 60.0);
        painter.freshSheet();
        for (int frame = 0; frame < 60; ++frame)
            painter.step(Synth::Snapshot{}, 1.0 / 60.0);
        const QImage sheet = paper::sheet(QSize(320, 200), 1.0);
        QCOMPARE(differingPixels(painter.image(), sheet, [](int, int) { return true; }), 0);
    }

    void projectionLightFadesAllTheWay()
    {
        InkPainter painter;
        painter.resize(QSize(320, 200), 1.0);
        painter.setMode(InkPainter::Projection);
        QVERIFY(painter.dark());
        for (int frame = 0; frame < 60; ++frame)
            painter.step(Synth::Snapshot{}, 1.0 / 60.0);
        for (int frame = 0; frame < 120; ++frame)
            painter.step(playing(67.0f, 0.9f, frame < 60 ? 1 : 2), 1.0 / 60.0);
        for (int frame = 0; frame < 600; ++frame)
            painter.step(Synth::Snapshot{}, 1.0 / 60.0);
        // Everything but the pilot light at the centre is back to the night.
        const QImage night = paper::night(QSize(320, 200), 1.0);
        const int left = differingPixels(painter.image(), night,
                                         [](int x, int y) { return std::hypot(x - 160, y - 100) > 16; });
        QCOMPARE(left, 0);
    }

    void paperIsDeterministic()
    {
        const QImage a = paper::sheet(QSize(200, 120), 1.0), b = paper::sheet(QSize(200, 120), 1.0);
        QCOMPARE(a.size(), QSize(200, 120));
        QCOMPARE(a, b);
    }

    void noteNames()
    {
        QCOMPARE(InkPainter::noteName(60), QString("C4"));
        QCOMPARE(InkPainter::noteName(69), QString("A4"));
        QCOMPARE(InkPainter::noteName(73), QString::fromUtf8("C♯5"));
    }

    // --- Theme -------------------------------------------------------------

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QCOMPARE(theme.pigments().size(), 8);
    }

    void themeReadsAccentAndPigments()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\nred = \"#aa2211\"\nblue = '#2244cc'\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.vermilion(), QString("#aa2211"));
        QVERIFY(theme.pigments().contains("#2244cc"));
    }

private:
    QTemporaryDir m_config;
};

QTEST_MAIN(HolophonorTests)
#include "holophonor_tests.moc"
