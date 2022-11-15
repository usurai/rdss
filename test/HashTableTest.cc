#include "HashTable.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <unordered_map>

namespace rdss::test {

class HashTableTest : public testing::Test {
public:
    void SetUp() override { std::srand(std::time(nullptr)); }

    static std::string GenRandomString(size_t len) {
        static const char alphanum[] = "0123456789"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz";
        std::string result;
        result.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            result += alphanum[std::rand() % (sizeof(alphanum) - 1)];
        }
        return result;
    }
};

TEST(HashTableTest, basic) {
    HashTable<std::string, std::string> hash_table;
    EXPECT_EQ(hash_table.Count(), 0);

    constexpr size_t key_length = 64;
    constexpr size_t value_length = 512;
    constexpr size_t n = 1024 * 16;

    // Insert / erase random key value pair against the hash table.
    std::unordered_map<std::string, std::string> fact;
    for (size_t i = 0; i < n; ++i) {
        if (fact.empty() || static_cast<double>(std::rand()) / RAND_MAX > 0.2) {
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
        } else {
            hash_table.Erase(fact.begin()->first);
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

} // namespace rdss::test
