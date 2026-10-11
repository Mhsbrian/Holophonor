#include "backend.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

#include "inkpainter.h"
#include "instruments.h"

Backend::Backend(QObject *parent)
    : QObject(parent), m_synth(std::make_unique<Synth>(float(AudioEngine::kSampleRate))), m_audio(*m_synth),
      m_midi([this](const ControlEvent &event) { m_synth->pushMidi(event); })
{
    QSettings settings;
    const int saved = instruments::indexOf(settings.value("instrument").toString().toStdString());
    m_instrument = saved >= 0 ? saved : instruments::defaultIndex();
    m_visual = std::clamp(settings.value("visual", 0).toInt(), 0, InkPainter::ModeCount - 1);
    m_synth->selectPreset(m_instrument);

    connect(&m_audio, &AudioEngine::changed, this, &Backend::audioChanged);
    connect(&m_midi, &MidiInput::deviceConnected, this, [this](const QString &name, bool ewi) {
        m_devices.append({name, ewi});
        emit deviceChanged();
        emit toast(ewi ? tr("%1 connected — breathe").arg(name) : tr("%1 connected").arg(name));
    });
    connect(&m_midi, &MidiInput::deviceDisconnected, this, [this](const QString &name) {
        const auto it = std::find_if(m_devices.begin(), m_devices.end(), [&](const Device &d) { return d.name == name; });
        if (it != m_devices.end())
            m_devices.erase(it);
        // A pulled cable must never leave a note hanging.
        m_synth->pushUi(ControlEvent::allNotesOff());
        emit deviceChanged();
        emit toast(tr("%1 disconnected").arg(name));
    });

    m_liveTimer.setInterval(33);
    connect(&m_liveTimer, &QTimer::timeout, this, &Backend::refreshLive);
    m_demoTimer.setInterval(4);
    m_demoTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_demoTimer, &QTimer::timeout, this, &Backend::playDue);
}

Backend::~Backend()
{
    m_demoTimer.stop();
    m_midi.stop();
    m_audio.stop();
}

void Backend::start(bool silent)
{
    m_audio.start(silent);
    if (!m_midi.start()) {
        m_midiError = m_midi.error();
        emit deviceChanged();
    }
    m_liveTimer.start();
}

void Backend::setInstrument(int index)
{
    const int count = int(instruments::catalog().size());
    index = std::clamp(index, 0, count - 1);
    if (index == m_instrument)
        return;
    m_instrument = index;
    m_synth->selectPreset(index);
    saveSettings();
    emit instrumentChanged();
}

void Backend::stepInstrument(int delta)
{
    const int count = int(instruments::catalog().size());
    setInstrument(((m_instrument + delta) % count + count) % count);
}

QString Backend::instrumentName() const
{
    return QString::fromStdString(instruments::catalog()[size_t(m_instrument)].name);
}

QString Backend::instrumentFamily() const
{
    return QString::fromStdString(instruments::catalog()[size_t(m_instrument)].family);
}

void Backend::setVisual(int visual)
{
    visual = std::clamp(visual, 0, InkPainter::ModeCount - 1);
    if (visual == m_visual)
        return;
    m_visual = visual;
    saveSettings();
    emit visualChanged();
}

void Backend::saveSettings() const
{
    QSettings settings;
    settings.setValue("instrument", QString::fromStdString(instruments::catalog()[size_t(m_instrument)].id));
    settings.setValue("visual", m_visual);
}

QString Backend::device() const
{
    for (const auto &d : m_devices)
        if (d.ewi)
            return d.name;
    return m_devices.isEmpty() ? QString() : m_devices.first().name;
}

bool Backend::ewi() const
{
    return std::any_of(m_devices.begin(), m_devices.end(), [](const Device &d) { return d.ewi; });
}

void Backend::refreshLive()
{
    static const char *sources[] = {"", "breath · cc2", "breath · cc11", "breath · cc7", "breath · aftertouch"};
    const Synth::Snapshot s = m_synth->snapshot();
    QString name = m_noteName;
    int cents = m_cents;
    if (s.sounding) {
        const int nearest = int(std::lround(s.pitch));
        name = InkPainter::noteName(nearest);
        cents = int(std::lround((s.pitch - nearest) * 100.0f));
    }
    const qreal level = std::round(s.level * 50.0) / 50.0; // fewer bindings to re-evaluate
    const QString source = QString::fromUtf8(sources[std::clamp(s.breathSource, 0, 4)]);
    if (name != m_noteName || cents != m_cents || level != m_level || s.sounding != m_sounding ||
        source != m_breathSource) {
        m_noteName = name;
        m_cents = cents;
        m_level = level;
        m_sounding = s.sounding;
        m_breathSource = source;
        emit liveChanged();
    }
    if (s.sounding && !m_hasPlayed) {
        m_hasPlayed = true;
        emit hasPlayedChanged();
    }
}

int Backend::noteForKey(int key, int octave)
{
    static const QHash<int, int> offsets = {
        {Qt::Key_A, 0},  {Qt::Key_W, 1},  {Qt::Key_S, 2},  {Qt::Key_E, 3},         {Qt::Key_D, 4},
        {Qt::Key_F, 5},  {Qt::Key_T, 6},  {Qt::Key_G, 7},  {Qt::Key_Y, 8},         {Qt::Key_H, 9},
        {Qt::Key_U, 10}, {Qt::Key_J, 11}, {Qt::Key_K, 12}, {Qt::Key_O, 13},        {Qt::Key_L, 14},
        {Qt::Key_P, 15}, {Qt::Key_Semicolon, 16},          {Qt::Key_Apostrophe, 17},
    };
    const auto it = offsets.find(key);
    return it == offsets.end() ? -1 : 60 + 12 * octave + it.value();
}

bool Backend::keyDown(int key)
{
    if (key == Qt::Key_Z || key == Qt::Key_X) {
        const int next = std::clamp(m_octave + (key == Qt::Key_X ? 1 : -1), -3, 3);
        if (next != m_octave) {
            m_octave = next;
            emit octaveChanged();
            emit toast(tr("Keys start at C%1").arg(4 + m_octave));
        }
        return true;
    }
    const int note = noteForKey(key, m_octave);
    if (note < 0)
        return false;
    if (!m_keysDown.contains(key)) {
        m_keysDown.insert(key, note);
        m_synth->pushUi(ControlEvent::noteOn(note, 100, ControlEvent::Keys));
    }
    return true;
}

bool Backend::keyUp(int key)
{
    if (key == Qt::Key_Z || key == Qt::Key_X)
        return true;
    const auto it = m_keysDown.find(key);
    if (it == m_keysDown.end())
        return noteForKey(key, 0) >= 0;
    // Release the note that was struck, even if the octave moved meanwhile.
    m_synth->pushUi(ControlEvent::noteOff(it.value(), ControlEvent::Keys));
    m_keysDown.erase(it);
    return true;
}

void Backend::releaseKeys()
{
    for (const int note : std::as_const(m_keysDown))
        m_synth->pushUi(ControlEvent::noteOff(note, ControlEvent::Keys));
    m_keysDown.clear();
}

void Backend::panic()
{
    releaseKeys();
    if (m_demoTimer.isActive()) {
        m_demoTimer.stop();
        emit demoPlayingChanged();
    }
    m_synth->pushUi(ControlEvent::allNotesOff(ControlEvent::Keys));
}

// A short phrase in D dorian, sent the way an EWI sends it: breath as a
// continuous controller, legato notes overlapping by a few milliseconds, and
// one entrance scooped up from a semitone below.
void Backend::playDemo()
{
    if (m_demoTimer.isActive())
        return;
    struct Note {
        int pitch;
        double start, end;
    };
    struct Phrase {
        QVector<Note> notes;
        double peak;
    };
    const Phrase phrases[] = {
        {{{62, 0.0, 1.1}, {65, 1.1, 1.6}, {67, 1.6, 2.2}, {69, 2.2, 3.6}}, 0.78},
        {{{72, 4.2, 4.9}, {69, 4.9, 5.4}, {67, 5.4, 6.0}, {64, 6.0, 6.5}, {62, 6.5, 8.2}}, 0.85},
        {{{69, 9.0, 9.5}, {74, 9.5, 10.3}, {76, 10.3, 10.8}, {77, 10.8, 11.3}, {76, 11.3, 11.8}, {74, 11.8, 13.8}},
         0.9},
    };

    m_demo.clear();
    auto cue = [this](double at, const ControlEvent &event) { m_demo.append({at, event}); };
    for (const Phrase &phrase : phrases) {
        const double start = phrase.notes.first().start, end = phrase.notes.last().end;
        for (double t = start; t <= end; t += 0.012) {
            const double u = (t - start) / (end - start);
            double breath = phrase.peak * (0.72 + 0.28 * std::sin(M_PI * u));
            breath *= std::min(1.0, (t - start) / 0.12);
            breath *= std::clamp((end - t) / 0.45, 0.0, 1.0);
            cue(t, ControlEvent::controller(2, int(std::lround(breath * 127.0))));
        }
        cue(end + 0.02, ControlEvent::controller(2, 0));
        for (int i = 0; i < phrase.notes.size(); ++i) {
            const Note &note = phrase.notes[i];
            const bool last = i == phrase.notes.size() - 1;
            cue(note.start + (i == 0 ? 0.02 : 0.0), ControlEvent::noteOn(note.pitch, 90));
            cue(note.end + (last ? 0.03 : 0.008), ControlEvent::noteOff(note.pitch));
        }
    }
    for (int k = 0; k <= 10; ++k)
        cue(4.2 + k * 0.015, ControlEvent::pitchBend(int(-4096 * (1.0 - k / 10.0))));
    std::stable_sort(m_demo.begin(), m_demo.end(), [](const Cue &a, const Cue &b) { return a.at < b.at; });

    m_demoNext = 0;
    m_demoClock.start();
    m_demoTimer.start();
    emit demoPlayingChanged();
}

void Backend::playDue()
{
    const double now = m_demoClock.elapsed() / 1000.0;
    while (m_demoNext < m_demo.size() && m_demo[m_demoNext].at <= now)
        m_synth->pushUi(m_demo[m_demoNext++].event);
    if (m_demoNext >= m_demo.size()) {
        m_demoTimer.stop();
        emit demoPlayingChanged();
    }
}
