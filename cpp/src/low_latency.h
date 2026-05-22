#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if __has_include(<expected>)
#include <expected>
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
#define LFOB_HAS_STD_EXPECTED 1
#else
#define LFOB_HAS_STD_EXPECTED 0
#endif

#if __has_include(<flat_map>)
#include <flat_map>
#endif

#if defined(__cpp_lib_flat_map)
#define LFOB_HAS_STD_FLAT_MAP 1
#else
#define LFOB_HAS_STD_FLAT_MAP 0
#endif

#if __has_include(<inplace_vector>)
#include <inplace_vector>
#endif

#if defined(__cpp_lib_inplace_vector)
#define LFOB_HAS_STD_INPLACE_VECTOR 1
#else
#define LFOB_HAS_STD_INPLACE_VECTOR 0
#endif

#if __has_include(<print>)
#include <print>
#endif

#if defined(__cpp_lib_print)
#define LFOB_HAS_STD_PRINT 1
#else
#define LFOB_HAS_STD_PRINT 0
#endif

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(assume) >= 202207L
#define LFOB_ASSUME(expr) [[assume(expr)]]
#elif defined(__GNUC__) || defined(__clang__)
#define LFOB_ASSUME(expr)                                                                                              \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            __builtin_unreachable();                                                                                   \
        }                                                                                                              \
    } while (false)
#else
#define LFOB_ASSUME(expr) ((void)sizeof(expr))
#endif
#else
#define LFOB_ASSUME(expr) ((void)sizeof(expr))
#endif

namespace low_latency {

#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t cache_line_size = 64;
#endif

struct EngineError {
    std::string_view code;
    std::string_view message;
};

#if LFOB_HAS_STD_EXPECTED
template <class T, class E> using expected = std::expected<T, E>;

template <class E> using unexpected = std::unexpected<E>;

template <class E> constexpr auto make_unexpected(E&& error) {
    return std::unexpected<std::decay_t<E>>(std::forward<E>(error));
}
#else
template <class E> class unexpected {
  public:
    explicit constexpr unexpected(E error) : error_(std::move(error)) {}

    constexpr const E& error() const& noexcept {
        return error_;
    }
    constexpr E& error() & noexcept {
        return error_;
    }
    constexpr E&& error() && noexcept {
        return std::move(error_);
    }

  private:
    E error_;
};

template <class E> constexpr auto make_unexpected(E&& error) {
    return unexpected<std::decay_t<E>>(std::forward<E>(error));
}

template <class T, class E> class expected {
  public:
    expected(const T& value) : storage_(value) {}
    expected(T&& value) : storage_(std::move(value)) {}
    expected(unexpected<E> error) : storage_(std::move(error).error()) {}

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(storage_);
    }
    explicit operator bool() const noexcept {
        return has_value();
    }

    T& value() & {
        return std::get<T>(storage_);
    }
    const T& value() const& {
        return std::get<T>(storage_);
    }
    T&& value() && {
        return std::move(std::get<T>(storage_));
    }

    E& error() & {
        return std::get<E>(storage_);
    }
    const E& error() const& {
        return std::get<E>(storage_);
    }

    template <class F> auto and_then(F&& fn) & {
        using result_type = std::invoke_result_t<F, T&>;
        if (has_value()) {
            return std::invoke(std::forward<F>(fn), value());
        }
        return result_type(make_unexpected(error()));
    }

    template <class F> auto transform(F&& fn) & {
        using value_type = std::invoke_result_t<F, T&>;
        if (has_value()) {
            return expected<value_type, E>(std::invoke(std::forward<F>(fn), value()));
        }
        return expected<value_type, E>(make_unexpected(error()));
    }

  private:
    std::variant<T, E> storage_;
};
#endif

#if LFOB_HAS_STD_INPLACE_VECTOR
template <class T, std::size_t Capacity> using inplace_vector = std::inplace_vector<T, Capacity>;
#else
template <class T, std::size_t Capacity> class inplace_vector {
  public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    constexpr inplace_vector() = default;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return size_;
    }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept {
        return Capacity;
    }
    [[nodiscard]] constexpr bool empty() const noexcept {
        return size_ == 0;
    }
    [[nodiscard]] constexpr bool full() const noexcept {
        return size_ == Capacity;
    }

    constexpr iterator begin() noexcept {
        return data_.data();
    }
    constexpr iterator end() noexcept {
        return data_.data() + size_;
    }
    constexpr const_iterator begin() const noexcept {
        return data_.data();
    }
    constexpr const_iterator end() const noexcept {
        return data_.data() + size_;
    }
    constexpr const_iterator cbegin() const noexcept {
        return begin();
    }
    constexpr const_iterator cend() const noexcept {
        return end();
    }

    constexpr T& operator[](std::size_t index) noexcept {
        return data_[index];
    }
    constexpr const T& operator[](std::size_t index) const noexcept {
        return data_[index];
    }

    template <class... Args> bool try_emplace_back(Args&&... args) {
        if (full()) {
            return false;
        }
        data_[size_] = T{std::forward<Args>(args)...};
        ++size_;
        return true;
    }

    void clear() noexcept {
        size_ = 0;
    }

  private:
    std::array<T, Capacity> data_{};
    std::size_t size_{0};
};
#endif

template <class T>
concept OrderLike = std::copy_constructible<T> && requires(T order, const T const_order) {
                                                      { const_order.order_id } -> std::convertible_to<std::uint64_t>;
                                                      { const_order.side };
                                                      { const_order.price } -> std::convertible_to<double>;
                                                      { const_order.quantity } -> std::convertible_to<std::uint64_t>;
                                                      { order.timestamp } -> std::convertible_to<std::uint64_t>;
                                                      { const_order <=> const_order };
                                                  };

template <class Channel, class OrderT>
concept ExecutionChannel = OrderLike<OrderT> && requires(Channel channel, std::span<const OrderT> orders) {
                                                    { channel.send_batch(orders) };
                                                    { channel.request_stop() } noexcept;
                                                };

template <class Gateway, class MessageT>
concept GatewayInterface = requires(Gateway gateway, std::span<const char> bytes) {
                               { gateway.parse(bytes) };
                               typename MessageT::value_type;
                           };

template <class T> T atomic_fetch_max(std::atomic<T>& target, T value) noexcept {
#if defined(__cpp_lib_atomic_min_max) && __cpp_lib_atomic_min_max >= 202403L
    return std::atomic_fetch_max(&target, value);
#else
    T current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
    return current;
#endif
}

template <class T> T atomic_fetch_min(std::atomic<T>& target, T value) noexcept {
#if defined(__cpp_lib_atomic_min_max) && __cpp_lib_atomic_min_max >= 202403L
    return std::atomic_fetch_min(&target, value);
#else
    T current = target.load(std::memory_order_relaxed);
    while (value < current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
    return current;
#endif
}

struct LatencyWatermarks {
    std::atomic<std::uint64_t> min_ns{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> max_ns{0};

    void record(std::uint64_t latency_ns) noexcept {
        atomic_fetch_min(min_ns, latency_ns);
        atomic_fetch_max(max_ns, latency_ns);
    }

    [[nodiscard]] std::uint64_t min() const noexcept {
        const auto value = min_ns.load(std::memory_order_relaxed);
        return value == std::numeric_limits<std::uint64_t>::max() ? 0 : value;
    }

    [[nodiscard]] std::uint64_t max() const noexcept {
        return max_ns.load(std::memory_order_relaxed);
    }
};

inline void emit_line(std::string_view line) {
#if LFOB_HAS_STD_PRINT
    std::println("{}", line);
#else
    std::fwrite(line.data(), sizeof(char), line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
#endif
}

template <class T, std::size_t Slots = 64, std::size_t RetireThreshold = 64> class HazardPointerDomain {
  public:
    ~HazardPointerDomain() {
        auto* node = retired_head_.exchange(nullptr, std::memory_order_acq_rel);
        while (node != nullptr) {
            auto* next = node->next;
            delete node->ptr;
            delete node;
            node = next;
        }
    }

    class Guard {
      public:
        explicit Guard(HazardPointerDomain& domain) noexcept : domain_(&domain), slot_(domain.acquire_slot()) {}
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&& other) noexcept : domain_(other.domain_), slot_(other.slot_) {
            other.domain_ = nullptr;
            other.slot_ = npos;
        }

        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) {
                release();
                domain_ = other.domain_;
                slot_ = other.slot_;
                other.domain_ = nullptr;
                other.slot_ = npos;
            }
            return *this;
        }

        ~Guard() {
            release();
        }

        T* protect(T* pointer) noexcept {
            if (domain_ != nullptr && slot_ != npos) {
                domain_->hazards_[slot_].store(pointer, std::memory_order_release);
            }
            return pointer;
        }

        void clear() noexcept {
            if (domain_ != nullptr && slot_ != npos) {
                domain_->hazards_[slot_].store(nullptr, std::memory_order_release);
            }
        }

      private:
        void release() noexcept {
            if (domain_ != nullptr && slot_ != npos) {
                clear();
                domain_->active_[slot_].store(false, std::memory_order_release);
            }
            domain_ = nullptr;
            slot_ = npos;
        }

        static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();
        HazardPointerDomain* domain_{nullptr};
        std::size_t slot_{npos};
    };

    [[nodiscard]] Guard make_guard() noexcept {
        return Guard(*this);
    }

    void retire(std::unique_ptr<T> node) {
        if (!node) {
            return;
        }

        auto* retired = new RetiredNode{node.release(), nullptr};
        push_retired(retired);

        if (retired_count_.fetch_add(1, std::memory_order_acq_rel) + 1 >= RetireThreshold) {
            scan();
        }
    }

    void scan() {
        auto* list = retired_head_.exchange(nullptr, std::memory_order_acq_rel);
        std::size_t scanned = 0;
        std::size_t still_retired = 0;

        while (list != nullptr) {
            ++scanned;
            auto* next = list->next;
            if (contains_hazard(list->ptr)) {
                list->next = nullptr;
                push_retired(list);
                ++still_retired;
            } else {
                delete list->ptr;
                delete list;
            }
            list = next;
        }

        if (scanned > still_retired) {
            retired_count_.fetch_sub(scanned - still_retired, std::memory_order_acq_rel);
        }
    }

  private:
    struct RetiredNode {
        T* ptr{nullptr};
        RetiredNode* next{nullptr};
    };

    void push_retired(RetiredNode* node) noexcept {
        auto* head = retired_head_.load(std::memory_order_acquire);
        do {
            node->next = head;
        } while (
            !retired_head_.compare_exchange_weak(head, node, std::memory_order_release, std::memory_order_acquire));
    }

    std::size_t acquire_slot() noexcept {
        for (std::size_t i = 0; i < Slots; ++i) {
            bool expected = false;
            if (active_[i].compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                return i;
            }
        }
        std::terminate();
    }

    bool contains_hazard(const T* pointer) const noexcept {
        for (const auto& hazard : hazards_) {
            if (hazard.load(std::memory_order_acquire) == pointer) {
                return true;
            }
        }
        return false;
    }

    std::array<std::atomic<T*>, Slots> hazards_{};
    std::array<std::atomic_bool, Slots> active_{};
    std::atomic<RetiredNode*> retired_head_{nullptr};
    std::atomic<std::size_t> retired_count_{0};
};

} // namespace low_latency
