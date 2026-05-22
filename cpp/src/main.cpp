#include "order_book.h"
#include "stdin_parser.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {
constexpr low_latency::EngineError UnknownAction{"unknown_action", "unsupported engine action"};

void append_u64(std::string& out, uint64_t value) {
    char buffer[32]{};
    const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec == std::errc{}) {
        out.append(buffer, ptr);
    }
}

void append_double(std::string& out, double value) {
    char buffer[64]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    if (written > 0) {
        out.append(buffer, static_cast<std::size_t>(written));
    }
}

void write_error(low_latency::EngineError error) {
    std::string out;
    out.reserve(128);
    out += "{\"type\":\"error\",\"code\":\"";
    out += error.code;
    out += "\",\"message\":\"";
    out += error.message;
    out += "\"}";
    low_latency::emit_line(out);
}

void write_batch_result(uint64_t latency_ns, std::size_t processed, const low_latency::LatencyWatermarks& watermarks) {
    std::string out;
    out.reserve(160);
    out += "{\"type\":\"batch_result\",\"latency_ns\":";
    append_u64(out, latency_ns);
    out += ",\"processed\":";
    append_u64(out, processed);
    out += ",\"latency_min_ns\":";
    append_u64(out, watermarks.min());
    out += ",\"latency_max_ns\":";
    append_u64(out, watermarks.max());
    out += "}";
    low_latency::emit_line(out);
}

void append_depth_levels(std::string& out, const DepthSnapshot& levels) {
    bool first = true;
    for (const auto& level : levels) {
        if (!first) {
            out += ',';
        }
        first = false;

        out += "{\"price\":";
        append_double(out, level.price);
        out += ",\"quantity\":";
        append_u64(out, level.total_quantity);
        out += ",\"order_count\":";
        append_u64(out, level.order_count);
        out += "}";
    }
}

void write_snapshot(const OrderBook& book) {
    std::string out;
    out.reserve(4096);
    out += "{\"type\":\"snapshot\",\"bids\":[";
    append_depth_levels(out, book.snapshot_bids());
    out += "],\"asks\":[";
    append_depth_levels(out, book.snapshot_asks());
    out += "]}";
    low_latency::emit_line(out);
}

class BookExecutionChannel {
  public:
    using value_type = Order;

    explicit BookExecutionChannel(OrderBook& book) : book_(book) {}

    low_latency::expected<std::size_t, low_latency::EngineError> send_batch(std::span<const Order> orders) {
        std::size_t processed = 0;
        for (const auto& order_template : orders) {
            auto order = order_template;
            auto result = book_.try_add_order(order);
            if (!result) {
                return low_latency::make_unexpected(result.error());
            }
            ++processed;
        }
        return processed;
    }

    low_latency::expected<bool, low_latency::EngineError> submit(Order& order) {
        if (stop_requested_) {
            return low_latency::make_unexpected(UnknownAction);
        }

        auto result = book_.try_add_order(order);
        if (!result) {
            return low_latency::make_unexpected(result.error());
        }
        return true;
    }

    void request_stop() noexcept {
        stop_requested_ = true;
    }

  private:
    OrderBook& book_;
    bool stop_requested_{false};
};

struct StdinGatewayMessage {
    using value_type = Order;
};

class StdinGateway {
  public:
    auto parse(std::span<const char> bytes) const {
        return stdin_parser::parse_action(bytes);
    }
};

static_assert(low_latency::ExecutionChannel<BookExecutionChannel, Order>);
static_assert(low_latency::GatewayInterface<StdinGateway, StdinGatewayMessage>);
} // namespace

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    OrderBook book;
    BookExecutionChannel channel(book);
    low_latency::LatencyWatermarks watermarks;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        const std::span<const char> bytes(line.data(), line.size());
        auto action = stdin_parser::parse_action(bytes);
        if (!action) {
            write_error(action.error());
            continue;
        }

        if (action.value() == "batch") {
            const auto start = std::chrono::steady_clock::now();
            auto processed =
                stdin_parser::parse_batch_orders(bytes, [&](Order& order) { return channel.submit(order); });
            const auto end = std::chrono::steady_clock::now();
            const auto latency_ns =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

            if (!processed) {
                write_error(processed.error());
                continue;
            }

            watermarks.record(latency_ns);
            write_batch_result(latency_ns, processed.value(), watermarks);
        } else if (action.value() == "snapshot") {
            write_snapshot(book);
        } else {
            write_error(UnknownAction);
        }
    }

    channel.request_stop();
    return 0;
}
