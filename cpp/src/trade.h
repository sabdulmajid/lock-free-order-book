#pragma once

#include <cstdint>
#include <compare>

struct Trade {
    uint64_t taker_order_id;
    uint64_t maker_order_id;
    uint64_t quantity;
    double price;
    uint64_t timestamp;

    Trade(uint64_t taker_id, uint64_t maker_id, uint64_t qty, double p)
        : taker_order_id(taker_id), maker_order_id(maker_id), quantity(qty), price(p), timestamp(0) {}

    auto operator<=>(const Trade&) const = default;
};
