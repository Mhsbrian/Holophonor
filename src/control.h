// What controllers tell the synth. Events are plain values so they can cross
// from the MIDI and UI threads to the audio thread through lock-free queues.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

struct ControlEvent {
    enum Type : uint8_t { NoteOn, NoteOff, Controller, PitchBend, ChannelPressure, AllNotesOff };
    enum Source : uint8_t { Midi, Keys };

    Type type = NoteOn;
    Source source = Midi;
    uint8_t number = 0; // note or controller number
    int16_t value = 0;  // velocity, controller value, pressure, or bend (-8192..8191)

    static ControlEvent noteOn(int note, int velocity, Source source = Midi)
    {
        return {NoteOn, source, uint8_t(note), int16_t(velocity)};
    }
    static ControlEvent noteOff(int note, Source source = Midi) { return {NoteOff, source, uint8_t(note), 0}; }
    static ControlEvent controller(int number, int value) { return {Controller, Midi, uint8_t(number), int16_t(value)}; }
    static ControlEvent pitchBend(int value) { return {PitchBend, Midi, 0, int16_t(value)}; }
    static ControlEvent pressure(int value) { return {ChannelPressure, Midi, 0, int16_t(value)}; }
    static ControlEvent allNotesOff(Source source = Midi) { return {AllNotesOff, source, 0, 0}; }
};

// Single-producer, single-consumer ring. One producer thread pushes, the
// audio thread pops; neither ever blocks or allocates.
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push(const T &item)
    {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tail.load(std::memory_order_acquire) == Capacity)
            return false;
        m_items[head & (Capacity - 1)] = item;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T &item)
    {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false;
        item = m_items[tail & (Capacity - 1)];
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> m_items{};
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
};
