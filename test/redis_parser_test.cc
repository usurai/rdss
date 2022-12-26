#include "redis_parser.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <string>

namespace rdss::test {

using State = RedisParser::State;

class RedisParserTest : public testing::Test {};

TEST(RedisParserTest, inlineBasic) {
    {
        const std::string buffer = "PING\r\n";
        InlineParser parser(buffer);
        auto res = parser.Parse(buffer.size());
        EXPECT_EQ(res.state, State::kDone);
        EXPECT_EQ(res.cursor, buffer.size());
        EXPECT_THAT(res.bulk_strings, testing::ElementsAre("PING"));
    }

    {
        const std::string buffer = "  PING  \r\n";
        InlineParser parser(buffer);
        auto res = parser.Parse(buffer.size());
        EXPECT_EQ(res.state, State::kDone);
        EXPECT_EQ(res.cursor, buffer.size());
        EXPECT_THAT(res.bulk_strings, testing::ElementsAre("PING"));
    }

    {
        const std::string buffer = "SET K0 V0\r\n";
        InlineParser parser(buffer);
        auto res = parser.Parse(buffer.size());
        EXPECT_EQ(res.state, State::kDone);
        EXPECT_EQ(res.cursor, buffer.size());
        EXPECT_THAT(res.bulk_strings, testing::ElementsAre("SET", "K0", "V0"));
    }

    {
        std::string buffer = "PING";
        InlineParser parser(buffer);
        auto res = parser.Parse(buffer.size());
        EXPECT_EQ(res.state, State::kParsing);
        buffer += "\r\n";
        auto res1 = parser.Parse(2);
        EXPECT_EQ(res1.state, State::kDone);
        EXPECT_EQ(res1.cursor, buffer.size());
        EXPECT_THAT(res1.bulk_strings, testing::ElementsAre("PING"));
    }

    {
        InlineParser parser("\r\n");
        auto res = parser.Parse(2);
        EXPECT_EQ(res.state, State::kDone);
        EXPECT_EQ(res.cursor, 2);
        EXPECT_TRUE(res.bulk_strings.empty());
    }

    {
        const std::string buffer = "PING\r\n";
        InlineParser parser(buffer);
        auto res = parser.Parse(5);
        EXPECT_EQ(res.state, State::kParsing);
        auto res1 = parser.Parse(1);
        EXPECT_EQ(res1.state, State::kDone);
    }
}

} // namespace rdss::test