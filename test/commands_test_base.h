#pragma once

#include "base/buffer.h"
#include "base/config.h"
#include "resp/resp_parser.h"
#include "service/data_structure_service.h"

#include <gtest/gtest.h>

#include <cstring>
namespace rdss::test {

class CommandsTestBase : public testing::Test {
protected:
    CommandsTestBase()
      : clock_(false)
      , service_(&config_, nullptr, &clock_, std::promise<void>{})
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
        StringViews args;
        size_t arg_size;
        const auto parse_result = ParseInline(&buffer_, args, arg_size);
        EXPECT_EQ(parse_result, ParserState::kDone);
        Result result;
        service_.Invoke(std::span<StringView>{args.data(), arg_size}, result);
        return result;
    }

    void ExpectKeyValue(std::string_view key, std::string_view value) {
        auto entry = service_.DataTable()->Find(key);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(*entry->value, value);
    }

    void ExpectNoKey(std::string_view key) {
        auto data_entry = service_.DataTable()->Find(key);
        if (data_entry != nullptr) {
            auto expire_entry = service_.ExpireTable()->Find(key);
            if (expire_entry) {
                EXPECT_LE(expire_entry->value, clock_.Now());
            }
        }
    }

    void ExpectTTL(std::string_view key, std::chrono::milliseconds ttl) {
        auto data_entry = service_.DataTable()->Find(key);
        ASSERT_NE(data_entry, nullptr);
        auto expire_entry = service_.ExpireTable()->Find(key);
        ASSERT_NE(expire_entry, nullptr);
        EXPECT_EQ(expire_entry->value - clock_.Now(), ttl);
    }

    void ExpectNoTTL(std::string_view key) {
        auto entry = service_.ExpireTable()->Find(key);
        if (entry != nullptr) {
            EXPECT_LE(entry->value, clock_.Now());
        }
    }

    void ExpectOk(Result result) { EXPECT_EQ(result.type, Result::Type::kOk); }

    void ExpectNull(Result result) { ASSERT_EQ(result.type, Result::Type::kNil); }

    void ExpectString(Result result, std::string_view str) {
        ASSERT_EQ(result.type, Result::Type::kString);
        ASSERT_NE(result.string_ptr, nullptr);
        EXPECT_EQ(*result.string_ptr, str);
    }

    void ExpectStrings(Result result, std::vector<std::string> strings) {
        ASSERT_EQ(result.type, Result::Type::kStrings);
        ASSERT_EQ(result.strings.size(), strings.size());
        for (size_t i = 0; i < strings.size(); ++i) {
            if (strings[i].empty()) {
                if (result.strings[i] == nullptr) {
                    continue;
                }
                EXPECT_TRUE(result.strings[i]->empty());
            } else {
                EXPECT_FALSE(result.strings[i]->compare(strings[i]));
            }
        }
    }

    void ExpectInt(Result result, int64_t i) {
        ASSERT_EQ(result.type, Result::Type::kInt);
        EXPECT_EQ(result.int_value, i);
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
