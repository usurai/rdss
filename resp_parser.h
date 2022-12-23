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
    enum class State { kInit, kError, kParsing, kDone };

    struct ParsingResult {
        State state;
        size_t cursor;
        Strings bulk_strings;
    };

    explicit RedisParser(const Buffer& buffer)
      : state_(State::kInit)
      , buffer_(buffer)
      , cursor_(0) {}

    virtual ParsingResult Parse(size_t bytes_read) = 0;

    // UpdateBuffer();

protected:
    State state_;
    const Buffer& buffer_;
    size_t cursor_;
    Strings parsed_;
};

class InlineParser : public RedisParser {
public:
    explicit InlineParser(const Buffer& buffer)
      : RedisParser(buffer) {}

    virtual ParsingResult Parse(size_t bytes_read) override {
        assert(bytes_read > 0);
        // TODO: currently only supports one pass.
        assert(state_ == State::kInit);
        assert(cursor_ == 0);
        assert(buffer_.size() > 0);
        assert(parsed_.empty());
        assert(buffer_[cursor_] != '*');

        const auto crlf = buffer_.find('\n', cursor_);
        if (crlf == Buffer::npos) {
            if (cursor_ + bytes_read > kMaxInlineBufferSize) {
                state_ = State::kError;
            } else {
                state_ = State::kParsing;
            }
            return {state_, 0, {}};
        }
        if (crlf == cursor_ || buffer_[crlf - 1] != '\r') {
            state_ = State::kError;
            return {state_, 0, {}};
        }

        // TODO: support quote and single quote.
        while (cursor_ < crlf - 1) {
            if (std::isspace(buffer_[cursor_])) {
                ++cursor_;
                continue;
            }
            auto next_space = cursor_ + 1;
            while (next_space < crlf - 1 && !std::isspace(buffer_[next_space])) {
                ++next_space;
            }
            parsed_.emplace_back(buffer_.data() + cursor_, next_space - cursor_);
            cursor_ = next_space;
        }
        state_ = State::kDone;
        cursor_ = crlf + 1;
        return {state_, cursor_, std::move(parsed_)};
    }
};

} // namespace rdss