#include "audioengine.h"

#include <QMetaObject>
#include <QPointer>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include "synth.h"

struct AudioEngine::PipeWire {
    AudioEngine *engine = nullptr;
    Synth *synth = nullptr;
    pw_thread_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    pw_stream_events events{};
    bool failed = false; // set on the loop thread, read with the loop locked

    static void process(void *data)
    {
        auto *self = static_cast<PipeWire *>(data);
        pw_buffer *buffer = pw_stream_dequeue_buffer(self->stream);
        if (!buffer)
            return;
        spa_data &plane = buffer->buffer->datas[0];
        if (auto *samples = static_cast<float *>(plane.data)) {
            constexpr uint32_t stride = sizeof(float) * 2;
            uint32_t frames = plane.maxsize / stride;
            if (buffer->requested)
                frames = std::min<uint32_t>(uint32_t(buffer->requested), frames);
            self->synth->render(samples, int(frames));
            plane.chunk->offset = 0;
            plane.chunk->stride = stride;
            plane.chunk->size = frames * stride;
        }
        pw_stream_queue_buffer(self->stream, buffer);
    }

    static void stateChanged(void *data, pw_stream_state, pw_stream_state state, const char *error)
    {
        auto *self = static_cast<PipeWire *>(data);
        if (state != PW_STREAM_STATE_ERROR || self->failed)
            return;
        self->failed = true;
        const QString why = QString::fromUtf8(error ? error : "stream error");
        QPointer<AudioEngine> engine = self->engine;
        QMetaObject::invokeMethod(
            self->engine, [engine, why] {
                if (engine)
                    engine->pipeWireFailed(why);
            },
            Qt::QueuedConnection);
    }
};

AudioEngine::AudioEngine(Synth &synth, QObject *parent) : QObject(parent), m_synth(synth) {}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::start(bool silent)
{
    stop();
    if (silent) {
        startSilentClock();
        m_status = QStringLiteral("silent");
        m_error.clear();
        emit changed();
        return;
    }
    QString why;
    if (startPipeWire(why)) {
        m_status = QStringLiteral("PipeWire · %1 kHz · %2 frames").arg(kSampleRate / 1000).arg(kQuantum);
        m_error.clear();
        emit changed();
    } else {
        pipeWireFailed(why);
    }
}

void AudioEngine::stop()
{
    stopPipeWire();
    stopSilentClock();
}

bool AudioEngine::startPipeWire(QString &why)
{
    pw_init(nullptr, nullptr);
    auto pw = std::make_unique<PipeWire>();
    pw->engine = this;
    pw->synth = &m_synth;
    pw->events.version = PW_VERSION_STREAM_EVENTS;
    pw->events.process = &PipeWire::process;
    pw->events.state_changed = &PipeWire::stateChanged;

    pw->loop = pw_thread_loop_new("holophonor-audio", nullptr);
    if (!pw->loop) {
        why = QStringLiteral("could not create the PipeWire loop");
        return false;
    }
    const QByteArray latency = QByteArray::number(kQuantum) + '/' + QByteArray::number(kSampleRate);
    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                             PW_KEY_MEDIA_ROLE, "Music", PW_KEY_APP_NAME, "Holophonor",
                                             PW_KEY_NODE_NAME, "holophonor", PW_KEY_NODE_DESCRIPTION, "Holophonor",
                                             PW_KEY_NODE_LATENCY, latency.constData(), nullptr);

    pw_thread_loop_lock(pw->loop);
    pw->stream = pw_stream_new_simple(pw_thread_loop_get_loop(pw->loop), "Holophonor", props, &pw->events, pw.get());
    if (!pw->stream) {
        pw_thread_loop_unlock(pw->loop);
        pw_thread_loop_destroy(pw->loop);
        why = QStringLiteral("could not create a PipeWire stream");
        return false;
    }

    uint8_t podBuffer[1024];
    spa_pod_builder builder;
    spa_pod_builder_init(&builder, podBuffer, sizeof podBuffer);
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kSampleRate;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};

    const int result = pw_stream_connect(
        pw->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params,
        1);
    pw_thread_loop_unlock(pw->loop);
    if (result < 0) {
        pw_stream_destroy(pw->stream);
        pw_thread_loop_destroy(pw->loop);
        why = QStringLiteral("PipeWire refused the stream (%1)").arg(QString::fromLocal8Bit(spa_strerror(result)));
        return false;
    }
    if (pw_thread_loop_start(pw->loop) < 0) {
        pw_stream_destroy(pw->stream);
        pw_thread_loop_destroy(pw->loop);
        why = QStringLiteral("could not start the PipeWire loop");
        return false;
    }
    m_pipewire = std::move(pw);
    return true;
}

void AudioEngine::stopPipeWire()
{
    if (!m_pipewire)
        return;
    pw_thread_loop_stop(m_pipewire->loop);
    pw_stream_destroy(m_pipewire->stream);
    pw_thread_loop_destroy(m_pipewire->loop);
    m_pipewire.reset();
}

void AudioEngine::pipeWireFailed(const QString &why)
{
    stopPipeWire();
    startSilentClock();
    m_status = QStringLiteral("silent");
    m_error = QStringLiteral("No sound — PipeWire: %1").arg(why);
    emit changed();
}

// Renders in real time and throws the samples away, so a missing sound server
// never freezes the instrument's visuals.
void AudioEngine::startSilentClock()
{
    if (m_clockRunning)
        return;
    m_clockRunning = true;
    m_clock = std::thread([this] {
        constexpr int frames = 256;
        std::vector<float> scratch(frames * 2);
        const auto period = std::chrono::microseconds(1000000LL * frames / kSampleRate);
        auto next = std::chrono::steady_clock::now();
        while (m_clockRunning) {
            m_synth.render(scratch.data(), frames);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });
}

void AudioEngine::stopSilentClock()
{
    m_clockRunning = false;
    if (m_clock.joinable())
        m_clock.join();
}
