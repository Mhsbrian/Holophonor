// The instrument as QML sees it: which voice is selected, which controller is
// connected, what is sounding right now. Owns the synth, audio and MIDI.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <memory>

#include "audioengine.h"
#include "control.h"
#include "instrumentmodel.h"
#include "midiinput.h"
#include "synth.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(InstrumentModel *instruments READ instruments CONSTANT)
    Q_PROPERTY(int instrument READ instrument WRITE setInstrument NOTIFY instrumentChanged)
    Q_PROPERTY(QString instrumentName READ instrumentName NOTIFY instrumentChanged)
    Q_PROPERTY(QString instrumentFamily READ instrumentFamily NOTIFY instrumentChanged)
    Q_PROPERTY(int visual READ visual WRITE setVisual NOTIFY visualChanged)

    Q_PROPERTY(QString device READ device NOTIFY deviceChanged)
    Q_PROPERTY(bool ewi READ ewi NOTIFY deviceChanged)
    Q_PROPERTY(QString midiError READ midiError NOTIFY deviceChanged)
    Q_PROPERTY(QString audioError READ audioError NOTIFY audioChanged)
    Q_PROPERTY(QString audioStatus READ audioStatus NOTIFY audioChanged)

    Q_PROPERTY(QString noteName READ noteName NOTIFY liveChanged)
    Q_PROPERTY(int cents READ cents NOTIFY liveChanged)
    Q_PROPERTY(qreal level READ level NOTIFY liveChanged)
    Q_PROPERTY(bool sounding READ sounding NOTIFY liveChanged)
    Q_PROPERTY(QString breathSource READ breathSource NOTIFY liveChanged)
    Q_PROPERTY(bool hasPlayed READ hasPlayed NOTIFY hasPlayedChanged)
    Q_PROPERTY(int octave READ octave NOTIFY octaveChanged)
    Q_PROPERTY(bool demoPlaying READ demoPlaying NOTIFY demoPlayingChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    // Starts sound and MIDI. `silent` renders without a sound server.
    void start(bool silent);

    Synth::Snapshot snapshot() const { return m_synth->snapshot(); }
    Synth &synth() { return *m_synth; }

    InstrumentModel *instruments() { return &m_instruments; }
    int instrument() const { return m_instrument; }
    void setInstrument(int index);
    QString instrumentName() const;
    QString instrumentFamily() const;
    int visual() const { return m_visual; }
    void setVisual(int visual);

    QString device() const;
    bool ewi() const;
    QString midiError() const { return m_midiError; }
    QString audioError() const { return m_audio.error(); }
    QString audioStatus() const { return m_audio.status(); }

    QString noteName() const { return m_noteName; }
    int cents() const { return m_cents; }
    qreal level() const { return m_level; }
    bool sounding() const { return m_sounding; }
    QString breathSource() const { return m_breathSource; }
    bool hasPlayed() const { return m_hasPlayed; }
    int octave() const { return m_octave; }
    bool demoPlaying() const { return m_demoTimer.isActive(); }

    // The computer keyboard as a practice instrument. Return true when the
    // key belongs to the instrument, so QML can accept the event.
    Q_INVOKABLE bool keyDown(int key);
    Q_INVOKABLE bool keyUp(int key);
    Q_INVOKABLE void releaseKeys();
    Q_INVOKABLE void panic();
    Q_INVOKABLE void stepInstrument(int delta);
    Q_INVOKABLE void playDemo();

    // The note a key plays (Ableton-style rows: A W S E D F T G Y H U J K…),
    // or -1. Octave 0 puts A on middle C.
    static int noteForKey(int key, int octave);

signals:
    void instrumentChanged();
    void visualChanged();
    void deviceChanged();
    void audioChanged();
    void liveChanged();
    void hasPlayedChanged();
    void octaveChanged();
    void demoPlayingChanged();
    void toast(const QString &message);

private:
    struct Device {
        QString name;
        bool ewi = false;
    };
    struct Cue {
        double at = 0.0; // seconds from the start of the demo
        ControlEvent event;
    };

    void refreshLive();
    void playDue();
    void saveSettings() const;

    std::unique_ptr<Synth> m_synth;
    AudioEngine m_audio;
    MidiInput m_midi;
    InstrumentModel m_instruments;
    int m_instrument = 0;
    int m_visual = 0;
    QVector<Device> m_devices;
    QString m_midiError;

    QTimer m_liveTimer;
    QString m_noteName;
    int m_cents = 0;
    qreal m_level = 0.0;
    bool m_sounding = false;
    QString m_breathSource;
    bool m_hasPlayed = false;

    QHash<int, int> m_keysDown; // key → note
    int m_octave = 0;

    QTimer m_demoTimer;
    QElapsedTimer m_demoClock;
    QVector<Cue> m_demo;
    int m_demoNext = 0;
};
