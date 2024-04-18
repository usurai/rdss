#include "resp/resp_parser.h"

#include "base/buffer.h"

#include <glog/logging.h>

#include <cassert>
#include <charconv>
#include <memory>

namespace rdss {

constexpr size_t kMaxInlineBufferSize = 1024 * 16;

ParserState ParseInline(Buffer* buffer, StringViews& result, size_t& result_size) {
    auto src = buffer->Source();
    if (src.empty()) {
        return ParserState::kError;
    }

    const auto crlf = src.find("\r\n");
    if (crlf == Buffer::View::npos) {
        if (src.size() >= kMaxInlineBufferSize) {
            return ParserState::kError;
        }
        return ParserState::kParsing;
    }

    size_t i = 0;
    size_t arg_index{0};
    while (i < crlf) {
        if (std::isspace(src[i])) {
            ++i;
            continue;
        }
        auto next_space = i + 1;
        while (next_space < crlf && !std::isspace(src[next_space])) {
            ++next_space;
        }
        if (result.size() == arg_index) {
            result.emplace_back(src.data() + i, src.data() + next_space);
        } else {
            result[arg_index] = {src.data() + i, src.data() + next_space};
        }
        ++arg_index;
        i = next_space;
    }
    buffer->Consume(crlf + 2);
    if (arg_index == 0) {
        return ParserState::kParsing;
    }
    result_size = arg_index;
    return ParserState::kDone;
}

MultiBulkParser::MultiBulkParser(Buffer* buffer)
  : buffer_(buffer) {}

ParserState MultiBulkParser::Parse(StringViews& result) {
    if (state_ == ParserState::kError || state_ == ParserState::kDone) {
        Reset();
    }

    auto src = buffer_->Source();
    if (src.empty()) {
        return state_;
    }

    // assert first char is *
    size_t cursor{0};
    if (state_ == ParserState::kInit && (cursor = ParseArgNum(src)) == 0) {
        return state_;
    }
    result.reserve(args_);

    // While there is arg to parse: 1.parse string_len 2.parse string[i]
    while (cur_arg_idx_ < args_) {
        if (cursor == src.size()) {
            return state_;
        }

        if (src[cursor] != '$') {
            state_ = ParserState::kError;
            return state_;
        }
        auto crlf = src.find("\r\n", cursor);
        if (crlf == cursor + 1) {
            state_ = ParserState::kError;
            return state_;
        }
        if (crlf == Buffer::View::npos) {
            return state_;
        }

        int str_len;
        auto [_, ec] = std::from_chars(src.data() + cursor + 1, src.data() + crlf, str_len);
        if (ec != std::errc() || str_len < 0) {
            state_ = ParserState::kError;
            return state_;
        }
        const auto old_cursor = cursor;
        cursor = crlf + 2;
        if (cursor + static_cast<size_t>(str_len) + 2 > src.size()) {
            return state_;
        }
        const auto expected_crlf = cursor + static_cast<size_t>(str_len);
        if (src[expected_crlf] != '\r' || src[expected_crlf + 1] != '\n') {
            state_ = ParserState::kError;
            return state_;
        }
        if (result.size() <= cur_arg_idx_) {
            assert(result.size() == cur_arg_idx_);
            result.emplace_back(src.data() + cursor, str_len);
        } else {
            result[cur_arg_idx_] = StringView{src.data() + cursor, static_cast<size_t>(str_len)};
        }
        ++cur_arg_idx_;
        cursor += static_cast<size_t>(str_len) + 2;
        buffer_->Consume(cursor - old_cursor);
    }
    state_ = ParserState::kDone;
    return state_;
}

void MultiBulkParser::Reset() {
    state_ = ParserState::kInit;
    args_ = 0;
    cur_arg_idx_ = 0;
}

void MultiBulkParser::BufferUpdate(const char* original, const char* updated, StringViews& result) {
    for (size_t i = 0; i < cur_arg_idx_; ++i) {
        const auto offset = result[i].data() - original;
        StringView new_sv(updated + offset, result[i].size());
        result[i].swap(new_sv);
    }
}

size_t MultiBulkParser::GetResultSize() const {
    assert(state_ == ParserState::kDone);
    return args_;
}

size_t MultiBulkParser::ParseArgNum(StringView src) {
    size_t cursor{0};
    if (src[0] != '*') {
        state_ = ParserState::kError;
        return cursor;
    }
    auto crlf = src.find("\r\n", 1);
    if (crlf == Buffer::View::npos) {
        return cursor;
    }
    int32_t parsed_args;
    auto [_, ec] = std::from_chars(src.data() + 1, src.data() + crlf, parsed_args);
    if (ec != std::errc() || parsed_args < 0) {
        state_ = ParserState::kError;
        return cursor;
    }
    args_ = static_cast<size_t>(parsed_args);
    VLOG(2) << "args:" << args_;
    state_ = ParserState::kParsing;
    cursor = crlf + 2;
    buffer_->Consume(cursor);
    return cursor;
}

} // namespace rdss
