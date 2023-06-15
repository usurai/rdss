#include "../buffer.h"
#include "../clock.h"
#include "../config.h"
#include "../data_structure_service.h"
#include "../redis_parser.h"
#include "../replier.h"
#include "../string_commands.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace rdss::test {

class StringCommandsTest : public testing::Test {
protected:
    StringCommandsTest()
      : clock_(false)
      , service_(&config_, &clock_)
      , buffer_(1024 * 16) {}

    void SetUp() override {
        std::srand(static_cast<unsigned int>(time(nullptr)));
        RegisterStringCommands(&service_);
        clock_.SetTime(std::chrono::time_point_cast<Clock::TimePoint::duration>(
          std::chrono::system_clock::now()));
    }

    Result Invoke(std::string query) {
        std::memcpy(buffer_.Sink().data(), query.data(), query.size());
        buffer_.Produce(query.size());
        auto [parse_result, args] = InlineParser::ParseInline(&buffer_);
        return service_.Invoke(args);
    }

    void ExpectKeyValue(std::string_view key, std::string_view value) {
        auto entry = service_.DataHashTable()->Find(key);
        EXPECT_NE(entry, nullptr);
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

    void AdvanceTime(Clock::TimePoint::duration duration) {
        clock_.SetTime(clock_.Now() + duration);
    }

    Config config_;
    Clock clock_;
    DataStructureService service_;
    Buffer buffer_;
};

TEST_F(StringCommandsTest, SetTest) {
    using namespace std::chrono;

    Invoke("SET k0 v0\r\n");
    ExpectKeyValue("k0", "v0");
    Invoke("SET k0 v1\r\n");
    ExpectKeyValue("k0", "v1");

    Invoke("SET k0 v2 NX\r\n");
    ExpectKeyValue("k0", "v1");
    Invoke("SET k1 v0 NX\r\n");
    ExpectKeyValue("k1", "v0");

    Invoke("SET k2 v0 XX\r\n");
    ExpectNoKey("k2");
    Invoke("SET k1 v1 XX\r\n");
    ExpectKeyValue("k1", "v1");

    ExpectNoTTL("k0");
    Invoke("SET k0 v0 PX 100\r\n");
    ExpectTTL("k0", 100ms);
    Invoke("SET k0 v0 PX 2000\r\n");
    ExpectTTL("k0", 2s);
    Invoke("SET k0 v0\r\n");
    ExpectNoTTL("k0");
    auto pxat = clock_.Now() + 1000ms;
    Invoke("SET k0 v0 PXAT " + std::to_string(pxat.time_since_epoch().count()) + "\r\n");
    ExpectTTL("k0", 1s);
    pxat = clock_.Now() - 1000ms;
    Invoke("SET k0 v0 PXAT " + std::to_string(pxat.time_since_epoch().count()) + "\r\n");
    ExpectNoTTL("k0");

    // Zero-out milliseconds to make EXAT do not lose precision.
    clock_.SetTime(Clock::TimePoint{2000s});
    ExpectNoTTL("k1");
    Invoke("SET k1 v0 EX 100\r\n");
    ExpectTTL("k1", 100s);
    Invoke("SET k1 v0 EX 2000\r\n");
    ExpectTTL("k1", 2000s);
    Invoke("SET k1 v0\r\n");
    ExpectNoTTL("k1");
    auto exat = clock_.Now() + 1000s;
    Invoke("SET k1 v0 EXAT " + std::to_string(exat.time_since_epoch().count() / 1000) + "\r\n");
    ExpectTTL("k1", 1000s);
    exat = clock_.Now() - 1000s;
    Invoke("SET k1 v0 EXAT " + std::to_string(exat.time_since_epoch().count() / 1000) + "\r\n");
    ExpectNoTTL("k1");

    clock_.SetTime(Clock::TimePoint{2000s});
    Invoke("SET k0 v0 PX 100\r\n");
    ExpectTTL("k0", 100ms);
    AdvanceTime(50ms);
    ExpectTTL("k0", 50ms);
    AdvanceTime(49ms);
    ExpectTTL("k0", 1ms);
    AdvanceTime(1ms);
    ExpectNoTTL("k0");

    Invoke("SET k0 v0\r\n");
    Invoke("SET k0 v0 KEEPTTL\r\n");
    ExpectNoTTL("k0");
    Invoke("SET k0 v0 EX 100\r\n");
    ExpectTTL("k0", 100s);
    Invoke("SET k0 v1 KEEPTTL\r\n");
    ExpectTTL("k0", 100s);

    Invoke("SET k0 v0\r\n");
    auto res = Invoke("SET k0 v1 GET\r\n");
    EXPECT_EQ(res.Size(), 1);
    EXPECT_EQ(res.data[0], "v0");
    Invoke("SET k0 v0 PX 100\r\n");
    res = Invoke("SET k0 v2 GET PX 100\r\n");
    EXPECT_EQ(res.Size(), 1);
    EXPECT_EQ(res.data[0], "v0");
    AdvanceTime(100ms);
    ExpectNoKey("k0");
    res = Invoke("SET k0 v3 GET\r\n");
    EXPECT_EQ(res.Size(), 1);
    EXPECT_TRUE(res.is_null[0]);
}

} // namespace rdss::test
