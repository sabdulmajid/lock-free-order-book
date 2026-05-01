#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include "../third_party/json.hpp"
#include "order_book.h"

using json = nlohmann::json;

int main() {
    // Optimization for fast I/O
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    OrderBook book;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        try {
            json req = json::parse(line);
            std::string action = req.value("action", "");

            if (action == "batch") {
                auto start = std::chrono::high_resolution_clock::now();
                int processed = 0;
                
                auto orders = req["orders"];
                for (const auto& o : orders) {
                    uint64_t id = o["id"];
                    Side side = o["side"] == "buy" ? Side::Buy : Side::Sell;
                    double price = o["price"];
                    uint64_t quantity = o["quantity"];

                    Order order(id, side, price, quantity);
                    book.add_order(order);
                    processed++;
                }
                
                auto end = std::chrono::high_resolution_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

                json resp;
                resp["type"] = "batch_result";
                resp["latency_ns"] = latency;
                resp["processed"] = processed;
                std::cout << resp.dump() << "\n";
                std::cout.flush();

            } else if (action == "snapshot") {
                json resp;
                resp["type"] = "snapshot";
                resp["bids"] = json::array();
                resp["asks"] = json::array();

                int i = 0;
                for (auto it = book.bids.rbegin(); it != book.bids.rend() && i < 20; ++it, ++i) {
                    json level;
                    level["price"] = it->first;
                    level["quantity"] = it->second.total_quantity;
                    level["order_count"] = it->second.orders.size();
                    resp["bids"].push_back(level);
                }

                i = 0;
                for (auto it = book.asks.begin(); it != book.asks.end() && i < 20; ++it, ++i) {
                    json level;
                    level["price"] = it->first;
                    level["quantity"] = it->second.total_quantity;
                    level["order_count"] = it->second.orders.size();
                    resp["asks"].push_back(level);
                }

                std::cout << resp.dump() << "\n";
                std::cout.flush();
            }
        } catch (const std::exception& e) {
            json err;
            err["type"] = "error";
            err["message"] = e.what();
            std::cout << err.dump() << "\n";
            std::cout.flush();
        }
    }

    return 0;
}
