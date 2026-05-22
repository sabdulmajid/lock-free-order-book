#include "../src/concurrent_queue.h"
#include "../src/low_latency.h"
#include "../src/order.h"
#include "../src/stdin_parser.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic_bool g_count_allocations{false};
std::atomic<std::size_t> g_allocations{0};

void* allocate(std::size_t size) {
    if (g_count_allocations.load(std::memory_order_relaxed)) {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* ptr = std::malloc(size)) {
        return ptr;
    }
    throw std::bad_alloc();
}

void deallocate(void* ptr) noexcept {
    std::free(ptr);
}

struct SmrNode {
    uint64_t value{0};
};

bool parser_is_allocation_free() {
    const std::string message = "{\"action\":\"batch\",\"orders\":["
                                "{\"id\":1,\"side\":\"buy\",\"price\":100.25,\"quantity\":10},"
                                "{\"id\":2,\"side\":\"sell\",\"price\":100.50,\"quantity\":20}"
                                "]}";
    std::array<Order, 2> parsed{};
    std::size_t parsed_count = 0;
    const std::span<const char> bytes(message.data(), message.size());

    g_allocations.store(0, std::memory_order_relaxed);
    g_count_allocations.store(true, std::memory_order_release);
    auto result = stdin_parser::parse_batch_orders(bytes, [&](Order& order) {
        if (parsed_count >= parsed.size()) {
            return low_latency::expected<bool, low_latency::EngineError>(
                low_latency::make_unexpected(stdin_parser::MalformedJson));
        }
        parsed[parsed_count++] = order;
        return low_latency::expected<bool, low_latency::EngineError>(true);
    });
    g_count_allocations.store(false, std::memory_order_release);

    return result && result.value() == parsed.size() && parsed_count == parsed.size() && parsed[0].order_id == 1 &&
           parsed[1].side == Side::Sell && g_allocations.load(std::memory_order_relaxed) == 0;
}

bool mpsc_queue_stress() {
    constexpr std::size_t producers = 4;
    constexpr std::size_t per_producer = 75000;
    constexpr std::size_t total = producers * per_producer;
    constexpr std::size_t capacity = 1 << 15;

    ConcurrentQueue<Order> queue(capacity);
    std::vector<unsigned char> seen(total, 0);
    std::vector<std::jthread> workers;
    workers.reserve(producers);

    for (std::size_t producer = 0; producer < producers; ++producer) {
        workers.emplace_back([&, producer](std::stop_token stop_token) {
            const auto base = producer * per_producer;
            for (std::size_t i = 0; i < per_producer && !stop_token.stop_requested(); ++i) {
                Order order(static_cast<uint64_t>(base + i), Side::Buy, 100.0, 1);
                while (!stop_token.stop_requested() && !queue.try_push(order)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::size_t consumed = 0;
    while (consumed < total && std::chrono::steady_clock::now() < deadline) {
        auto order = queue.try_pop();
        if (!order) {
            std::this_thread::yield();
            continue;
        }

        const auto id = static_cast<std::size_t>(order->order_id);
        if (id >= seen.size() || seen[id] != 0) {
            return false;
        }
        seen[id] = 1;
        ++consumed;
    }

    for (auto& worker : workers) {
        worker.request_stop();
    }

    if (consumed != total) {
        return false;
    }

    for (const auto flag : seen) {
        if (flag == 0) {
            return false;
        }
    }
    return true;
}

bool hazard_pointer_stress() {
    low_latency::HazardPointerDomain<SmrNode, 128, 32> domain;
    std::atomic<SmrNode*> published{new SmrNode{0}};
    std::atomic_bool writer_done{false};
    std::atomic<uint64_t> read_checksum{0};

    std::jthread writer([&](std::stop_token stop_token) {
        for (uint64_t value = 1; value <= 100000 && !stop_token.stop_requested(); ++value) {
            auto* next = new SmrNode{value};
            auto* old = published.exchange(next, std::memory_order_acq_rel);
            domain.retire(std::unique_ptr<SmrNode>(old));
            if ((value & 0x3ffU) == 0) {
                domain.scan();
            }
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> readers;
    for (std::size_t i = 0; i < 4; ++i) {
        readers.emplace_back([&](std::stop_token stop_token) {
            while (!stop_token.stop_requested() && !writer_done.load(std::memory_order_acquire)) {
                auto guard = domain.make_guard();
                SmrNode* protected_node = nullptr;
                do {
                    protected_node = published.load(std::memory_order_acquire);
                    guard.protect(protected_node);
                } while (protected_node != published.load(std::memory_order_acquire));

                if (protected_node != nullptr) {
                    read_checksum.fetch_add(protected_node->value, std::memory_order_relaxed);
                }
            }
        });
    }

    writer.join();
    for (auto& reader : readers) {
        reader.request_stop();
    }
    domain.retire(std::unique_ptr<SmrNode>(published.exchange(nullptr, std::memory_order_acq_rel)));
    domain.scan();

    return writer_done.load(std::memory_order_acquire) && read_checksum.load(std::memory_order_relaxed) > 0;
}
} // namespace

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void operator delete(void* ptr) noexcept {
    deallocate(ptr);
}

void operator delete[](void* ptr) noexcept {
    deallocate(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    deallocate(ptr);
}

int main() {
    if (!parser_is_allocation_free()) {
        std::cerr << "stdin parser allocated or parsed incorrectly\n";
        return 1;
    }
    if (!mpsc_queue_stress()) {
        std::cerr << "MPSC queue stress failed\n";
        return 1;
    }
    if (!hazard_pointer_stress()) {
        std::cerr << "hazard pointer stress failed\n";
        return 1;
    }

    low_latency::emit_line("low_latency_stress passed");
    return 0;
}
