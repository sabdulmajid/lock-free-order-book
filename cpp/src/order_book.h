#pragma once

#include "low_latency.h"
#include "order.h"
#include "trade.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

inline constexpr std::size_t MaxDepthLevels = 20;
inline constexpr std::size_t MaxTradesPerOrder = 512;

struct OrderNode {
    explicit OrderNode(const Order& order_) : order(order_) {}

    Order order;
};

struct PriceLevel {
    uint64_t total_quantity{0};
    std::deque<std::unique_ptr<OrderNode>> orders;

    PriceLevel() = default;
    PriceLevel(PriceLevel&&) noexcept = default;
    PriceLevel& operator=(PriceLevel&&) noexcept = default;
    PriceLevel(const PriceLevel&) = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
};

struct DepthLevel {
    double price{0.0};
    uint64_t total_quantity{0};
    uint64_t order_count{0};
};

using TradeBatch = low_latency::inplace_vector<Trade, MaxTradesPerOrder>;
using DepthSnapshot = low_latency::inplace_vector<DepthLevel, MaxDepthLevels>;

template <class Compare> class FlatPriceLevels {
  public:
    using value_type = std::pair<double, PriceLevel>;
#if LFOB_HAS_STD_FLAT_MAP
    using storage_type = std::flat_map<double, PriceLevel, Compare>;
#else
    using storage_type = std::vector<value_type>;
#endif
    using iterator = storage_type::iterator;
    using const_iterator = storage_type::const_iterator;

    FlatPriceLevels() {
#if !LFOB_HAS_STD_FLAT_MAP
        levels_.reserve(MaxDepthLevels + 1);
#endif
    }

    [[nodiscard]] bool empty() const noexcept {
        return levels_.empty();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return levels_.size();
    }

    iterator begin() noexcept {
        return levels_.begin();
    }
    iterator end() noexcept {
        return levels_.end();
    }
    const_iterator begin() const noexcept {
        return levels_.begin();
    }
    const_iterator end() const noexcept {
        return levels_.end();
    }
    const_iterator cbegin() const noexcept {
        return levels_.cbegin();
    }
    const_iterator cend() const noexcept {
        return levels_.cend();
    }

    iterator erase(iterator it) {
        return levels_.erase(it);
    }

    iterator find(double price) {
#if LFOB_HAS_STD_FLAT_MAP
        return levels_.find(price);
#else
        auto it = lower_bound(price);
        if (it != levels_.end() && equivalent(it->first, price)) {
            return it;
        }
        return levels_.end();
#endif
    }

    const_iterator find(double price) const {
#if LFOB_HAS_STD_FLAT_MAP
        return levels_.find(price);
#else
        auto it = lower_bound(price);
        if (it != levels_.end() && equivalent(it->first, price)) {
            return it;
        }
        return levels_.end();
#endif
    }

    PriceLevel* upsert(double price) {
#if LFOB_HAS_STD_FLAT_MAP
        auto existing = levels_.find(price);
        if (existing != levels_.end()) {
            return &existing->second;
        }

        if (levels_.size() == MaxDepthLevels && compare_(std::prev(levels_.end())->first, price)) {
            return nullptr;
        }

        auto [it, inserted] = levels_.try_emplace(price);
        (void)inserted;
        if (levels_.size() > MaxDepthLevels) {
            levels_.erase(std::prev(levels_.end()));
        }
        return &it->second;
#else
        auto it = lower_bound(price);
        if (it != levels_.end() && equivalent(it->first, price)) {
            return &it->second;
        }

        if (levels_.size() == MaxDepthLevels && compare_(levels_.back().first, price)) {
            return nullptr;
        }

        it = levels_.insert(it, value_type{price, PriceLevel{}});
        if (levels_.size() > MaxDepthLevels) {
            levels_.pop_back();
        }
        return &it->second;
#endif
    }

  private:
#if !LFOB_HAS_STD_FLAT_MAP
    iterator lower_bound(double price) noexcept {
        return std::lower_bound(
            levels_.begin(), levels_.end(), price, [this](const value_type& level, double candidate) {
                return compare_(level.first, candidate);
            });
    }

    const_iterator lower_bound(double price) const noexcept {
        return std::lower_bound(
            levels_.begin(), levels_.end(), price, [this](const value_type& level, double candidate) {
                return compare_(level.first, candidate);
            });
    }

    bool equivalent(double lhs, double rhs) const noexcept {
        return !compare_(lhs, rhs) && !compare_(rhs, lhs);
    }
#endif

    Compare compare_{};
    storage_type levels_;
};

class OrderBook {
  public:
    OrderBook();

    low_latency::expected<TradeBatch, low_latency::EngineError> try_add_order(Order& order);
    TradeBatch add_order(Order& order);
    bool cancel_order(uint64_t order_id, Side side, double price);
    bool modify_order(uint64_t order_id, Side side, double price, uint64_t new_quantity);

    std::optional<double> get_best_bid() const;
    std::optional<double> get_best_ask() const;
    DepthSnapshot snapshot_bids() const;
    DepthSnapshot snapshot_asks() const;

  private:
    using BidLevels = FlatPriceLevels<std::greater<double>>;
    using AskLevels = FlatPriceLevels<std::less<double>>;
    using OrderSmrDomain = low_latency::HazardPointerDomain<OrderNode>;

    low_latency::expected<std::reference_wrapper<Order>, low_latency::EngineError> validate_order(Order& order);
    low_latency::expected<TradeBatch, low_latency::EngineError> add_validated_order(Order& order);
    low_latency::expected<TradeBatch, low_latency::EngineError> match_order(Order& order);

    template <class Levels> bool cancel_from(Levels& levels, uint64_t order_id, double price);

    template <class Levels> bool modify_in(Levels& levels, uint64_t order_id, double price, uint64_t new_quantity);

    template <class Levels> DepthSnapshot snapshot_side(const Levels& levels) const;

    BidLevels bids_;
    AskLevels asks_;
    OrderSmrDomain order_smr_;
};
