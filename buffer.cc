#include "buffer.h"

namespace rdss {

// TODO: Consider move forward if there is space at front. If resize, consider expand more.
void Buffer::EnsureAvailable(size_t n) {
    if (data_.size() - write_index_ >= n) {
        return;
    }
    data_.resize(n + write_index_);
}

Buffer::View Buffer::Source() const {
    if (write_index_ == read_index_) {
        return {};
    }
    return View(data_.data() + read_index_, write_index_ - read_index_);
}

void Buffer::Consume(size_t n) {
    assert(read_index_ + n <= write_index_);
    read_index_ += n;
    if (read_index_ == write_index_) {
        read_index_ = 0;
        write_index_ = 0;
        // TODO: Consider shrink the buffer when capcacity is over some limit.
    }
}

} // namespace rdss
