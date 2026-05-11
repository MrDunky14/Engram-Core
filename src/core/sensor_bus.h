#pragma once
// Lock-free SPSC (Single Producer Single Consumer) Ring Buffer for Sensor Bus
// Designed for high-throughput UDP packet ingestion

#include <atomic>
#include <vector>
#include <cstdint>

template <typename T, size_t Size>
class SensorBus {
private:
    T buffer[Size];
    std::atomic<size_t> head; // Written by producer (UDP thread)
    std::atomic<size_t> tail; // Read by consumer (Cognitive Loop)

public:
    SensorBus() : head(0), tail(0) {}

    // Push data into the ring buffer (Producer)
    // Returns false if buffer is full (frame dropped)
    bool push(const T& item) {
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) % Size;

        if (next_head == tail.load(std::memory_order_acquire)) {
            // Buffer full, drop frame to maintain real-time
            return false;
        }

        buffer[current_head] = item;
        head.store(next_head, std::memory_order_release);
        return true;
    }

    // Pop data from the ring buffer (Consumer)
    // Returns false if buffer is empty
    bool pop(T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);

        if (current_tail == head.load(std::memory_order_acquire)) {
            // Buffer empty
            return false;
        }

        item = buffer[current_tail];
        tail.store((current_tail + 1) % Size, std::memory_order_release);
        return true;
    }

    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        if (h >= t) return h - t;
        return Size - t + h;
    }
};
