#include "base/buffer.h"

#include <glog/logging.h>

namespace detail {

constexpr size_t kResizeThreshold = 1024 * 1024;

size_t MakeRoomFor(size_t expected_size, bool greedy) {
    if (!greedy) {
        return expected_size;
    }

    if (expected_size < kResizeThreshold) {
        return expected_size * 2;
    }
    return expected_size + kResizeThreshold;
}

} // namespace detail

namespace rdss {

// TODO: Consider move forward if there is space at front. If resize, consider expand more.
// When resizing, returns pointer original start of data_ to enable the parser to update accumulated
// result, othwise, returns nullptr.
char* Buffer::EnsureAvailable(size_t n, bool greedy) {
    const size_t needed_size = n + write_index_;
    if (data_.size() >= needed_size) {
        return nullptr;
    }

    char* original_start = data_.data();
    const auto new_size = detail::MakeRoomFor(needed_size, greedy);
    assert(new_size >= needed_size);
    VLOG(1) << "Resize buffer from " << data_.size() << " to " << new_size;
    MemoryTracker::GetInstance().Allocate<MemCategory>(new_size - data_.size());
    data_.resize(new_size);
    return original_start;
}

Buffer::View Buffer::Source() const {
    if (write_index_ == read_index_) {
        return {};
    }
    if (!virtual_view_) {
        return View(data_.data() + read_index_, write_index_ - read_index_);
    }
    return std::string_view{view_.data() + read_index_, view_.size() - read_index_};
}

void Buffer::Consume(size_t n) {
    assert(read_index_ + n <= write_index_);
    read_index_ += n;
}

} // namespace rdss
