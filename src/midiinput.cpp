#include "midiinput.h"

#include <QRegularExpression>

#include <alsa/asoundlib.h>
#include <poll.h>

#include <vector>

MidiInput::MidiInput(Sink sink, QObject *parent) : QObject(parent), m_sink(std::move(sink)) {}

MidiInput::~MidiInput() { stop(); }

bool MidiInput::start()
{
    if (m_seq)
        return true;
    int result = snd_seq_open(&m_seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK);
    if (result < 0) {
        m_seq = nullptr;
        m_error = QStringLiteral("MIDI is unavailable: the ALSA sequencer could not be opened (%1)")
                      .arg(QString::fromLocal8Bit(snd_strerror(result)));
        return false;
    }
    snd_seq_set_client_name(m_seq, "Holophonor");
    m_client = snd_seq_client_id(m_seq);
    m_port = snd_seq_create_simple_port(m_seq, "Breath In", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (m_port < 0) {
        m_error = QStringLiteral("MIDI is unavailable: could not create an input port (%1)")
                      .arg(QString::fromLocal8Bit(snd_strerror(m_port)));
        snd_seq_close(m_seq);
        m_seq = nullptr;
        return false;
    }
    // Announcements arrive whenever a port appears or disappears anywhere in
    // the system; this is what makes plugging in an EWI just work.
    snd_seq_connect_from(m_seq, m_port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

    m_error.clear();
    m_running = true;
    m_thread = std::thread([this] { run(); });
    return true;
}

void MidiInput::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    if (m_seq) {
        snd_seq_close(m_seq);
        m_seq = nullptr;
    }
}

bool MidiInput::looksLikeEwi(const QString &name)
{
    // "EWI USB", "EWI5000", "EWI Solo", "EWI-USB": EWI at the start of a word.
    static const QRegularExpression ewi(QStringLiteral("(^|[^a-z])ewi"), QRegularExpression::CaseInsensitiveOption);
    return ewi.match(name).hasMatch();
}

bool MidiInput::shouldAutoConnect(const MidiPort &port, int ownClient)
{
    if (!port.readable || port.client == ownClient || port.client == SND_SEQ_CLIENT_SYSTEM)
        return false;
    const QString name = port.clientName + QLatin1Char(' ') + port.portName;
    // The kernel's loopback port only echoes what other programs send.
    if (name.contains(QLatin1String("Midi Through"), Qt::CaseInsensitive))
        return false;
    if (looksLikeEwi(name))
        return true;
    // Physical devices come from kernel clients. Application ports (a DAW,
    // PipeWire's bridge) stay unconnected so nothing is doubled or looped.
    return port.kernelClient;
}

bool MidiInput::translate(const snd_seq_event_t &event, ControlEvent &out)
{
    switch (event.type) {
    case SND_SEQ_EVENT_NOTEON:
        out = ControlEvent::noteOn(event.data.note.note, event.data.note.velocity);
        return true;
    case SND_SEQ_EVENT_NOTEOFF:
        out = ControlEvent::noteOff(event.data.note.note);
        return true;
    case SND_SEQ_EVENT_CONTROLLER:
        out = ControlEvent::controller(int(event.data.control.param), event.data.control.value);
        return true;
    case SND_SEQ_EVENT_PITCHBEND:
        out = ControlEvent::pitchBend(event.data.control.value);
        return true;
    case SND_SEQ_EVENT_CHANPRESS:
        out = ControlEvent::pressure(event.data.control.value);
        return true;
    default:
        return false;
    }
}

void MidiInput::run()
{
    scan();
    std::vector<pollfd> fds(size_t(snd_seq_poll_descriptors_count(m_seq, POLLIN)));
    snd_seq_poll_descriptors(m_seq, fds.data(), unsigned(fds.size()), POLLIN);

    while (m_running) {
        // A short timeout lets stop() end the thread without a wake-up pipe.
        if (poll(fds.data(), nfds_t(fds.size()), 150) <= 0)
            continue;
        snd_seq_event_t *event = nullptr;
        int result;
        while ((result = snd_seq_event_input(m_seq, &event)) >= 0 || result == -ENOSPC) {
            if (result >= 0 && event)
                dispatch(*event);
        }
    }
}

void MidiInput::dispatch(const snd_seq_event_t &event)
{
    switch (event.type) {
    case SND_SEQ_EVENT_PORT_START:
        consider(event.data.addr.client, event.data.addr.port);
        return;
    case SND_SEQ_EVENT_PORT_EXIT:
    case SND_SEQ_EVENT_CLIENT_EXIT:
        forget(event.data.addr.client);
        return;
    default:
        break;
    }
    ControlEvent control;
    if (translate(event, control) && m_sink)
        m_sink(control);
}

void MidiInput::scan()
{
    snd_seq_client_info_t *client;
    snd_seq_port_info_t *port;
    snd_seq_client_info_alloca(&client);
    snd_seq_port_info_alloca(&port);
    snd_seq_client_info_set_client(client, -1);
    while (snd_seq_query_next_client(m_seq, client) >= 0) {
        const int id = snd_seq_client_info_get_client(client);
        snd_seq_port_info_set_client(port, id);
        snd_seq_port_info_set_port(port, -1);
        while (snd_seq_query_next_port(m_seq, port) >= 0)
            consider(id, snd_seq_port_info_get_port(port));
    }
}

void MidiInput::consider(int clientId, int portId)
{
    // One port per device: MIDI 2.0 endpoints expose an aggregate port plus
    // one per group, and connecting both would double every note.
    if (m_connected.contains(clientId))
        return;

    snd_seq_client_info_t *client;
    snd_seq_port_info_t *port;
    snd_seq_client_info_alloca(&client);
    snd_seq_port_info_alloca(&port);
    if (snd_seq_get_any_client_info(m_seq, clientId, client) < 0 ||
        snd_seq_get_any_port_info(m_seq, clientId, portId, port) < 0)
        return;

    const unsigned caps = snd_seq_port_info_get_capability(port);
    MidiPort candidate;
    candidate.client = clientId;
    candidate.port = portId;
    candidate.clientName = QString::fromUtf8(snd_seq_client_info_get_name(client));
    candidate.portName = QString::fromUtf8(snd_seq_port_info_get_name(port));
    candidate.kernelClient = snd_seq_client_info_get_type(client) == SND_SEQ_KERNEL_CLIENT;
    candidate.readable = (caps & SND_SEQ_PORT_CAP_READ) && (caps & SND_SEQ_PORT_CAP_SUBS_READ) &&
                         !(caps & SND_SEQ_PORT_CAP_NO_EXPORT);
    if (!shouldAutoConnect(candidate, m_client))
        return;
    if (snd_seq_connect_from(m_seq, m_port, clientId, portId) < 0)
        return;

    m_connected.insert(clientId, candidate.clientName);
    emit deviceConnected(candidate.clientName,
                         looksLikeEwi(candidate.clientName + QLatin1Char(' ') + candidate.portName));
}

void MidiInput::forget(int clientId)
{
    const auto it = m_connected.find(clientId);
    if (it == m_connected.end())
        return;
    const QString name = it.value();
    m_connected.erase(it);
    emit deviceDisconnected(name);
}
