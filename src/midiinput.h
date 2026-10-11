// Finds MIDI controllers through the ALSA sequencer and keeps finding them:
// it subscribes to the sequencer's announce port, so an EWI plugged in while
// the app runs is connected the moment the kernel creates its port.
// "EWI" below means any wind controller: Akai EWI or Roland Aerophone.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <thread>

#include "control.h"

typedef struct _snd_seq snd_seq_t;
typedef struct snd_seq_event snd_seq_event_t;

// The facts the auto-connect policy decides on.
struct MidiPort {
    int client = 0;
    int port = 0;
    QString clientName;
    QString portName;
    bool kernelClient = false; // created by a driver, i.e. a physical device
    bool readable = false;     // can be read from and subscribed to
};

class MidiInput : public QObject {
    Q_OBJECT

public:
    using Sink = std::function<void(const ControlEvent &)>;

    // `sink` runs on the MIDI thread for every musical event.
    explicit MidiInput(Sink sink, QObject *parent = nullptr);
    ~MidiInput() override;

    bool start(); // false, with error() set, when the sequencer can't be opened
    void stop();
    QString error() const { return m_error; }
    int clientId() const { return m_client; }

    static bool shouldAutoConnect(const MidiPort &port, int ownClient);
    static bool looksLikeEwi(const QString &name);
    static bool translate(const snd_seq_event_t &event, ControlEvent &out);

signals:
    void deviceConnected(const QString &name, bool ewi);
    void deviceDisconnected(const QString &name);

private:
    void run();
    void scan();
    void consider(int client, int port);
    void forget(int client);
    void dispatch(const snd_seq_event_t &event);

    Sink m_sink;
    snd_seq_t *m_seq = nullptr;
    int m_client = -1;
    int m_port = -1;
    QString m_error;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    QHash<int, QString> m_connected; // client id → name; MIDI thread only
};
