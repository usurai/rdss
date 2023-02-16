#pragma once

#include <cassert>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace rdss {

/***
Usage:
1. Write to the buffer:
    constexpr size_t BUF_SIZE = 1024;
    Buffer buf(BUF_SIZE);
    ...
    buf.EnsureAvailable(BUF_SIZE);
    auto nread = read(buf.Data(), buf.Available(), BUF_SIZE);
    or
    auto sink = buf.Sink();
    auto nread = read(sink.data(), sink.size(), BUF_SIZE);
    if (nread == 0) ...
    else buf.Proceduce(nread);
2. Read from the buffer:
    auto source = buf.Source();
    auto bytes_consumed = process(source);
    buf.Consume(bytes_consumed);
***/
class Buffer {
public:
    using View = std::string_view;
    using SinkType = std::span<char>;

public:
    Buffer() = default;

    explicit Buffer(size_t capacity)
      : data_(capacity) {}

    void EnsureAvailable(size_t n);

    char* Data() { return data_.data() + write_index_; }

    size_t Available() const { return data_.size() - write_index_; }

    std::span<char> Sink() { return std::span(data_.data() + write_index_, Available()); }

    void Produce(size_t n) {
        assert(data_.size() >= write_index_ + n);
        write_index_ += n;
    }

    size_t NumWritten() const { return write_index_ - read_index_; }

    View Source() const;

    void Consume(size_t n);

    void Reset() {
        read_index_ = 0;
        write_index_ = 0;
    }

private:
    std::vector<char> data_;
    size_t read_index_{0};
    size_t write_index_{0};
};

} // namespace rdss