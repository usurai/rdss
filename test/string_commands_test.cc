#include "base/buffer.h"
#include "base/clock.h"
#include "commands/string_commands.h"
#include "config.h"
#include "data_structure_service.h"
#include "resp/redis_parser.h"
#include "resp/replier.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace rdss::test {

using namespace std::chrono;

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

TEST_F(StringCommandsTest, SetTest) {
    // Insert, then update.
    Invoke("SET k0 v0");
    ExpectKeyValue("k0", "v0");
    Invoke("SET k0 v1");
    ExpectKeyValue("k0", "v1");

    // NX not exists -> insert.
    Invoke("SET k0 v2 NX");
    ExpectKeyValue("k0", "v1");
    // NX exists -> noop.
    Invoke("SET k1 v0 NX");
    ExpectKeyValue("k1", "v0");

    // XX not exists -> insert.
    Invoke("SET k2 v0 XX");
    ExpectNoKey("k2");
    // XX exists -> update.
    Invoke("SET k1 v1 XX");
    ExpectKeyValue("k1", "v1");

    // PX no exists.
    ExpectNoTTL("k0");
    Invoke("SET k0 v0 PX 100");
    ExpectTTL("k0", 100ms);
    // PX update expire time.
    Invoke("SET k0 v0 PX 2000");
    ExpectTTL("k0", 2s);
    // SET invalidates previous expire.
    Invoke("SET k0 v0");
    ExpectNoTTL("k0");
    auto pxat = clock_.Now() + 1000ms;
    // PXAT
    Invoke("SET k0 v0 PXAT " + std::to_string(pxat.time_since_epoch().count()) + "");
    ExpectTTL("k0", 1s);
    pxat = clock_.Now() - 1000ms;
    // PXAT now -> noop.
    Invoke("SET k0 v0 PXAT " + std::to_string(pxat.time_since_epoch().count()) + "");
    ExpectNoTTL("k0");

    // Zero-out milliseconds to make EXAT do not lose precision.
    clock_.SetTime(Clock::TimePoint{2000s});
    ExpectNoTTL("k1");
    // EX.
    Invoke("SET k1 v0 EX 100");
    ExpectTTL("k1", 100s);
    // EX update expire time.
    Invoke("SET k1 v0 EX 2000");
    ExpectTTL("k1", 2000s);
    // SET invalidates previous expire.
    Invoke("SET k1 v0");
    ExpectNoTTL("k1");
    // EXAT.
    auto exat = clock_.Now() + 1000s;
    Invoke("SET k1 v0 EXAT " + std::to_string(exat.time_since_epoch().count() / 1000) + "");
    ExpectTTL("k1", 1000s);
    // EXAT now -> noop.
    exat = clock_.Now() - 1000s;
    Invoke("SET k1 v0 EXAT " + std::to_string(exat.time_since_epoch().count() / 1000) + "");
    ExpectNoTTL("k1");

    // TTL reduces as time goes by.
    clock_.SetTime(Clock::TimePoint{2000s});
    Invoke("SET k0 v0 PX 100");
    ExpectTTL("k0", 100ms);
    AdvanceTime(50ms);
    ExpectTTL("k0", 50ms);
    AdvanceTime(49ms);
    ExpectTTL("k0", 1ms);
    AdvanceTime(1ms);
    ExpectNoTTL("k0");

    Invoke("SET k0 v0");
    // KEEPTTL on normal key does nothing.
    Invoke("SET k0 v0 KEEPTTL");
    ExpectNoTTL("k0");
    Invoke("SET k0 v0 EX 100");
    ExpectTTL("k0", 100s);
    Invoke("SET k0 v1 KEEPTTL");
    ExpectTTL("k0", 100s);

    // GET
    Invoke("SET k0 v0");
    auto res = Invoke("SET k0 v1 GET");
    ExpectString(res, "v0");
    Invoke("SET k0 v0 PX 100");
    res = Invoke("SET k0 v2 GET PX 100");
    ExpectString(res, "v0");
    AdvanceTime(100ms);
    // GET on expired key returns null.
    res = Invoke("SET k0 v3 GET");
    ExpectNull(res);

    // SET NX on expired key should success.
    Invoke("SET k0 v0 EX 1");
    Invoke("SET k0 v1 NX");
    ExpectKeyValue("k0", "v0");
    AdvanceTime(1s);
    Invoke("SET k0 v1 NX");
    ExpectKeyValue("k0", "v1");

    // SET XX on expired key should fail.
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    res = Invoke("SET k0 v1 XX");
    ExpectNull(res);
    ExpectNoKey("k0");
}

TEST_F(StringCommandsTest, SetEXTest) {
    Invoke("SETEX k0 10 v0");
    ExpectKeyValue("k0", "v0");
    ExpectTTL("k0", 10s);
    Invoke("SETEX k0 1000 v0");
    ExpectTTL("k0", 1000s);

    Invoke("SETEX k1 0 v0");
    ExpectNoKey("k1");
    Invoke("SETEX k1 invalid v0");
    ExpectNoKey("k1");
    Invoke("SETEX k1 v0");
    ExpectNoKey("k1");

    Invoke("PSETEX k0 10 v0");
    ExpectKeyValue("k0", "v0");
    ExpectTTL("k0", 10ms);
    Invoke("PSETEX k0 1000 v0");
    ExpectTTL("k0", 1000ms);

    Invoke("PSETEX k1 0 v0");
    ExpectNoKey("k1");
    Invoke("PSETEX k1 invalid v0");
    ExpectNoKey("k1");
    Invoke("PSETEX k1 v0");
    ExpectNoKey("k1");
}

TEST_F(StringCommandsTest, SetNXTest) {
    // SETNX on no existing -> insert
    auto res = Invoke("SETNX k0 v0");
    ExpectInt(res, 1);
    ExpectKeyValue("k0", "v0");

    // SETNX on existing -> noop
    res = Invoke("SETNX k0 v1");
    ExpectInt(res, 0);
    ExpectKeyValue("k0", "v0");

    // SETNX on expired -> insert
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    res = Invoke("SETNX k0 v1");
    ExpectInt(res, 1);
    ExpectKeyValue("k0", "v1");
}

TEST_F(StringCommandsTest, MSetTest) {
    // MSET should succeed and invalidate expiration no matter key exists / is volatile or not.
    Invoke("SET k1 v0");
    Invoke("SET k2 v0 EX 1000");
    ExpectString(Invoke("MSET k0 v1 k1 v1 k2 v1"), "OK");
    ExpectKeyValue("k0", "v1");
    ExpectKeyValue("k1", "v1");
    ExpectKeyValue("k2", "v1");
    ExpectNoTTL("k2");
}

TEST_F(StringCommandsTest, MSetNXTest) {
    ExpectInt(Invoke("MSETNX k0 v0 k1 v1 k2 v2"), 1);
    ExpectKeyValue("k0", "v0");
    ExpectKeyValue("k1", "v1");
    ExpectKeyValue("k2", "v2");

    ExpectInt(Invoke("MSETNX k0 v1 k1 v2 k2 v3"), 0);
    ExpectKeyValue("k0", "v0");
    ExpectKeyValue("k1", "v1");
    ExpectKeyValue("k2", "v2");

    ExpectInt(Invoke("MSETNX k0 v1 k1 v2 k2 v3 k3 v4"), 1);
    ExpectKeyValue("k3", "v4");

    Invoke("SET k4 v4 EX 1");
    AdvanceTime(1s);
    ExpectInt(Invoke("MSETNX k4 v5"), 1);
    ExpectKeyValue("k4", "v5");
    ExpectNoTTL("k4");
}

TEST_F(StringCommandsTest, GetTest) {
    Result result;
    // key no exist -> nil
    result = Invoke("GET non-existing-key");
    ExpectNull(result);

    // key exist
    //      valid   -> value, update lru
    Invoke("SET k0 v0");
    result = Invoke("GET k0");
    ASSERT_EQ(result.Size(), 1);
    EXPECT_EQ(result.data[0], "v0");
    // TODO: LRU

    Invoke("SET k0 v0 EX 10");
    result = Invoke("GET k0");
    ExpectString(result, "v0");

    //      invalid -> nil, erase data/expire
    AdvanceTime(10s);
    result = Invoke("GET k0");
    ExpectNull(result);
    EXPECT_EQ(service_.DataHashTable()->Find("k0"), nullptr);
    EXPECT_EQ(service_.GetExpireHashTable()->Find("k0"), nullptr);
}

TEST_F(StringCommandsTest, GetDelTest) {
    // GETDEL on not existing -> nil
    ExpectNull(Invoke("GETDEL k0"));

    // GETDEL on expired -> nil
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    ExpectNull(Invoke("GETDEL k0"));

    // GETDEL on valid -> return and delete
    Invoke("SET k0 v0");
    ExpectString(Invoke("GETDEL k0"), "v0");
    ExpectNoKey("k0");

    // GETDEL on volatile valid -> return and delete data / expire
    Invoke("SET k0 v0 EX 1");
    ExpectString(Invoke("GETDEL k0"), "v0");
    ExpectNoKey("k0");
}

TEST_F(StringCommandsTest, GetEXTest) {
    // GETEX on not existing -> nil
    ExpectNull(Invoke("GETEX k0"));

    // Also for PERSIST / EX ...
    ExpectNull(Invoke("GETEX k0 PERSIST"));
    ExpectNull(Invoke("GETEX k0 EX 10"));
    ExpectNull(Invoke("GETEX k0 PX 10"));
    ExpectNull(Invoke("GETEX k0 EXAT 10"));
    ExpectNull(Invoke("GETEX k0 PXAT 10"));

    // Clean GETEX is same as GET for valid / expired keys.
    Invoke("SET k0 v0");
    ExpectString(Invoke("GETEX k0"), "v0");
    ExpectNoTTL("k0");

    Invoke("SET k0 v1 EX 10");
    ExpectString(Invoke("GETEX k0"), "v1");
    ExpectTTL("k0", 10s);

    // For expired keys, returns null.
    AdvanceTime(10s);
    ExpectNull(Invoke("GETEX k0 PERSIST"));
    ExpectNull(Invoke("GETEX k0 EX 10"));
    ExpectNull(Invoke("GETEX k0 PX 10"));
    ExpectNull(Invoke("GETEX k0 EXAT 10"));
    ExpectNull(Invoke("GETEX k0 PXAT 10"));
    ExpectNoKey("k0");

    // GETEX with EX/PX/EXAT/PXAT changes expire time
    clock_.SetTime(Clock::TimePoint{2000s});
    Invoke("SET k0 v0");
    ExpectString(Invoke("GETEX k0 EX 10"), "v0");
    ExpectTTL("k0", 10s);
    ExpectString(Invoke("GETEX k0 PX 10"), "v0");
    ExpectTTL("k0", 10ms);
    ExpectString(Invoke("GETEX k0 EXAT 3000"), "v0");
    ExpectTTL("k0", 1000s);
    ExpectString(Invoke("GETEX k0 PXAT 2100000"), "v0");
    ExpectTTL("k0", 100s);

    // GETEX with PERSIST cleans TTL
    ExpectString(Invoke("GETEX k0 PERSIST"), "v0");
    ExpectNoTTL("k0");
}

TEST_F(StringCommandsTest, GetSetTest) {
    // GETSET on not existing -> set key, returns null
    ExpectNull(Invoke("GETSET k0 v0"));
    ExpectKeyValue("k0", "v0");

    // GETSET on existing nonvolatile -> set key, returns old value
    Invoke("SET k0 v0");
    ExpectString(Invoke("GETSET k0 v1"), "v0");
    ExpectKeyValue("k0", "v1");

    // GETSET on existing and valid -> set key, invalidate expire, returns old value
    Invoke("SET k0 v0 EX 1");
    ExpectString(Invoke("GETSET k0 v1"), "v0");
    ExpectKeyValue("k0", "v1");
    ExpectNoTTL("k0");

    // GETSET on existing and expired -> set key, returns null
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    ExpectNull(Invoke("GETSET k0 v1"));
    ExpectKeyValue("k0", "v1");
    ExpectNoTTL("k0");
}

TEST_F(StringCommandsTest, GetRangeTest) {
    ExpectString(Invoke("GETRANGE k 0 2"), "");

    Invoke("SET k abcdefghijklmn");
    ExpectString(Invoke("GETRANGE k 0 4"), "abcde");
    ExpectString(Invoke("GETRANGE k 0 -1"), "abcdefghijklmn");
    ExpectString(Invoke("GETRANGE k 3 7"), "defgh");
    ExpectString(Invoke("GETRANGE k -4 -1"), "klmn");
    ExpectString(Invoke("GETRANGE k -4 13"), "klmn");

    ExpectString(Invoke("GETRANGE k 1000 0"), "");
    ExpectString(Invoke("GETRANGE k 0 -20"), "");
}

TEST_F(StringCommandsTest, AppendTest) {
    // APPEND non-exist key should create
    ExpectInt(Invoke("APPEND k0 foobar"), 6);
    ExpectKeyValue("k0", "foobar");

    // regular APPEND
    ExpectInt(Invoke("APPEND k0 barfoo"), 12);
    ExpectKeyValue("k0", "foobarbarfoo");

    // APPEND should not modify TTL
    Invoke("SET k0 v0 EX 1");
    ExpectInt(Invoke("APPEND k0 foobar"), 8);
    ExpectTTL("k0", 1s);
}

} // namespace rdss::test
