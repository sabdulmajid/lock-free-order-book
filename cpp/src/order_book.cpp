#include "order_book.h"
#include <chrono>
#include <algorithm>

OrderBook::OrderBook() {}

std::vector<Trade> OrderBook::match_order(Order& order) {
    std::vector<Trade> trades;
    if (order.side == Side::Buy) {
        for (auto it = asks.begin(); it != asks.end(); ) {
            auto& price = it->first;
            auto& price_level = it->second;
            if (order.price < price || order.quantity == 0) {
                break;
            }
            for (auto& maker_order : price_level.orders) {
                if (order.quantity == 0) {
                    break;
                }
                uint64_t trade_quantity = std::min(order.quantity, maker_order.quantity);
                trades.emplace_back(order.order_id, maker_order.order_id, trade_quantity, price);
                order.quantity -= trade_quantity;
                maker_order.quantity -= trade_quantity;
                price_level.total_quantity -= trade_quantity;
            }
            std::erase_if(price_level.orders, [](const Order& o) { return o.quantity == 0; });
            
            if (price_level.orders.empty()) {
                it = asks.erase(it);
            } else {
                ++it;
            }
        }
    } else { // Side::Sell
        for (auto it = bids.begin(); it != bids.end(); ) {
            auto& price = it->first;
            auto& price_level = it->second;
            if (order.price > price || order.quantity == 0) {
                break;
            }
            for (auto& maker_order : price_level.orders) {
                if (order.quantity == 0) {
                    break;
                }
                uint64_t trade_quantity = std::min(order.quantity, maker_order.quantity);
                trades.emplace_back(order.order_id, maker_order.order_id, trade_quantity, price);
                order.quantity -= trade_quantity;
                maker_order.quantity -= trade_quantity;
                price_level.total_quantity -= trade_quantity;
            }
            std::erase_if(price_level.orders, [](const Order& o) { return o.quantity == 0; });

            if (price_level.orders.empty()) {
                it = bids.erase(it);
            } else {
                ++it;
            }
        }
    }
    return trades;
}

std::vector<Trade> OrderBook::add_order(Order& order) {
    order.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    auto trades = match_order(order);

    if (order.quantity > 0) {
        if (order.side == Side::Buy) {
            auto& price_level = bids[order.price];
            price_level.total_quantity += order.quantity;
            price_level.orders.push_back(order);
        } else {
            auto& price_level = asks[order.price];
            price_level.total_quantity += order.quantity;
            price_level.orders.push_back(order);
        }
    }

    return trades;
}

bool OrderBook::cancel_order(uint64_t order_id, Side side, double price) {
    auto* book_side = (side == Side::Buy) ? &bids : (std::map<double, PriceLevel, std::greater<double>>*) &asks;

    if (side == Side::Buy) {
        auto it = bids.find(price);
        if (it == bids.end()) {
            return false;
        }
        auto& price_level = it->second;
        auto& orders = price_level.orders;
        auto order_it = std::ranges::find_if(orders, 
                                             [order_id](const Order& o) { return o.order_id == order_id; });

        if (order_it != orders.end()) {
            price_level.total_quantity -= order_it->quantity;
            orders.erase(order_it);
            if (orders.empty()) {
                bids.erase(it);
            }
            return true;
        }
    } else {
        auto it = asks.find(price);
        if (it == asks.end()) {
            return false;
        }
        auto& price_level = it->second;
        auto& orders = price_level.orders;
        auto order_it = std::ranges::find_if(orders, 
                                             [order_id](const Order& o) { return o.order_id == order_id; });

        if (order_it != orders.end()) {
            price_level.total_quantity -= order_it->quantity;
            orders.erase(order_it);
            if (orders.empty()) {
                asks.erase(it);
            }
            return true;
        }
    }

    return false;
}

bool OrderBook::modify_order(uint64_t order_id, Side side, double price, uint64_t new_quantity) {
    auto* book_side = (side == Side::Buy) ? &bids : (std::map<double, PriceLevel, std::greater<double>>*) &asks;

    if (side == Side::Buy) {
        auto it = bids.find(price);
        if (it != bids.end()) {
            auto& price_level = it->second;
            auto order_it = std::ranges::find_if(price_level.orders, [order_id](const Order& o) { return o.order_id == order_id; });
            if (order_it != price_level.orders.end()) {
                price_level.total_quantity -= order_it->quantity;
                order_it->quantity = new_quantity;
                price_level.total_quantity += new_quantity;
                return true;
            }
        }
    } else {
        auto it = asks.find(price);
        if (it != asks.end()) {
            auto& price_level = it->second;
            auto order_it = std::ranges::find_if(price_level.orders, [order_id](const Order& o) { return o.order_id == order_id; });
            if (order_it != price_level.orders.end()) {
                price_level.total_quantity -= order_it->quantity;
                order_it->quantity = new_quantity;
                price_level.total_quantity += new_quantity;
                return true;
            }
        }
    }
    return false;
}

std::optional<double> OrderBook::get_best_bid() const {
    if (bids.empty()) {
        return std::nullopt;
    }
    return bids.begin()->first;
}

std::optional<double> OrderBook::get_best_ask() const {
    if (asks.empty()) {
        return std::nullopt;
    }
    return asks.begin()->first;
}
