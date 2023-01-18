#pragma once

#include <cassert>
#include <cstddef>
#include <string_view>
#include <vector>

namespace rdss {

class Buffer {
public:
    using BufferView = std::string_view;

public:
    virtual ~Buffer() = default;

    /***
    usage | write to buffer:
    constexpr size_t BUF_SIZE = 1024;
    Buffer buf(BUF_SIZE);
    ...
    buf.EnsureCapcacity(BUF_SIZE);
    auto nread = read(buf.Data(), buf.Available(), BUF_SIZE);
    if (nread == 0) ...
    else buf.CommitWrite(nread);
    ***/
    virtual void EnsureAvailable(size_t n) = 0;
    virtual char* Data() = 0;
    virtual size_t Available() const = 0;
    virtual void CommitWrite(size_t n) = 0;

    /***
    usage | read from buffer:
    auto to_consume = buf.Stored();
    auto bytes_consumed = process(to_consume);
    buf.Consume(bytes_consumed);
    ***/
    virtual size_t NumWritten() const = 0;
    virtual BufferView Stored() const = 0;
    virtual void Consume(size_t n) = 0;

    virtual void Reset() = 0;
};

class VectorBuffer : public Buffer {
public:
    explicit VectorBuffer(size_t capacity)
      : data_(capacity)
      , read_index_(0)
      , write_index_(0) {}

    virtual ~VectorBuffer() = default;

    // TODO: Consider move forward if there is space at front. If resize, consider expand more.
    virtual void EnsureAvailable(size_t n) override {
        if (data_.size() - write_index_ >= n) {
            return;
        }
        data_.resize(n + write_index_);
    }

    virtual char* Data() override { return data_.data() + write_index_; }

    virtual size_t Available() const override { return write_index_ - read_index_; }

    virtual void CommitWrite(size_t n) override {
        assert(data_.size() >= write_index_ + n);
        write_index_ += n;
    }

    virtual size_t NumWritten() const override { return write_index_ - read_index_; }

    virtual BufferView Stored() const override {
        if (write_index_ == read_index_) {
            return {};
        }
        return BufferView(data_.data() + read_index_, write_index_ - read_index_);
    }

    virtual void Consume(size_t n) override {
        assert(read_index_ + n <= write_index_);
        read_index_ += n;
        if (read_index_ == write_index_) {
            read_index_ = 0;
            write_index_ = 0;
            // TODO: Consider shrink the buffer when capcacity is over some limit.
        }
    }

    virtual void Reset() override {
        read_index_ = 0;
        write_index_ = 0;
    }

private:
    std::vector<char> data_;
    size_t read_index_;
    size_t write_index_;
};

} // namespace rdss