#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <vector>

class MemoryTracker {
public:
    MemoryTracker(MemoryTracker&) = delete;
    void operator=(const MemoryTracker&) = delete;
    static MemoryTracker& GetInstance();
    void Allocate(size_t n) { counter_ += n; }
    void Deallocate(size_t n) { counter_ -= n; }
    size_t GetAllocated() const { return counter_; }
protected:
    MemoryTracker() = default;
    static MemoryTracker* instance_;
private:
    size_t counter_ = 0;
};

MemoryTracker* MemoryTracker::instance_ = nullptr;

MemoryTracker& MemoryTracker::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new MemoryTracker();
    }
    return *instance_;
}

template<class T>
struct Mallocator {
    using value_type = T;

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
    void report(T* p, std::size_t n, bool alloc = true) const {
        const auto bytes = sizeof(T) * n;
        std::cout << (alloc ? "Alloc: " : "Dealloc: ") << bytes << " bytes at " << std::hex
                  << std::showbase << reinterpret_cast<void*>(p) << std::dec << '\n';
        if (alloc) {
            MemoryTracker::GetInstance().Allocate(bytes);
        } else {
            MemoryTracker::GetInstance().Deallocate(bytes);
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

int main() {
    using TrackingString = std::basic_string<char, std::char_traits<char>, Mallocator<char>>;
    using ValueType = std::map<TrackingString, TrackingString>;

    std::map<TrackingString, TrackingString, std::less<>, Mallocator<ValueType>> m;
    std::cout << MemoryTracker::GetInstance().GetAllocated() << '\n';

    m["abc"] = "abcdasasjdkf;adff";
    std::cout << MemoryTracker::GetInstance().GetAllocated() << '\n';

    m["efg"] = "lkasdjf;laskdfjasdklfsdajsdklfjsdafklasdufaflkanfiwjaiefbnaokdvadsnfsdfsdfsdfasd;"
               "lvnaeijfnasjdkfl;asdkjfasdkfla;sdlkfjasskdl;faskdjfasl;dfasd;lkfasdjfl;aksdfjsksd";
    std::cout << MemoryTracker::GetInstance().GetAllocated() << '\n';
}
