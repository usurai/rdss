#include "client.h"

#include <gtest/gtest.h>

#include "climits"

using rdss::Client;

TEST(ClientTest, parseBuffer) {
    Client c(nullptr, 0);

    auto assert_argc = [&](const std::string& buffer, size_t expected_argc) {
        memset(c.query_buffer.data(), 0, c.query_buffer.size());
        memcpy(c.query_buffer.data(), buffer.data(), buffer.size());
        c.cursor = 0;
        c.arguments.clear();
        c.ParseBuffer();
        EXPECT_EQ(expected_argc, c.arguments.size());
    };

    assert_argc("", 0);
    assert_argc("\r\n", 0);
    assert_argc("*\r\n", 0);
    assert_argc("*1\r\n", 1);
    assert_argc("*12345\r\n", 12345);
    const auto int_max_str = "*" + std::to_string(INT_MAX) + "\r\n";
    assert_argc(int_max_str, INT_MAX);
    const auto overflow_str = "*" + std::to_string(static_cast<long long>(INT_MAX) + 1) + "\r\n";
    assert_argc(overflow_str, 0);
    assert_argc("*-1\r\n", 0);
    assert_argc("*12345a\r\n", 0);
    assert_argc("*x1\r\n", 0);
}
