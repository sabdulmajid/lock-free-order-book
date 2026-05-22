#include "../src/concurrent_queue.h"
#include "../src/order.h"
#include <benchmark/benchmark.h>
#include <thread>
#include <vector>

static size_t next_pow2(size_t x) {
    size_t power = 1;
    while (power < x) {
        power <<= 1;
    }
    return power;
}

template <typename T> static void BM_SPSC_Queue(benchmark::State& state) {
    size_t total = state.range(0);
    for (auto _ : state) {
        ConcurrentQueue<T> queue(next_pow2(total));
        // Producer
        std::jthread prod([&](std::stop_token stop_token) {
            for (size_t i = 0; i < total; ++i) {
                T data(i, Side::Buy, 100.0, 1);
                while (!stop_token.stop_requested() && !queue.try_push(data)) {
                    std::this_thread::yield();
                }
            }
        });

        // Consumer
        size_t count = 0;
        while (count < total) {
            auto item = queue.try_pop();
            if (item.has_value()) {
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
        prod.request_stop();
        prod.join();
    }
}

template <typename T> static void BM_MPSC_Queue(benchmark::State& state) {
    int producers = 4;
    size_t per = state.range(0);
    size_t total = producers * per;
    for (auto _ : state) {
        ConcurrentQueue<T> queue(next_pow2(total));
        // Producers
        std::vector<std::jthread> threads;
        for (int t = 0; t < producers; ++t) {
            threads.emplace_back([&, t](std::stop_token stop_token) {
                for (size_t i = 0; i < per; ++i) {
                    T data(t * per + i, Side::Buy, 100.0, 1);
                    while (!stop_token.stop_requested() && !queue.try_push(data)) {
                        std::this_thread::yield();
                    }
                }
            });
        }

        // Consumer
        size_t count = 0;
        while (count < total) {
            auto item = queue.try_pop();
            if (item.has_value()) {
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
        for (auto& th : threads) {
            th.request_stop();
            th.join();
        }
    }
}

BENCHMARK_TEMPLATE(BM_SPSC_Queue, Order)->Arg(100000);
BENCHMARK_TEMPLATE(BM_MPSC_Queue, Order)->Arg(50000);

BENCHMARK_MAIN();
