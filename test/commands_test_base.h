#pragma once

#include "base/buffer.h"
#include "config.h"
#include "data_structure_service.h"
#include "resp/redis_parser.h"

#include <gtest/gtest.h>

#include <cstring>
namespace rdss::test {

class CommandsTestBase : public testing::Test {
protected:
    CommandsTestBase()
      : clock_(false)
      , service_(&config_, &clock_)
      , buffer_(1024 * 16) {}

    void SetUp() override {
        std::srand(static_cast<unsigned int>(time(nullptr)));
        clock_.SetTime(std::chrono::time_point_cast<Clock::TimePoint::duration>(
          std::chrono::system_clock::now()));
    }

    Result Invoke(std::string query) {
        if (!query.ends_with("\r\n")) {
            query += "\r\n";
        }
        buffer_.Reset();
        std::memcpy(buffer_.Sink().data(), query.data(), query.size());
        buffer_.Produce(query.size());
        auto parse_result = InlineParser::ParseInline(&buffer_);
        EXPECT_EQ(parse_result.first, RedisParser::State::kDone);
        return service_.Invoke(parse_result.second);
    }

    void ExpectKeyValue(std::string_view key, std::string_view value) {
        auto entry = service_.DataHashTable()->Find(key);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(*entry->value, value);
    }

    void ExpectNoKey(std::string_view key) {
        auto data_entry = service_.DataHashTable()->Find(key);
        if (data_entry != nullptr) {
            auto expire_entry = service_.GetExpireHashTable()->Find(key);
            if (expire_entry) {
                EXPECT_LE(expire_entry->value, clock_.Now());
            }
        }
    }

    void ExpectTTL(std::string_view key, std::chrono::milliseconds ttl) {
        auto data_entry = service_.DataHashTable()->Find(key);
        ASSERT_NE(data_entry, nullptr);
        auto expire_entry = service_.GetExpireHashTable()->Find(key);
        ASSERT_NE(expire_entry, nullptr);
        EXPECT_EQ(expire_entry->value - clock_.Now(), ttl);
    }

    void ExpectNoTTL(std::string_view key) {
        auto entry = service_.GetExpireHashTable()->Find(key);
        if (entry != nullptr) {
            EXPECT_LE(entry->value, clock_.Now());
        }
    }

    void ExpectNull(Result result) {
        ASSERT_EQ(result.Size(), 1);
        EXPECT_TRUE(result.is_null[0]);
    }

    void ExpectString(Result result, std::string_view str) {
        ASSERT_EQ(result.Size(), 1);
        ASSERT_EQ(result.data.size(), 1);
        EXPECT_EQ(result.data[0], str);
    }

    void ExpectInt(Result result, int i) {
        ASSERT_EQ(result.Size(), 1);
        ASSERT_EQ(result.ints.size(), 1);
        EXPECT_EQ(result.ints[0], i);
    }

    void AdvanceTime(Clock::TimePoint::duration duration) {
        clock_.SetTime(clock_.Now() + duration);
    }

    Config config_;
    Clock clock_;
    DataStructureService service_;
    Buffer buffer_;
};

} // namespace rdss::test
