#pragma once

#include <cassert>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace rdss {

constexpr size_t kMaxInlineBufferSize = 1024 * 64;

// class Buffer;
using Buffer = std::string;
// class String;
using String = std::string;

using Strings = std::vector<String>;

class RedisParser {
public:
    enum class State : uint8_t { kInit, kError, kParsing, kDone };

    struct ParsingResult {
        State state;
        size_t cursor;
        Strings bulk_strings;
    };

    explicit RedisParser(const Buffer& buffer)
      : state_(State::kInit)
      , buffer_(buffer)
      , cursor_(0)
      , bytes_to_parse_(0) {}

    virtual ParsingResult Parse(size_t bytes_read) = 0;

    // UpdateBuffer();

protected:
    State state_;
    const Buffer& buffer_;
    size_t cursor_;
    size_t bytes_to_parse_;
    Strings parsed_;
};

class InlineParser : public RedisParser {
public:
    explicit InlineParser(const Buffer& buffer)
      : RedisParser(buffer) {}

    virtual ParsingResult Parse(size_t bytes_read) override {
        assert(bytes_read > 0);
        assert(state_ == State::kInit || state_ == State::kParsing);
        assert(!buffer_.empty());
        assert(cursor_ + bytes_read <= buffer_.size());

        bytes_to_parse_ += bytes_read;

        const auto crlf = buffer_.find("\r\n", cursor_);
        if (crlf == Buffer::npos || crlf >= bytes_to_parse_ - 1) {
            if (bytes_to_parse_ > kMaxInlineBufferSize) {
                state_ = State::kError;
            } else {
                state_ = State::kParsing;
            }
            return {state_, 0, {}};
        }

        const auto old_cursor{cursor_};
        // TODO: support quote and single quote.
        while (cursor_ < crlf) {
            if (std::isspace(buffer_[cursor_])) {
                ++cursor_;
                continue;
            }
            auto next_space = cursor_ + 1;
            while (next_space < crlf && !std::isspace(buffer_[next_space])) {
                ++next_space;
            }
            parsed_.emplace_back(buffer_.data() + cursor_, next_space - cursor_);
            cursor_ = next_space;
        }
        state_ = State::kDone;
        cursor_ = crlf + 2;
        bytes_to_parse_ -= cursor_ - old_cursor;
        return {state_, cursor_, std::move(parsed_)};
    }
};

} // namespace rdss