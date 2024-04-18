#include "base/buffer.h"
#include "constants.h"
#include "resp/resp_parser.h"
#include "util.h"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

namespace rdss::test {

class RespParserTest : public testing::Test {
protected:
    void SetUp() override { std::srand(static_cast<unsigned int>(time(nullptr))); }

    void ExpectEQ(StringViews lhs, std::vector<std::string> expected) {
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(lhs[i], expected[i]);
        }
    }

    void ExpectParseInline(Buffer& buffer, std::vector<std::string> expected) {
        StringViews result;
        size_t result_size;
        EXPECT_EQ(ParserState::kDone, ParseInline(&buffer, result, result_size));
        EXPECT_EQ(result_size, expected.size());
        EXPECT_GE(result.size(), result_size);
        ExpectEQ(result, std::move(expected));
    }
};

TEST_F(RespParserTest, inlineBasic) {
    constexpr size_t kBufferCapacity = 1024;
    Buffer buffer(kBufferCapacity);

    {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        ExpectParseInline(buffer, {"PING"});
    }

    {
        const std::string content = "  PING  \r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        ExpectParseInline(buffer, {"PING"});
    }

    {
        const std::string content = "SET K0 V0\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        ExpectParseInline(buffer, {"SET", "K0", "V0"});
    }

    for (size_t remaining = 2; remaining > 0; --remaining) {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size() - remaining);
        StringViews result;
        size_t result_size;
        EXPECT_EQ(ParseInline(&buffer, result, result_size), ParserState::kParsing);

        buffer.Produce(remaining);
        ExpectParseInline(buffer, {"PING"});
    }

    {
        const std::string content = "\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        StringViews result;
        size_t result_size;
        EXPECT_EQ(ParseInline(&buffer, result, result_size), ParserState::kParsing);
    }
}

TEST_F(RespParserTest, mbulkBasic) {
    constexpr size_t kBufferCapacity = 1024;
    Buffer buffer(kBufferCapacity);

    {
        const std::string content = "*1\r\n$4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        StringViews result;
        EXPECT_EQ(parser.Parse(result), ParserState::kDone);
        EXPECT_EQ(parser.GetResultSize(), 1);
        ExpectEQ(result, {"PING"});
    }

    {
        const std::string content = "*3\r\n$3\r\nSET\r\n$2\r\nK0\r\n$6\r\nFOOBAR\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        StringViews result;
        EXPECT_EQ(parser.Parse(result), ParserState::kDone);
        EXPECT_EQ(parser.GetResultSize(), 3);
        ExpectEQ(result, {"SET", "K0", "FOOBAR"});
    }

    {
        const std::string content = "*0\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        StringViews result;
        EXPECT_EQ(parser.Parse(result), ParserState::kDone);
        EXPECT_EQ(parser.GetResultSize(), 0);
        ExpectEQ(result, {});
    }

    {
        const std::string content = "*1\r\n4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        StringViews result;
        EXPECT_EQ(parser.Parse(result), ParserState::kError);
    }

    {
        const std::string content = "*-1\r\n$4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        StringViews result;
        EXPECT_EQ(parser.Parse(result), ParserState::kError);
    }

    {
        const std::string content = "*1\r\n$-4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        StringViews result;
        EXPECT_EQ(parser.Parse(result), ParserState::kError);
    }

    {
        const std::string content = "*1\r\n$6\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        StringViews result;
        EXPECT_EQ(parser.Parse(result), ParserState::kParsing);
    }
}

TEST_F(RespParserTest, mbulkTwoPasses) {
    constexpr size_t kBufferCapacity = 1024;
    Buffer buffer(kBufferCapacity);
    const std::string content = "*3\r\n$3\r\nSET\r\n$2\r\nK0\r\n$6\r\nFOOBAR\r\n";

    for (size_t i = 4; i < content.size() - 1; ++i) {
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), i);
        buffer.Produce(i);
        MultiBulkParser parser(&buffer);
        StringViews result;
        auto res = parser.Parse(result);
        EXPECT_EQ(res, ParserState::kParsing);

        memcpy(buffer.Data(), content.data() + i, content.size() - i);
        buffer.Produce(content.size() - i);
        auto res1 = parser.Parse(result);
        EXPECT_EQ(res1, ParserState::kDone) << "Committed buffer:'" << content.substr(0, i) << "'";
        ExpectEQ(result, {"SET", "K0", "FOOBAR"});
    }
}

TEST_F(RespParserTest, bufferExpansion) {
    constexpr size_t num_argument = 100;
    constexpr size_t min_length = 512;
    constexpr size_t max_length = 16 * 1024;
    std::string full_query = '*' + std::to_string(num_argument) + "\r\n";
    std::vector<std::string> arguments;
    arguments.reserve(num_argument);

    for (size_t i = 0; i < num_argument; ++i) {
        arguments.push_back(GenRandomString(
          min_length + static_cast<size_t>(std::rand()) % (max_length - min_length)));
        full_query += '$' + std::to_string(arguments.back().size()) + "\r\n" + arguments.back()
                      + "\r\n";
    }

    Buffer buffer;
    MultiBulkParser parser(&buffer);
    StringViews result;
    size_t offset{0};
    while (offset < full_query.size()) {
        auto start = buffer.EnsureAvailable(
          kIOGenericBufferSize, buffer.Capacity() >= kIOGenericBufferSize);
        if (start != nullptr && parser.InProgress()) {
            parser.BufferUpdate(start, buffer.Start(), result);
        }

        auto sink = buffer.Sink();
        const size_t bytes_to_copy = std::min(full_query.size() - offset, sink.size());
        memcpy(sink.data(), full_query.data() + offset, bytes_to_copy);
        buffer.Produce(bytes_to_copy);
        offset += bytes_to_copy;

        auto res = parser.Parse(result);
        EXPECT_NE(res, ParserState::kError);

        if (offset == full_query.size()) {
            EXPECT_EQ(res, ParserState::kDone);
            EXPECT_EQ(parser.GetResultSize(), num_argument);
            ExpectEQ(result, arguments);
        }
    }
}

} // namespace rdss::test
