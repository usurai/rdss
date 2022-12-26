#pragma once

#include "buffer.h"

#include <glog/logging.h>

#include <cassert>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {}

namespace rdss {

constexpr size_t kMaxInlineBufferSize = 1024 * 64;

class Buffer;

// class String;
using String = std::string;
using Strings = std::vector<String>;

class RedisParser {
public:
    enum class State : uint8_t { kInit, kError, kParsing, kDone };

    using ParsingResult = std::pair<State, Strings>;

    explicit RedisParser(Buffer* buffer)
      : state_(State::kInit)
      , buffer_(buffer) {
        assert(buffer_ != nullptr);
    }

    virtual ParsingResult Parse() = 0;

    void Reset() { state_ = State::kInit; }

    // UpdateBuffer();

protected:
    State state_;
    Buffer* buffer_;
};

class InlineParser : public RedisParser {
public:
    explicit InlineParser(Buffer* buffer)
      : RedisParser(buffer) {}

    ParsingResult Parse() {
        // TODO: naming
        auto buffer = buffer_->Stored();
        if (buffer.empty()) {
            state_ = State::kError;
            return {state_, {}};
        }

        const auto crlf = buffer.find("\r\n");
        if (crlf == Buffer::BufferView::npos) {
            if (buffer_->NumWritten() > kMaxInlineBufferSize) {
                state_ = State::kError;
            } else {
                state_ = State::kParsing;
            }
            return {state_, {}};
        }

        Strings result;
        size_t i = 0;
        while (i < buffer.size()) {
            if (std::isspace(buffer[i])) {
                ++i;
                continue;
            }
            auto next_space = i + 1;
            while (next_space < buffer.size() && !std::isspace(buffer[next_space])) {
                ++next_space;
            }
            result.emplace_back(buffer.data() + i, buffer.data() + next_space);
            i = next_space;
        }
        buffer_->Consume(buffer.size());
        state_ = State::kDone;
        return {state_, std::move(result)};
    }
};

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