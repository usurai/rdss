#include "base/buffer.h"
#include "constants.h"
#include "resp/redis_parser.h"
#include "util.h"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

namespace rdss::test {

using State = RedisParser::State;

class RedisParserTest : public testing::Test {
    void SetUp() override { std::srand(static_cast<unsigned int>(time(nullptr))); }
};

TEST(RedisParserTest, inlineBasic) {
    constexpr size_t kBufferCapacity = 1024;
    Buffer buffer(kBufferCapacity);

    {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "  PING  \r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "SET K0 V0\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("SET", "K0", "V0"));
    }

    {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size() - 2);
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kParsing);

        buffer.Produce(2);
        auto res1 = parser.Parse();
        EXPECT_EQ(res1.first, State::kDone);
        EXPECT_THAT(res1.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size() - 1);
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kParsing);

        buffer.Produce(1);
        auto res1 = parser.Parse();
        EXPECT_EQ(res1.first, State::kDone);
        EXPECT_THAT(res1.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_TRUE(res.second.empty());
    }
}

TEST(RedisParserTest, mbulkBasic) {
    constexpr size_t kBufferCapacity = 1024;
    Buffer buffer(kBufferCapacity);

    {
        const std::string content = "*1\r\n$4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "*3\r\n$3\r\nSET\r\n$2\r\nK0\r\n$6\r\nFOOBAR\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("SET", "K0", "FOOBAR"));
    }

    {
        const std::string content = "*0\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_TRUE(res.second.empty());
    }

    {
        const std::string content = "*1\r\n4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kError);
    }

    {
        const std::string content = "*-1\r\n$4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kError);
    }

    {
        const std::string content = "*1\r\n$-4\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kError);
    }

    {
        const std::string content = "*1\r\n$6\r\nPING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.Produce(content.size());
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kParsing);
    }
}

TEST(RedisParserTest, mbulkTwoPasses) {
    constexpr size_t kBufferCapacity = 1024;
    Buffer buffer(kBufferCapacity);
    const std::string content = "*3\r\n$3\r\nSET\r\n$2\r\nK0\r\n$6\r\nFOOBAR\r\n";

    for (size_t i = 4; i < content.size() - 1; ++i) {
        VLOG(1) << "First pass size: " << i;
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), i);
        buffer.Produce(i);
        MultiBulkParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kParsing);

        memcpy(buffer.Data(), content.data() + i, content.size() - i);
        buffer.Produce(content.size() - i);
        auto res1 = parser.Parse();
        EXPECT_EQ(res1.first, State::kDone) << "Committed buffer:'" << content.substr(0, i) << "'";
        ASSERT_THAT(res1.second, testing::ElementsAre("SET", "K0", "FOOBAR"))
          << "Committed buffer:'" << content.substr(0, i) << "'";
    }
}

TEST(RedisParserTest, bufferExpansion) {
    constexpr size_t num_argument = 100;
    constexpr size_t min_length = 512;
    constexpr size_t max_length = 16 * 1024;
    std::string full_query = '*' + std::to_string(num_argument) + "\r\n";
    std::vector<std::string> arguments;
    arguments.reserve(num_argument);

    for (size_t i = 0; i < num_argument; ++i) {
        arguments.push_back(GenRandomString(min_length + std::rand() % (max_length - min_length)));
        full_query += '$' + std::to_string(arguments.back().size()) + "\r\n" + arguments.back()
                      + "\r\n";
    }

    Buffer buffer;
    MultiBulkParser parser(&buffer);
    size_t offset{0};
    while (offset < full_query.size()) {
        auto start = buffer.EnsureAvailable(
          kIOGenericBufferSize, buffer.Capacity() >= kIOGenericBufferSize);
        if (start != nullptr && parser.InProgress()) {
            parser.BufferUpdate(start, buffer.Start());
        }

        auto sink = buffer.Sink();
        const size_t bytes_to_copy = std::min(full_query.size() - offset, sink.size());
        memcpy(sink.data(), full_query.data() + offset, bytes_to_copy);
        buffer.Produce(bytes_to_copy);
        offset += bytes_to_copy;

        auto result = parser.Parse();
        EXPECT_NE(result.first, RedisParser::State::kError);

        if (offset == full_query.size()) {
            EXPECT_EQ(result.first, RedisParser::State::kDone);
            EXPECT_EQ(result.second.size(), num_argument);
            for (size_t i = 0; i < num_argument; ++i) {
                EXPECT_EQ(arguments[i].size(), result.second[i].size());
                EXPECT_FALSE(arguments[i].compare(result.second[i]));
            }
        }
    }
}

} // namespace rdss::test
