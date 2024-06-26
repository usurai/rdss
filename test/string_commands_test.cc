#include "commands_test_base.h"
#include "service/commands/string_commands.h"

namespace rdss::test {

using namespace std::chrono;

class StringCommandsTest : public CommandsTestBase {
protected:
    void SetUp() override {
        CommandsTestBase::SetUp();
        RegisterStringCommands(&service_);
    }
};

TEST_F(StringCommandsTest, SetTest) {
    // Insert, then update.
    Invoke("SET k0 v0");
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));
    Invoke("SET k0 v1");
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));

    // NX not exists -> insert.
    Invoke("SET k0 v2 NX");
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));
    // NX exists -> noop.
    Invoke("SET k1 v0 NX");
    EXPECT_TRUE(ExpectKeyValue("k1", "v0"));

    // XX not exists -> insert.
    Invoke("SET k2 v0 XX");
    EXPECT_TRUE(ExpectNoKey("k2"));
    // XX exists -> update.
    Invoke("SET k1 v1 XX");
    EXPECT_TRUE(ExpectKeyValue("k1", "v1"));

    // PX no exists.
    EXPECT_TRUE(ExpectNoTTL("k0"));
    Invoke("SET k0 v0 PX 100");
    // EXPECT_TRUE(ExpectTTL("k0", 100ms));
    EXPECT_EQ(GetTTL("k0"), 100ms);
    // PX update expire time.
    Invoke("SET k0 v0 PX 2000");
    EXPECT_TRUE(ExpectTTL("k0", 2s));
    // SET invalidates previous expire.
    Invoke("SET k0 v0");
    EXPECT_TRUE(ExpectNoTTL("k0"));
    auto pxat = clock_.Now() + 1000ms;
    // PXAT
    Invoke("SET k0 v0 PXAT " + std::to_string(pxat.time_since_epoch().count()) + "");
    EXPECT_TRUE(ExpectTTL("k0", 1s));
    pxat = clock_.Now() - 1000ms;
    // PXAT now -> noop.
    Invoke("SET k0 v0 PXAT " + std::to_string(pxat.time_since_epoch().count()) + "");
    EXPECT_TRUE(ExpectNoTTL("k0"));

    // Zero-out milliseconds to make EXAT do not lose precision.
    SetTime(Clock::TimePoint{2000s});
    EXPECT_TRUE(ExpectNoTTL("k1"));
    // EX.
    Invoke("SET k1 v0 EX 100");
    EXPECT_TRUE(ExpectTTL("k1", 100s));
    // EX update expire time.
    Invoke("SET k1 v0 EX 2000");
    EXPECT_TRUE(ExpectTTL("k1", 2000s));
    // SET invalidates previous expire.
    Invoke("SET k1 v0");
    EXPECT_TRUE(ExpectNoTTL("k1"));
    // EXAT.
    auto exat = clock_.Now() + 1000s;
    Invoke("SET k1 v0 EXAT " + std::to_string(exat.time_since_epoch().count() / 1000) + "");
    EXPECT_TRUE(ExpectTTL("k1", 1000s));
    // EXAT now -> noop.
    exat = clock_.Now() - 1000s;
    Invoke("SET k1 v0 EXAT " + std::to_string(exat.time_since_epoch().count() / 1000) + "");
    EXPECT_TRUE(ExpectNoTTL("k1"));

    // TTL reduces as time goes by.
    SetTime(Clock::TimePoint{2000s});
    Invoke("SET k0 v0 PX 100");
    EXPECT_TRUE(ExpectTTL("k0", 100ms));
    AdvanceTime(50ms);
    EXPECT_TRUE(ExpectTTL("k0", 50ms));
    AdvanceTime(49ms);
    EXPECT_TRUE(ExpectTTL("k0", 1ms));
    AdvanceTime(1ms);
    EXPECT_TRUE(ExpectNoTTL("k0"));

    Invoke("SET k0 v0");
    // KEEPTTL on normal key does nothing.
    Invoke("SET k0 v0 KEEPTTL");
    EXPECT_TRUE(ExpectNoTTL("k0"));
    Invoke("SET k0 v0 EX 100");
    EXPECT_TRUE(ExpectTTL("k0", 100s));
    Invoke("SET k0 v1 KEEPTTL");
    EXPECT_TRUE(ExpectTTL("k0", 100s));

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
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));
    AdvanceTime(1s);
    Invoke("SET k0 v1 NX");
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));

    // SET XX on expired key should fail.
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    res = Invoke("SET k0 v1 XX");
    ExpectNull(res);
    EXPECT_TRUE(ExpectNoKey("k0"));
}

TEST_F(StringCommandsTest, SetEXTest) {
    Invoke("SETEX k0 10 v0");
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));
    EXPECT_TRUE(ExpectTTL("k0", 10s));
    Invoke("SETEX k0 1000 v0");
    EXPECT_TRUE(ExpectTTL("k0", 1000s));

    Invoke("SETEX k1 0 v0");
    EXPECT_TRUE(ExpectNoKey("k1"));
    Invoke("SETEX k1 invalid v0");
    EXPECT_TRUE(ExpectNoKey("k1"));
    Invoke("SETEX k1 v0");
    EXPECT_TRUE(ExpectNoKey("k1"));

    Invoke("PSETEX k0 10 v0");
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));
    EXPECT_TRUE(ExpectTTL("k0", 10ms));
    Invoke("PSETEX k0 1000 v0");
    EXPECT_TRUE(ExpectTTL("k0", 1000ms));

    Invoke("PSETEX k1 0 v0");
    EXPECT_TRUE(ExpectNoKey("k1"));
    Invoke("PSETEX k1 invalid v0");
    EXPECT_TRUE(ExpectNoKey("k1"));
    Invoke("PSETEX k1 v0");
    EXPECT_TRUE(ExpectNoKey("k1"));
}

TEST_F(StringCommandsTest, SetNXTest) {
    // SETNX on no existing -> insert
    auto res = Invoke("SETNX k0 v0");
    ExpectInt(res, 1);
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));

    // SETNX on existing -> noop
    res = Invoke("SETNX k0 v1");
    ExpectInt(res, 0);
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));

    // SETNX on expired -> insert
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    res = Invoke("SETNX k0 v1");
    ExpectInt(res, 1);
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));
}

TEST_F(StringCommandsTest, SetRangeTest) {
    // SETRANGE on non-existing key creates it
    ExpectInt(Invoke("SETRANGE k 0 foobar"), 6);
    EXPECT_TRUE(ExpectKeyValue("k", "foobar"));

    // SETRANGE at the end appends
    ExpectInt(Invoke("SETRANGE k 6 foobar"), 12);
    EXPECT_TRUE(ExpectKeyValue("k", "foobarfoobar"));

    // SETRANGE at the middle replaces
    ExpectInt(Invoke("SETRANGE k 3 foobar"), 9);
    EXPECT_TRUE(ExpectKeyValue("k", "foofoobar"));

    // zero-padding
    ExpectInt(Invoke("SETRANGE k 12 foobar"), 18);
    std::string expected{std::string{"foofoobar"} + std::string(3, 0) + std::string{"foobar"}};
    EXPECT_TRUE(ExpectKeyValue("k", expected));
}

TEST_F(StringCommandsTest, MSetTest) {
    // MSET should succeed and invalidate expiration no matter key exists / is volatile or not.
    Invoke("SET k1 v0");
    Invoke("SET k2 v0 EX 1000");
    ExpectOk(Invoke("MSET k0 v1 k1 v1 k2 v1"));
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));
    EXPECT_TRUE(ExpectKeyValue("k1", "v1"));
    EXPECT_TRUE(ExpectKeyValue("k2", "v1"));
    EXPECT_TRUE(ExpectNoTTL("k2"));
}

TEST_F(StringCommandsTest, MSetNXTest) {
    ExpectInt(Invoke("MSETNX k0 v0 k1 v1 k2 v2"), 1);
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));
    EXPECT_TRUE(ExpectKeyValue("k1", "v1"));
    EXPECT_TRUE(ExpectKeyValue("k2", "v2"));

    ExpectInt(Invoke("MSETNX k0 v1 k1 v2 k2 v3"), 0);
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));
    EXPECT_TRUE(ExpectKeyValue("k1", "v1"));
    EXPECT_TRUE(ExpectKeyValue("k2", "v2"));

    ExpectInt(Invoke("MSETNX k0 v1 k1 v2 k2 v3 k3 v4"), 1);
    EXPECT_TRUE(ExpectKeyValue("k3", "v4"));

    Invoke("SET k4 v4 EX 1");
    AdvanceTime(1s);
    ExpectInt(Invoke("MSETNX k4 v5"), 1);
    EXPECT_TRUE(ExpectKeyValue("k4", "v5"));
    EXPECT_TRUE(ExpectNoTTL("k4"));
}

TEST_F(StringCommandsTest, GetTest) {
    Result result;
    // key no exist -> nil
    result = Invoke("GET non-existing-key");
    ExpectNull(result);

    // key exist
    //      valid   -> value, update lru
    Invoke("SET k0 v0");
    ExpectString(Invoke("GET k0"), "v0");

    Invoke("SET k0 v0 EX 10");
    result = Invoke("GET k0");
    ExpectString(result, "v0");

    //      invalid -> nil, erase data/expire
    AdvanceTime(10s);
    result = Invoke("GET k0");
    ExpectNull(result);
    EXPECT_EQ(service_.DataTable()->Find("k0"), nullptr);
    EXPECT_EQ(service_.ExpireTable()->Find("k0"), nullptr);
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
    EXPECT_TRUE(ExpectNoKey("k0"));

    // GETDEL on volatile valid -> return and delete data / expire
    Invoke("SET k0 v0 EX 1");
    ExpectString(Invoke("GETDEL k0"), "v0");
    EXPECT_TRUE(ExpectNoKey("k0"));
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
    EXPECT_TRUE(ExpectNoTTL("k0"));

    Invoke("SET k0 v1 EX 10");
    ExpectString(Invoke("GETEX k0"), "v1");
    EXPECT_TRUE(ExpectTTL("k0", 10s));

    // For expired keys, returns null.
    AdvanceTime(10s);
    ExpectNull(Invoke("GETEX k0 PERSIST"));
    ExpectNull(Invoke("GETEX k0 EX 10"));
    ExpectNull(Invoke("GETEX k0 PX 10"));
    ExpectNull(Invoke("GETEX k0 EXAT 10"));
    ExpectNull(Invoke("GETEX k0 PXAT 10"));
    EXPECT_TRUE(ExpectNoKey("k0"));

    // GETEX with EX/PX/EXAT/PXAT changes expire time
    SetTime(Clock::TimePoint{2000s});
    Invoke("SET k0 v0");
    ExpectString(Invoke("GETEX k0 EX 10"), "v0");
    EXPECT_TRUE(ExpectTTL("k0", 10s));
    ExpectString(Invoke("GETEX k0 PX 10"), "v0");
    EXPECT_TRUE(ExpectTTL("k0", 10ms));
    ExpectString(Invoke("GETEX k0 EXAT 3000"), "v0");
    EXPECT_TRUE(ExpectTTL("k0", 1000s));
    ExpectString(Invoke("GETEX k0 PXAT 2100000"), "v0");
    EXPECT_TRUE(ExpectTTL("k0", 100s));

    // GETEX with PERSIST cleans TTL
    ExpectString(Invoke("GETEX k0 PERSIST"), "v0");
    EXPECT_TRUE(ExpectNoTTL("k0"));
}

TEST_F(StringCommandsTest, GetSetTest) {
    // GETSET on not existing -> set key, returns null
    ExpectNull(Invoke("GETSET k0 v0"));
    EXPECT_TRUE(ExpectKeyValue("k0", "v0"));

    // GETSET on existing nonvolatile -> set key, returns old value
    Invoke("SET k0 v0");
    ExpectString(Invoke("GETSET k0 v1"), "v0");
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));

    // GETSET on existing and valid -> set key, invalidate expire, returns old value
    Invoke("SET k0 v0 EX 1");
    ExpectString(Invoke("GETSET k0 v1"), "v0");
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));
    EXPECT_TRUE(ExpectNoTTL("k0"));

    // GETSET on existing and expired -> set key, returns null
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    ExpectNull(Invoke("GETSET k0 v1"));
    EXPECT_TRUE(ExpectKeyValue("k0", "v1"));
    EXPECT_TRUE(ExpectNoTTL("k0"));
}

TEST_F(StringCommandsTest, MGetTest) {
    Invoke("MSET k0 xx k1 xxxxxx k2 xxxxxxxxxxxxxxx");
    ExpectStrings(Invoke("MGET k0"), {"xx"});
    ExpectStrings(Invoke("MGET k0 k1 k2"), {"xx", "xxxxxx", "xxxxxxxxxxxxxxx"});
    ExpectStrings(Invoke("MGET k0 k1 k3 k2"), {"xx", "xxxxxx", "", "xxxxxxxxxxxxxxx"});
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(1s);
    ExpectStrings(Invoke("MGET k0 k1 k2"), {"", "xxxxxx", "xxxxxxxxxxxxxxx"});
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
    EXPECT_TRUE(ExpectKeyValue("k0", "foobar"));

    // regular APPEND
    ExpectInt(Invoke("APPEND k0 barfoo"), 12);
    EXPECT_TRUE(ExpectKeyValue("k0", "foobarbarfoo"));

    // APPEND should not modify TTL
    Invoke("SET k0 v0 EX 1");
    ExpectInt(Invoke("APPEND k0 foobar"), 8);
    EXPECT_TRUE(ExpectTTL("k0", 1s));
}

TEST_F(StringCommandsTest, StrlenTest) {
    ExpectInt(Invoke("STRLEN k"), 0);
    Invoke("SET k foobar");
    ExpectInt(Invoke("STRLEN k"), 6);
    Invoke("SET k foobar EX 1");
    AdvanceTime(1s);
    ExpectInt(Invoke("STRLEN k"), 0);
}

} // namespace rdss::test
