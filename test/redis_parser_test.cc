#include "redis_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

namespace rdss::test {

using State = RedisParser::State;

class RedisParserTest : public testing::Test {};

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
        const std::string content = "*3\r\n$3\r\nSET\r\n$2\r\nK0\r\n$6\r\nFOOBAR\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());

        for (size_t i = 4; i < content.size() - 1; ++i) {
            buffer.Reset();
            memcpy(buffer.Data(), content.data(), i);
            buffer.Produce(i);
            MultiBulkParser parser(&buffer);
            auto res = parser.Parse();
            EXPECT_EQ(res.first, State::kParsing);

            memcpy(buffer.Data(), content.data() + i, content.size() - i);
            buffer.Produce(content.size() - i);
            auto res1 = parser.Parse();
            EXPECT_EQ(res1.first, State::kDone)
              << "Committed buffer:'" << content.substr(0, i) << "'";
            EXPECT_THAT(res1.second, testing::ElementsAre("SET", "K0", "FOOBAR"));
        }
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

} // namespace rdss::test