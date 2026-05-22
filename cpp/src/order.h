#pragma once

#include "low_latency.h"

#include <compare>
#include <cstdint>
#include <iosfwd>

enum class Side { Buy, Sell };

struct Order {
    uint64_t order_id;
    Side side;
    double price;
    uint64_t quantity;
    uint64_t timestamp;

    Order() : order_id(0), side(Side::Buy), price(0.0), quantity(0), timestamp(0) {}

    Order(uint64_t id, Side s, double p, uint64_t qty) : order_id(id), side(s), price(p), quantity(qty), timestamp(0) {}

    auto operator<=>(const Order&) const = default;
};

struct OrderKey {
    double price{0.0};
    uint64_t timestamp{0};
    uint64_t order_id{0};

    auto operator<=>(const OrderKey&) const = default;
};

static_assert(low_latency::OrderLike<Order>);
static_assert(std::three_way_comparable<OrderKey>);

std::ostream& operator<<(std::ostream& os, const Order& order);
