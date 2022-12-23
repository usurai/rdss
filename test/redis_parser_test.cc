#include "resp_parser.h"

#include <gtest/gtest.h>

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
        EXPECT_EQ(res.bulk_strings.size(), 1);
        EXPECT_EQ(res.bulk_strings[0], "PING");
    }

    {
        const std::string buffer = "  PING  \r\n";
        InlineParser parser(buffer);
        auto res = parser.Parse(buffer.size());
        EXPECT_EQ(res.state, State::kDone);
        EXPECT_EQ(res.cursor, buffer.size());
        EXPECT_EQ(res.bulk_strings.size(), 1);
        EXPECT_EQ(res.bulk_strings[0], "PING");
    }

    {
        const std::string buffer = "SET K0 V0\r\n";
        InlineParser parser(buffer);
        auto res = parser.Parse(buffer.size());
        EXPECT_EQ(res.state, State::kDone);
        EXPECT_EQ(res.cursor, buffer.size());
        EXPECT_EQ(res.bulk_strings.size(), 3);
        EXPECT_EQ(res.bulk_strings[0], "SET");
        EXPECT_EQ(res.bulk_strings[1], "K0");
        EXPECT_EQ(res.bulk_strings[2], "V0");
    }

    {
        InlineParser parser("PING");
        auto res = parser.Parse(4);
        EXPECT_EQ(res.state, State::kParsing);
    }

    {
        InlineParser parser("\r\n");
        auto res = parser.Parse(2);
        EXPECT_EQ(res.state, State::kDone);
        EXPECT_EQ(res.cursor, 2);
        EXPECT_TRUE(res.bulk_strings.empty());
    }
}

} // namespace rdss::test