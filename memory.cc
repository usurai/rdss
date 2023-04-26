#include "memory.h"

namespace rdss {

MemoryTracker* MemoryTracker::instance_ = nullptr;

MemoryTracker& MemoryTracker::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new MemoryTracker();
    }
    return *instance_;
}

std::ostream& operator<<(std::ostream& os, MemTrackingCategory c) {
    switch (c) {
    case MemTrackingCategory::kMallocator:
        return os << "Mallocator";
    case MemTrackingCategory::kQueryBuffer:
        return os << "QueryBuffer";
    case MemTrackingCategory::kAll:
        return os << "All";
    }
}

} // namespace rdss
