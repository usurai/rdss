#include "HashTable.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <unordered_map>

namespace rdss::test {

using TrackingString = std::basic_string<char, std::char_traits<char>, Mallocator<char>>;

class HashTableTest : public testing::Test {
public:
    void SetUp() override { std::srand(std::time(nullptr)); }

    static TrackingString GenRandomString(size_t len) {
        static const char alphanum[] = "0123456789"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz";
        TrackingString result;
        result.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            result += alphanum[std::rand() % (sizeof(alphanum) - 1)];
        }
        return result;
    }
};

TEST(HashTableTest, basic) {
    HashTable<TrackingString, TrackingString> hash_table;
    EXPECT_EQ(hash_table.Count(), 0);

    constexpr size_t key_length = 64;
    constexpr size_t value_length = 512;
    constexpr size_t n = 1024 * 16;

    // Insert / assign / erase random key value pair against the hash table.
    std::map<TrackingString, TrackingString> fact;
    for (size_t i = 0; i < n; ++i) {
        const auto r = static_cast<double>(std::rand()) / RAND_MAX;
        if (fact.empty() || r > 0.5) {
            auto key = HashTableTest::GenRandomString(key_length);
            while (fact.contains(key)) {
                key = HashTableTest::GenRandomString(key_length);
            }
            auto value = HashTableTest::GenRandomString(value_length);
            fact.insert({key, value});

            EXPECT_EQ(hash_table.Find(key), nullptr);
            auto [entry, inserted] = hash_table.Insert(key, value);
            EXPECT_NE(entry, nullptr);
            EXPECT_TRUE(inserted);

            auto find_result = hash_table.Find(key);
            EXPECT_NE(find_result, nullptr);
            EXPECT_EQ(find_result->key, key);
            EXPECT_EQ(find_result->value, value);
        } else if (r > 0.2) {
            auto it = fact.begin();
            auto value = HashTableTest::GenRandomString(value_length);
            EXPECT_NE(hash_table.Find(it->first), nullptr);
            it->second = value;
            auto [entry, replaced] = hash_table.InsertOrAssign(it->first, it->second);
            EXPECT_NE(entry, nullptr);
            EXPECT_TRUE(replaced);

            auto find_result = hash_table.Find(it->first);
            EXPECT_NE(find_result, nullptr);
            EXPECT_EQ(find_result->key, it->first);
            EXPECT_EQ(find_result->value, it->second);
        } else {
            EXPECT_TRUE(hash_table.Erase(fact.begin()->first));
            auto find_result = hash_table.Find(fact.begin()->first);
            EXPECT_EQ(find_result, nullptr);
            fact.erase(fact.begin());
        }
    }
    EXPECT_EQ(hash_table.Count(), fact.size());

    for (const auto& [key, value] : fact) {
        auto find_result = hash_table.Find(key);
        EXPECT_NE(find_result, nullptr);
        EXPECT_EQ(find_result->key, key);
        EXPECT_EQ(find_result->value, value);
    }
}

// TODO: find a way to test randomness
TEST(HashTableTest, getRandomEntry) {
    constexpr size_t key_length = 64;
    constexpr size_t value_length = 512;
    constexpr size_t n = 1024 * 16;

    HashTable<TrackingString, TrackingString> hash_table;
    for (size_t i = 0; i < n; ++i) {
        auto key = HashTableTest::GenRandomString(key_length);
        auto value = HashTableTest::GenRandomString(value_length);
        hash_table.Insert(key, value);
    }

    std::map<TrackingString, size_t> count;
    for (size_t i = 0; i < n; ++i) {
        auto entry = hash_table.GetRandomEntry();
        EXPECT_NE(entry, nullptr);
        ++count[entry->key];
    }

    size_t max_count{0};
    for (const auto& [_, c] : count) {
        max_count = std::max(max_count, c);
    }
    EXPECT_LE(max_count, 16);
}

} // namespace rdss::test
