#pragma once

#include "low_latency.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <vector>

template <low_latency::OrderLike T> class ConcurrentQueue {
  public:
    explicit ConcurrentQueue(std::size_t capacity) : buffer_(capacity), capacity_(capacity), mask_(capacity - 1) {
        if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
            std::terminate();
        }

        for (std::size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool try_push(const T& data) noexcept {
        Cell* cell = nullptr;
        std::size_t pos = enqueue_pos_.value.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                if (enqueue_pos_.value.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = enqueue_pos_.value.load(std::memory_order_relaxed);
            }
        }

        cell->data = data;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> try_pop() noexcept {
        Cell* cell = nullptr;
        std::size_t pos = dequeue_pos_.value.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            if (diff == 0) {
                if (dequeue_pos_.value.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return std::nullopt;
            } else {
                pos = dequeue_pos_.value.load(std::memory_order_relaxed);
            }
        }

        T data = cell->data;
        cell->sequence.store(pos + capacity_, std::memory_order_release);
        return data;
    }

  private:
    struct Cell {
        std::atomic<std::size_t> sequence{0};
        T data{};
    };

    struct alignas(low_latency::cache_line_size) CacheAlignedCursor {
        std::atomic<std::size_t> value{0};
    };

    static_assert(alignof(CacheAlignedCursor) >= low_latency::cache_line_size);
    static_assert(sizeof(CacheAlignedCursor) >= low_latency::cache_line_size);

    std::vector<Cell> buffer_;
    std::size_t capacity_{0};
    std::size_t mask_{0};
    CacheAlignedCursor enqueue_pos_;
    CacheAlignedCursor dequeue_pos_;
};
