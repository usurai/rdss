#pragma once

#include <glog/logging.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <vector>

namespace rdss {

enum class MemTrackingCategory : uint8_t { kMallocator = 0, kQueryBuffer, kAll };
std::ostream& operator<<(std::ostream& os, MemTrackingCategory c);

class MemoryTracker {
public:
    using Category = MemTrackingCategory;

public:
    MemoryTracker(MemoryTracker&) = delete;

    void operator=(const MemoryTracker&) = delete;

    // TODO: This is not thread-safe.
    static MemoryTracker& GetInstance();

    template<Category C>
    void Allocate(size_t n) {
        static_assert(C != Category::kAll);
        counter_[static_cast<size_t>(C)].fetch_add(n, std::memory_order_relaxed);
        VLOG(1) << '[' << C << "] Allocate [" << n << " | " << counter_[static_cast<size_t>(C)]
                << "].";
    }

    template<Category C>
    void Deallocate(size_t n) {
        static_assert(C != Category::kAll);
        counter_[static_cast<size_t>(C)].fetch_sub(n, std::memory_order_relaxed);
        VLOG(1) << '[' << C << "] Deallocate [" << n << " | " << counter_[static_cast<size_t>(C)]
                << "].";
    }

    template<Category C>
    size_t GetAllocated() const {
        if constexpr (C == Category::kAll) {
            return counter_[0] + counter_[1];
        }
        return counter_[static_cast<size_t>(C)];
    }

protected:
    MemoryTracker() = default;
    static MemoryTracker* instance_;

private:
    std::atomic<size_t> counter_[2] = {};
};

template<class T>
struct Mallocator {
    using value_type = T;
    static constexpr auto MemCategory = MemoryTracker::Category::kMallocator;

    Mallocator() = default;

    template<class U>
    constexpr Mallocator(const Mallocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();

        if (auto p = static_cast<T*>(std::malloc(n * sizeof(T)))) {
            report(p, n);
            return p;
        }

        throw std::bad_alloc();
    }

    void deallocate(T* p, std::size_t n) noexcept {
        report(p, n, 0);
        std::free(p);
    }

private:
    void report([[maybe_unused]] T* p, std::size_t n, bool alloc = true) const {
        const auto bytes = sizeof(T) * n;
        if (alloc) {
            MemoryTracker::GetInstance().Allocate<MemCategory>(bytes);
        } else {
            MemoryTracker::GetInstance().Deallocate<MemCategory>(bytes);
        }
    }
};

template<class T, class U>
bool operator==(const Mallocator<T>&, const Mallocator<U>&) {
    return true;
}

template<class T, class U>
bool operator!=(const Mallocator<T>&, const Mallocator<U>&) {
    return false;
}

} // namespace rdss
