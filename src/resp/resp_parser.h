// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include <string>
#include <vector>

namespace rdss {

using StringView = std::string_view;
using StringViews = std::vector<StringView>;

class Buffer;

enum class ParserState : uint8_t {
    kInit,    // Parsing is not yet started.
    kError,   // Parsing error, need to reset state.
    kParsing, // In progress, part of buffer might be consumed.
    kDone
};

/// Try parse 'buffer' until the first occurance of CRLF.
/// TODO: Currently only supports parsing in one pass. Need to add support of accumulative parsing,
/// e.g., inline arguments that are larger than initial buffer size.
ParserState ParseInline(Buffer* buffer, StringViews& result, size_t& result_size);

class MultiBulkParser {
public:
    explicit MultiBulkParser(Buffer* buffer);

    ParserState Parse(StringViews& result);

    bool InProgress() const { return state_ == ParserState::kParsing; }

    void Reset();

    /// If 'buffer_' gets expanded, it's current data is moved to new memory location. If parser
    /// holds partial result view over the original memory of 'query_buffer', it should be updated.
    void BufferUpdate(const char* original, const char* updated, StringViews& result);

    /// Should only be called when the last call of Parse() returns kDone.
    size_t GetResultSize() const;

private:
    size_t ParseArgNum(StringView src);

    ParserState state_ = ParserState::kInit;
    Buffer* buffer_;
    size_t args_ = 0;
    size_t cur_arg_idx_ = 0;
};

} // namespace rdss
