#include "../src/order_book.h"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>

static void BM_Add_10k_Orders(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book;
        std::vector<Order> orders;
        orders.reserve(10000);
        for (int i = 0; i < 10000; ++i) {
            orders.emplace_back(i, Side::Buy, 100.0, 10);
        }
        state.ResumeTiming();

        for (auto& order : orders) {
            auto trades = book.add_order(order);
            benchmark::DoNotOptimize(trades);
        }
    }
}
BENCHMARK(BM_Add_10k_Orders);

static void BM_Cancel_1k_Orders(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book;
        std::vector<Order> orders;
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 9999);

        for (int i = 0; i < 10000; ++i) {
            Order order(i, Side::Buy, 100.0 + (i % 10), 10);
            orders.push_back(order);
            book.add_order(order);
        }

        std::vector<int> indices_to_cancel;
        indices_to_cancel.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            indices_to_cancel.push_back(dist(rng));
        }
        state.ResumeTiming();

        for (int idx : indices_to_cancel) {
            Order& order_to_cancel = orders[idx];
            book.cancel_order(order_to_cancel.order_id, order_to_cancel.side, order_to_cancel.price);
        }
    }
}
BENCHMARK(BM_Cancel_1k_Orders);

static void BM_Modify_1k_Orders(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book;
        std::vector<Order> orders;
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 9999);
        std::uniform_int_distribution<uint64_t> qty_dist(1, 200);

        for (int i = 0; i < 10000; ++i) {
            Order order(i, Side::Buy, 100.0 + (i % 10), 10);
            orders.push_back(order);
            book.add_order(order);
        }

        std::vector<int> indices_to_modify;
        indices_to_modify.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            indices_to_modify.push_back(dist(rng));
        }
        state.ResumeTiming();

        for (int idx : indices_to_modify) {
            Order& order_to_modify = orders[idx];
            book.modify_order(order_to_modify.order_id, order_to_modify.side, order_to_modify.price, qty_dist(rng));
        }
    }
}
BENCHMARK(BM_Modify_1k_Orders);

static void BM_MatchingEngine(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book;
        int num_orders = state.range(0);
        for (int i = 0; i < num_orders; ++i) {
            Order order(i, Side::Sell, 100.0 + (i % 10), 10);
            book.add_order(order);
        }
        Order buy_order(num_orders, Side::Buy, 110.0, num_orders * 10);
        state.ResumeTiming();

        auto trades = book.add_order(buy_order);
        benchmark::DoNotOptimize(trades);
    }
}
BENCHMARK(BM_MatchingEngine)->Range(1000, 100000);

BENCHMARK_MAIN();
