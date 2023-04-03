#include "redis_parser.h"

#include "buffer.h"

#include <glog/logging.h>

#include <cassert>
#include <charconv>
#include <memory>

namespace rdss {

using ParsingResult = RedisParser::ParsingResult;

RedisParser::RedisParser(Buffer* buffer)
  : state_(State::kInit)
  , buffer_(buffer) {
    assert(buffer_ != nullptr);
}

ParsingResult InlineParser::ParseInline(Buffer* buffer) {
    InlineParser parser(buffer);
    return parser.Parse();
}

ParsingResult InlineParser::Parse() {
    // TODO: naming
    auto buffer = buffer_->Source();
    if (buffer.empty()) {
        state_ = State::kError;
        return {state_, {}};
    }

    const auto crlf = buffer.find("\r\n");
    if (crlf == Buffer::View::npos) {
        if (buffer_->NumWritten() > kMaxInlineBufferSize) {
            state_ = State::kError;
        } else {
            state_ = State::kParsing;
        }
        return {state_, {}};
    }

    StringViews result;
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

ParsingResult MultiBulkParser::Parse() {
    // assert(state_ != State::kError);
    if (state_ != State::kParsing) {
        Reset();
    }

    auto buffer = buffer_->Source();
    // LOG(INFO) << "Parsing:'" << buffer << "'";
    if (buffer.empty()) {
        return {state_, {}};
    }

    // assert first char is *
    // parse args
    size_t cursor{0};
    if (state_ == State::kInit) {
        assert(buffer[0] == '*');
        auto crlf = buffer.find("\r\n", 1);
        if (crlf == Buffer::View::npos) {
            return {state_, {}};
        }
        auto [_, ec] = std::from_chars(buffer.data() + 1, buffer.data() + crlf, args_);
        if (ec != std::errc() || args_ < 0) {
            state_ = State::kError;
            return {state_, {}};
        }
        // LOG(INFO) << "args:" << args_;
        state_ = State::kParsing;
        args_to_parse_ = args_;
        cursor = crlf + 2;
        buffer_->Consume(cursor);
    }

    // while args_to_parse
    // parse string_len
    // parse string[i]
    while (args_to_parse_) {
        if (cursor >= buffer.size()) {
            return {state_, {}};
        }

        if (buffer[cursor] != '$') {
            state_ = State::kError;
            return {state_, {}};
        }
        auto crlf = buffer.find("\r\n", cursor);
        if (crlf == cursor + 1) {
            state_ = State::kError;
            return {state_, {}};
        }
        if (crlf == Buffer::View::npos) {
            return {state_, {}};
        }
        int str_len;
        auto [_, ec] = std::from_chars(buffer.data() + cursor + 1, buffer.data() + crlf, str_len);
        if (ec != std::errc() || str_len < 0) {
            state_ = State::kError;
            return {state_, {}};
        }
        const auto old_cursor = cursor;
        cursor = crlf + 2;
        if (cursor + static_cast<size_t>(str_len) + 2 > buffer.size()) {
            return {state_, {}};
        }
        const auto expected_crlf = cursor + static_cast<size_t>(str_len);
        if (buffer[expected_crlf] != '\r' || buffer[expected_crlf + 1] != '\n') {
            state_ = State::kError;
            return {state_, {}};
        }
        result_.emplace_back(buffer.data() + cursor, str_len);
        cursor += static_cast<size_t>(str_len) + 2;
        --args_to_parse_;
        buffer_->Consume(cursor - old_cursor);
    }
    state_ = State::kInit;
    return {State::kDone, std::move(result_)};
}

void MultiBulkParser::Reset() {
    RedisParser::Reset();
    args_ = 0;
    args_to_parse_ = 0;
    result_.clear();
}

} // namespace rdss
