#pragma once

#include "low_latency.h"
#include "order.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>

namespace stdin_parser {

using ParseResult = low_latency::expected<std::string_view, low_latency::EngineError>;
using OrderResult = low_latency::expected<Order, low_latency::EngineError>;
using CountResult = low_latency::expected<std::size_t, low_latency::EngineError>;

inline constexpr low_latency::EngineError MissingAction{"missing_action", "message does not contain an action"};
inline constexpr low_latency::EngineError MissingOrders{"missing_orders",
                                                        "batch message does not contain an orders array"};
inline constexpr low_latency::EngineError MalformedJson{"malformed_json", "message does not match the engine protocol"};
inline constexpr low_latency::EngineError BadNumber{"bad_number", "numeric field could not be parsed"};
inline constexpr low_latency::EngineError BadSide{"bad_side", "order side must be buy or sell"};

inline void skip_ws(std::string_view text, std::size_t& pos) {
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
            break;
        }
        ++pos;
    }
}

inline low_latency::expected<std::size_t, low_latency::EngineError> find_value_start(std::string_view object,
                                                                                     std::string_view quoted_key) {
    const auto key_pos = object.find(quoted_key);
    if (key_pos == std::string_view::npos) {
        return low_latency::make_unexpected(MalformedJson);
    }

    const auto colon_pos = object.find(':', key_pos + quoted_key.size());
    if (colon_pos == std::string_view::npos) {
        return low_latency::make_unexpected(MalformedJson);
    }

    std::size_t value_pos = colon_pos + 1;
    skip_ws(object, value_pos);
    if (value_pos >= object.size()) {
        return low_latency::make_unexpected(MalformedJson);
    }

    return value_pos;
}

inline ParseResult parse_string_field(std::string_view object,
                                      std::string_view quoted_key,
                                      low_latency::EngineError missing_error = MalformedJson) {
    auto value_pos = find_value_start(object, quoted_key);
    if (!value_pos) {
        return low_latency::make_unexpected(missing_error);
    }

    std::size_t pos = value_pos.value();
    if (object[pos] != '"') {
        return low_latency::make_unexpected(MalformedJson);
    }

    const auto end = object.find('"', pos + 1);
    if (end == std::string_view::npos) {
        return low_latency::make_unexpected(MalformedJson);
    }

    return object.substr(pos + 1, end - pos - 1);
}

template <class UInt>
low_latency::expected<UInt, low_latency::EngineError> parse_uint_field(std::string_view object,
                                                                       std::string_view quoted_key) {
    auto value_pos = find_value_start(object, quoted_key);
    if (!value_pos) {
        return low_latency::make_unexpected(value_pos.error());
    }

    std::size_t end = value_pos.value();
    while (end < object.size() && object[end] != ',' && object[end] != '}' && object[end] != ' ' &&
           object[end] != '\t' && object[end] != '\n' && object[end] != '\r') {
        ++end;
    }

    UInt value{};
    const char* first = object.data() + value_pos.value();
    const char* last = object.data() + end;
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        return low_latency::make_unexpected(BadNumber);
    }

    return value;
}

inline low_latency::expected<double, low_latency::EngineError> parse_double_field(std::string_view object,
                                                                                  std::string_view quoted_key) {
    auto value_pos = find_value_start(object, quoted_key);
    if (!value_pos) {
        return low_latency::make_unexpected(value_pos.error());
    }

    std::size_t end = value_pos.value();
    while (end < object.size() && object[end] != ',' && object[end] != '}' && object[end] != ' ' &&
           object[end] != '\t' && object[end] != '\n' && object[end] != '\r') {
        ++end;
    }

    double value{};
    const char* first = object.data() + value_pos.value();
    const char* last = object.data() + end;
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        return low_latency::make_unexpected(BadNumber);
    }

    return value;
}

inline ParseResult parse_action(std::span<const char> bytes) {
    const std::string_view message(bytes.data(), bytes.size());
    return parse_string_field(message, "\"action\"", MissingAction);
}

inline OrderResult parse_order(std::string_view object) {
    auto id = parse_uint_field<uint64_t>(object, "\"id\"");
    if (!id) {
        return low_latency::make_unexpected(id.error());
    }

    auto side_text = parse_string_field(object, "\"side\"");
    if (!side_text) {
        return low_latency::make_unexpected(side_text.error());
    }

    Side side{};
    if (side_text.value() == "buy") {
        side = Side::Buy;
    } else if (side_text.value() == "sell") {
        side = Side::Sell;
    } else {
        return low_latency::make_unexpected(BadSide);
    }

    auto price = parse_double_field(object, "\"price\"");
    if (!price) {
        return low_latency::make_unexpected(price.error());
    }

    auto quantity = parse_uint_field<uint64_t>(object, "\"quantity\"");
    if (!quantity) {
        return low_latency::make_unexpected(quantity.error());
    }

    return Order(id.value(), side, price.value(), quantity.value());
}

template <class SubmitOrder> CountResult parse_batch_orders(std::span<const char> bytes, SubmitOrder&& submit_order) {
    const std::string_view message(bytes.data(), bytes.size());
    const auto orders_key = message.find("\"orders\"");
    if (orders_key == std::string_view::npos) {
        return low_latency::make_unexpected(MissingOrders);
    }

    const auto array_start = message.find('[', orders_key);
    if (array_start == std::string_view::npos) {
        return low_latency::make_unexpected(MissingOrders);
    }

    std::size_t pos = array_start + 1;
    std::size_t processed = 0;
    while (pos < message.size()) {
        skip_ws(message, pos);
        if (pos >= message.size()) {
            break;
        }

        if (message[pos] == ']') {
            return processed;
        }
        if (message[pos] == ',') {
            ++pos;
            continue;
        }
        if (message[pos] != '{') {
            return low_latency::make_unexpected(MalformedJson);
        }

        const auto object_end = message.find('}', pos + 1);
        if (object_end == std::string_view::npos) {
            return low_latency::make_unexpected(MalformedJson);
        }

        auto order = parse_order(message.substr(pos + 1, object_end - pos - 1));
        if (!order) {
            return low_latency::make_unexpected(order.error());
        }

        auto accepted = submit_order(order.value());
        if (!accepted) {
            return low_latency::make_unexpected(accepted.error());
        }

        ++processed;
        pos = object_end + 1;
    }

    return low_latency::make_unexpected(MalformedJson);
}

} // namespace stdin_parser
