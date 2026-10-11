// Pulls audio from the synth. PipeWire is Arch's sound server, so the synth
// renders straight into a low-latency PipeWire stream. When PipeWire can't be
// reached a silent clock keeps the synth (and so the visuals) running, and the
// reason is shown in the window instead of the app quietly doing less.
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>

class Synth;

class AudioEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kQuantum = 128; // frames: 2.7 ms at 48 kHz

    explicit AudioEngine(Synth &synth, QObject *parent = nullptr);
    ~AudioEngine() override;

    // Uses PipeWire unless `silent` is set (tests, screenshots) or it fails.
    void start(bool silent = false);
    void stop();

    QString status() const { return m_status; }
    QString error() const { return m_error; }

signals:
    void changed();

private:
    struct PipeWire;

    bool startPipeWire(QString &why);
    void stopPipeWire();
    void startSilentClock();
    void stopSilentClock();
    void pipeWireFailed(const QString &why);

    Synth &m_synth;
    std::unique_ptr<PipeWire> m_pipewire;
    std::thread m_clock;
    std::atomic<bool> m_clockRunning{false};
    QString m_status;
    QString m_error;
};
