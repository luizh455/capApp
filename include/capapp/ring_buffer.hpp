#pragma once

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace capapp {

// Cache line size for modern x86_64 processors to avoid false sharing
inline constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief Lock-free, wait-free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 *
 * Designed for ultra-low latency audio/video frame transmission.
 * Uses atomic head and tail pointers separated by cache-line padding to eliminate
 * false sharing. Requires capacity to be a power of two for fast bitwise masking.
 */
template <typename T>
class RingBuffer {
public:
    static_assert(std::is_nothrow_destructible_v<T>, "T must have a noexcept destructor");

    explicit RingBuffer(size_t capacity)
        : capacity_(round_up_to_power_of_two(capacity < 2 ? 2 : capacity)),
          mask_(capacity_ - 1),
          storage_(static_cast<StorageType*>(::operator new[](sizeof(StorageType) * capacity_))) {
    }

    ~RingBuffer() noexcept {
        clear();
        ::operator delete[](storage_);
    }

    // Non-copyable, non-movable to guarantee address stability for atomic pointers
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * @brief Pushes an item by copy into the buffer. (Producer thread only)
     */
    bool push(const T& item) {
        return emplace(item);
    }

    /**
     * @brief Pushes an item by move into the buffer. (Producer thread only)
     */
    bool push(T&& item) {
        return emplace(std::move(item));
    }

    /**
     * @brief Emplaces a new item in-place in the buffer. (Producer thread only)
     */
    template <typename... Args>
    bool emplace(Args&&... args) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if (head - tail >= capacity_) {
            // Buffer is full
            return false;
        }

        const size_t index = head & mask_;
        new (slot_address(index)) T(std::forward<Args>(args)...);

        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops an item from the buffer into outValue. (Consumer thread only)
     */
    bool pop(T& outValue) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            // Buffer is empty
            return false;
        }

        const size_t index = tail & mask_;
        T* item = slot_address(index);
        outValue = std::move(*item);
        item->~T();

        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops an item and returns std::optional<T>. (Consumer thread only)
     */
    [[nodiscard]] std::optional<T> try_pop() {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return std::nullopt;
        }

        const size_t index = tail & mask_;
        T* item = slot_address(index);
        std::optional<T> result(std::move(*item));
        item->~T();

        tail_.store(tail + 1, std::memory_order_release);
        return result;
    }

    /**
     * @brief Returns current approximate size. Safe to call from any thread.
     */
    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool full() const noexcept {
        return size() >= capacity_;
    }

    [[nodiscard]] size_t capacity() const noexcept {
        return capacity_;
    }

    void clear() noexcept {
        T temp;
        while (pop(temp)) {}
    }

private:
    using StorageType = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

    [[nodiscard]] T* slot_address(size_t index) noexcept {
        return reinterpret_cast<T*>(&storage_[index]);
    }

    [[nodiscard]] static size_t round_up_to_power_of_two(size_t value) noexcept {
        if (value == 0) return 1;
        --value;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
        value |= value >> 32;
#endif
        return value + 1;
    }

    const size_t capacity_;
    const size_t mask_;
    StorageType* const storage_;

    // Padded to distinct cache lines to avoid false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
};

} // namespace capapp

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
