#include "order_book.h"

#include <algorithm>
#include <chrono>

namespace {
constexpr low_latency::EngineError InvalidOrder{"invalid_order", "order price and quantity must be positive"};

constexpr low_latency::EngineError TradeBatchOverflow{"trade_batch_overflow", "fixed-capacity trade batch exhausted"};
} // namespace

OrderBook::OrderBook() = default;

low_latency::expected<std::reference_wrapper<Order>, low_latency::EngineError> OrderBook::validate_order(Order& order) {
    if (order.price <= 0.0 || order.quantity == 0) {
        return low_latency::make_unexpected(InvalidOrder);
    }
    return std::ref(order);
}

low_latency::expected<TradeBatch, low_latency::EngineError> OrderBook::match_order(Order& order) {
    TradeBatch trades;

    if (order.quantity == 0) {
        return trades;
    }

    LFOB_ASSUME(order.quantity > 0);

    if (order.side == Side::Buy) {
        for (auto it = asks_.begin(); it != asks_.end();) {
            const double price = it->first;
            auto& price_level = it->second;
            if (order.price < price || order.quantity == 0) {
                break;
            }

            for (auto& maker_node : price_level.orders) {
                if (order.quantity == 0) {
                    break;
                }

                auto guard = order_smr_.make_guard();
                auto* protected_node = guard.protect(maker_node.get());
                if (protected_node == nullptr || protected_node->order.quantity == 0) {
                    continue;
                }

                auto& maker_order = protected_node->order;
                LFOB_ASSUME(maker_order.quantity > 0);

                const uint64_t trade_quantity = std::min(order.quantity, maker_order.quantity);
                if (!trades.try_emplace_back(order.order_id, maker_order.order_id, trade_quantity, price)) {
                    return low_latency::make_unexpected(TradeBatchOverflow);
                }

                order.quantity -= trade_quantity;
                maker_order.quantity -= trade_quantity;
                price_level.total_quantity -= trade_quantity;
            }

            auto removable = std::remove_if(price_level.orders.begin(), price_level.orders.end(), [](const auto& node) {
                return node == nullptr || node->order.quantity == 0;
            });
            for (auto retire_it = removable; retire_it != price_level.orders.end(); ++retire_it) {
                order_smr_.retire(std::move(*retire_it));
            }
            price_level.orders.erase(removable, price_level.orders.end());

            if (price_level.orders.empty()) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        for (auto it = bids_.begin(); it != bids_.end();) {
            const double price = it->first;
            auto& price_level = it->second;
            if (order.price > price || order.quantity == 0) {
                break;
            }

            for (auto& maker_node : price_level.orders) {
                if (order.quantity == 0) {
                    break;
                }

                auto guard = order_smr_.make_guard();
                auto* protected_node = guard.protect(maker_node.get());
                if (protected_node == nullptr || protected_node->order.quantity == 0) {
                    continue;
                }

                auto& maker_order = protected_node->order;
                LFOB_ASSUME(maker_order.quantity > 0);

                const uint64_t trade_quantity = std::min(order.quantity, maker_order.quantity);
                if (!trades.try_emplace_back(order.order_id, maker_order.order_id, trade_quantity, price)) {
                    return low_latency::make_unexpected(TradeBatchOverflow);
                }

                order.quantity -= trade_quantity;
                maker_order.quantity -= trade_quantity;
                price_level.total_quantity -= trade_quantity;
            }

            auto removable = std::remove_if(price_level.orders.begin(), price_level.orders.end(), [](const auto& node) {
                return node == nullptr || node->order.quantity == 0;
            });
            for (auto retire_it = removable; retire_it != price_level.orders.end(); ++retire_it) {
                order_smr_.retire(std::move(*retire_it));
            }
            price_level.orders.erase(removable, price_level.orders.end());

            if (price_level.orders.empty()) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
    }

    return trades;
}

low_latency::expected<TradeBatch, low_latency::EngineError> OrderBook::add_validated_order(Order& order) {
    order.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());

    auto trades = match_order(order);
    if (!trades) {
        return low_latency::make_unexpected(trades.error());
    }

    if (order.quantity > 0) {
        auto* price_level = order.side == Side::Buy ? bids_.upsert(order.price) : asks_.upsert(order.price);
        if (price_level != nullptr) {
            price_level->total_quantity += order.quantity;
            price_level->orders.push_back(std::make_unique<OrderNode>(order));
        }
    }

    return std::move(trades).value();
}

low_latency::expected<TradeBatch, low_latency::EngineError> OrderBook::try_add_order(Order& order) {
    auto validated = validate_order(order);
    return validated.and_then(
        [this](std::reference_wrapper<Order> validated_order) { return add_validated_order(validated_order.get()); });
}

TradeBatch OrderBook::add_order(Order& order) {
    auto result = try_add_order(order);
    if (!result) {
        return {};
    }
    return std::move(result).value();
}

template <class Levels> bool OrderBook::cancel_from(Levels& levels, uint64_t order_id, double price) {
    auto level_it = levels.find(price);
    if (level_it == levels.end()) {
        return false;
    }

    auto& price_level = level_it->second;
    auto& orders = price_level.orders;
    for (auto order_it = orders.begin(); order_it != orders.end(); ++order_it) {
        auto guard = order_smr_.make_guard();
        auto* node = guard.protect(order_it->get());
        if (node != nullptr && node->order.order_id == order_id) {
            price_level.total_quantity -= node->order.quantity;
            auto retired = std::move(*order_it);
            orders.erase(order_it);
            order_smr_.retire(std::move(retired));

            if (orders.empty()) {
                levels.erase(level_it);
            }
            return true;
        }
    }

    return false;
}

bool OrderBook::cancel_order(uint64_t order_id, Side side, double price) {
    return side == Side::Buy ? cancel_from(bids_, order_id, price) : cancel_from(asks_, order_id, price);
}

template <class Levels>
bool OrderBook::modify_in(Levels& levels, uint64_t order_id, double price, uint64_t new_quantity) {
    auto level_it = levels.find(price);
    if (level_it == levels.end()) {
        return false;
    }

    auto& price_level = level_it->second;
    for (auto& maker_node : price_level.orders) {
        auto guard = order_smr_.make_guard();
        auto* node = guard.protect(maker_node.get());
        if (node != nullptr && node->order.order_id == order_id) {
            price_level.total_quantity -= node->order.quantity;
            node->order.quantity = new_quantity;
            price_level.total_quantity += new_quantity;
            return true;
        }
    }

    return false;
}

bool OrderBook::modify_order(uint64_t order_id, Side side, double price, uint64_t new_quantity) {
    return side == Side::Buy ? modify_in(bids_, order_id, price, new_quantity)
                             : modify_in(asks_, order_id, price, new_quantity);
}

std::optional<double> OrderBook::get_best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<double> OrderBook::get_best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

template <class Levels> DepthSnapshot OrderBook::snapshot_side(const Levels& levels) const {
    DepthSnapshot snapshot;
    for (const auto& [price, price_level] : levels) {
        if (!snapshot.try_emplace_back(
                price, price_level.total_quantity, static_cast<uint64_t>(price_level.orders.size()))) {
            break;
        }
    }
    return snapshot;
}

DepthSnapshot OrderBook::snapshot_bids() const {
    return snapshot_side(bids_);
}

DepthSnapshot OrderBook::snapshot_asks() const {
    return snapshot_side(asks_);
}
