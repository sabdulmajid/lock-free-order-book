#include "../src/concurrent_queue.h"
#include "../src/order_book.h"
#include <benchmark/benchmark.h>
#include <thread>
#include <vector>

template <typename T> static size_t next_pow2(T x) {
    size_t power = 1;
    while (power < static_cast<size_t>(x)) {
        power <<= 1;
    }
    return power;
}

static void BM_ConcurrentOrderBook(benchmark::State& state) {
    const int producers = 4;
    const int orders_per = 10000;
    const size_t total = producers * orders_per;
    for (auto _ : state) {
        state.PauseTiming();
        ConcurrentQueue<Order> queue(next_pow2(total));
        OrderBook book;
        state.ResumeTiming();

        // Producers
        std::vector<std::jthread> threads;
        for (int t = 0; t < producers; ++t) {
            threads.emplace_back([&, t](std::stop_token stop_token) {
                for (int i = 0; i < orders_per; ++i) {
                    Order o(static_cast<uint64_t>(t * orders_per + i), Side::Buy, 100.0, 1);
                    while (!stop_token.stop_requested() && !queue.try_push(o)) {
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
                book.add_order(item.value());
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

BENCHMARK(BM_ConcurrentOrderBook)->Arg(0);
BENCHMARK_MAIN();
