#pragma once

#include <compare>
#include <cstdint>

struct Trade {
    uint64_t taker_order_id{0};
    uint64_t maker_order_id{0};
    uint64_t quantity{0};
    double price{0.0};
    uint64_t timestamp{0};

    Trade() = default;

    Trade(uint64_t taker_id, uint64_t maker_id, uint64_t qty, double p)
        : taker_order_id(taker_id), maker_order_id(maker_id), quantity(qty), price(p), timestamp(0) {}

    auto operator<=>(const Trade&) const = default;
};
