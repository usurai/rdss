#pragma once

#include <string>
#include <vector>

namespace rdss {

using StringView = std::string_view;
using StringViews = std::vector<StringView>;

class Buffer;

class RedisParser {
public:
    enum class State : uint8_t { kInit, kError, kParsing, kDone };
    using ParsingResult = std::pair<State, StringViews>;

public:
    explicit RedisParser(Buffer* buffer);

    virtual ~RedisParser() = default;

    virtual ParsingResult Parse() = 0;

    virtual void Reset() { state_ = State::kInit; }

    virtual bool InProgress() const { return false; }

protected:
    State state_;
    Buffer* buffer_;
};

class InlineParser : public RedisParser {
public:
    explicit InlineParser(Buffer* buffer)
      : RedisParser(buffer) {}

    virtual ~InlineParser() = default;

    static ParsingResult ParseInline(Buffer* buffer);

    ParsingResult Parse();
};

class MultiBulkParser : public RedisParser {
public:
    explicit MultiBulkParser(Buffer* buffer)
      : RedisParser(buffer)
      , args_{0} {}

    virtual ~MultiBulkParser() = default;

    virtual ParsingResult Parse() override;

    virtual bool InProgress() const override { return state_ == State::kParsing; }

    virtual void Reset() override;

private:
    int32_t args_;
    int32_t args_to_parse_;
    StringViews result_;
};

} // namespace rdss
