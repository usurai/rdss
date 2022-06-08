#include "client.h"

#include <gtest/gtest.h>

#include "climits"

namespace rdss::test {

class ClientTest : public testing::Test {
public:
    static Client CreateClient(std::string buffer) {
        Client c(nullptr, 0);
        c.query_buffer = std::move(buffer);
        c.read_length = c.query_buffer.size();
        return c;
    }

    static bool CompareArgv(Client& c, std::vector<std::string> argvs = {}) {
        if (c.arguments.size() != argvs.size()) {
            return false;
        }
        for (size_t i = 0; i < c.arguments.size(); ++i) {
            const auto& arg{c.arguments[i]};
            if (std::string_view(arg.data, arg.length).compare(argvs[i])) {
                return false;
            }
        }
        return true;
    }
};

TEST(ClientTest, parseBufferArgc) {
    auto get_argc = [&](const std::string& buffer) {
        Client c(nullptr, 0);
        c.query_buffer = buffer;
        c.read_length = buffer.size();
        c.ParseBuffer();
        return c.arguments.size();
    };

    EXPECT_EQ(get_argc(""), 0);
    EXPECT_EQ(get_argc("\r\n"), 0);
    EXPECT_EQ(get_argc("*\r\n"), 0);
    EXPECT_EQ(get_argc("*1\r\n"), 1);
    EXPECT_EQ(get_argc("*12345\r\n"), 12345);
    const auto int_max_str = "*" + std::to_string(INT_MAX) + "\r\n";
    EXPECT_EQ(get_argc(int_max_str), INT_MAX);
    const auto overflow_str = "*" + std::to_string(static_cast<long long>(INT_MAX) + 1) + "\r\n";
    EXPECT_EQ(get_argc(overflow_str), 0);
    EXPECT_EQ(get_argc("*-1\r\n"), 0);
    EXPECT_EQ(get_argc("*12345a\r\n"), 0);
    EXPECT_EQ(get_argc("*x1\r\n"), 0);
}

TEST(ClientTest, parseBuffer) {
    {
        auto c = ClientTest::CreateClient("*1\r\n$5\r\nHELLO\r\n");
        EXPECT_EQ(Client::ParseResult::Success, c.ParseBuffer());
        EXPECT_TRUE(ClientTest::CompareArgv(c, {"HELLO"}));
    }

    {
        auto c = ClientTest::CreateClient("*2\r\n$5\r\nHELLO\r\n$6\r\n WORLD\r\n");
        EXPECT_EQ(Client::ParseResult::Success, c.ParseBuffer());
        EXPECT_TRUE(ClientTest::CompareArgv(c, {"HELLO", " WORLD"}));
    }

    {
        auto c = ClientTest::CreateClient("*1\r\n$0\r\n\r\n");
        EXPECT_EQ(Client::ParseResult::Success, c.ParseBuffer());
        EXPECT_TRUE(ClientTest::CompareArgv(c, {""}));
    }

    {
        auto c = ClientTest::CreateClient("*1\r\n$5\r\nHELL\r\n");
        EXPECT_EQ(Client::ParseResult::Error, c.ParseBuffer());
    }

    {
        auto c = ClientTest::CreateClient("*2\r\n$5\r\nHELLO\r\n");
        EXPECT_EQ(Client::ParseResult::Error, c.ParseBuffer());
    }

    {
        auto c = ClientTest::CreateClient("*2\r\n$5\r\nHELLO\r\n$120\r\n");
        EXPECT_EQ(Client::ParseResult::Error, c.ParseBuffer());
    }
}

} // namespace rdss::test
