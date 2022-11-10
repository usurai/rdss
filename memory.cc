#include "memory.h"

namespace rdss {

MemoryTracker* MemoryTracker::instance_ = nullptr;

MemoryTracker& MemoryTracker::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new MemoryTracker();
    }
    return *instance_;
}

} // namespace rdss
