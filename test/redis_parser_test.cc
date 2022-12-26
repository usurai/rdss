#include "redis_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

namespace rdss::test {

using State = RedisParser::State;

class RedisParserTest : public testing::Test {};

TEST(RedisParserTest, inlineBasic) {
    constexpr size_t kBufferCapacity = 1024;
    VectorBuffer buffer(kBufferCapacity);

    {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.CommitWrite(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "  PING  \r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.CommitWrite(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "SET K0 V0\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.CommitWrite(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_THAT(res.second, testing::ElementsAre("SET", "K0", "V0"));
    }

    {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.CommitWrite(content.size()-2);
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kParsing);

        buffer.CommitWrite(2);
        auto res1 = parser.Parse();
        EXPECT_EQ(res1.first, State::kDone);
        EXPECT_THAT(res1.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "PING\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.CommitWrite(content.size()-1);
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kParsing);

        buffer.CommitWrite(1);
        auto res1 = parser.Parse();
        EXPECT_EQ(res1.first, State::kDone);
        EXPECT_THAT(res1.second, testing::ElementsAre("PING"));
    }

    {
        const std::string content = "\r\n";
        buffer.Reset();
        memcpy(buffer.Data(), content.data(), content.size());
        buffer.CommitWrite(content.size());
        InlineParser parser(&buffer);
        auto res = parser.Parse();
        EXPECT_EQ(res.first, State::kDone);
        EXPECT_TRUE(res.second.empty());
    }
}

} // namespace rdss::test