#include "data_structure/tracking_hash_table.h"
#include "util.h"

#include <benchmark/benchmark.h>

using namespace rdss;

class HashTableBenchmark : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State&) {
        GenerateRandomKeys(keys, key_max, key_prefix, key_digits);
    }

    const size_t key_max = 10'000'000;
    const size_t key_digits = std::to_string(key_max - 1).size();
    const std::string key_prefix = "memtier-";
    std::string value = std::string(32, 'x');

    std::vector<std::string> keys;
};

BENCHMARK_F(HashTableBenchmark, InsertAllUnorderedMap)(benchmark::State& s) {
    for (auto _ : s) {
        std::unordered_map<std::string, std::string> m;
        for (const auto& key : keys) {
            m.insert({key, value});
        }
    }
}

BENCHMARK_F(HashTableBenchmark, InsertAllCreateSharedPtr)(benchmark::State& s) {
    for (auto _ : s) {
        MTSHashTable ht;
        for (const auto& key : keys) {
            ht.Insert(key, CreateMTSPtr(value));
        }
    }
}

BENCHMARK_F(HashTableBenchmark, InsertAllCopySharedPtr)(benchmark::State& s) {
    for (auto _ : s) {
        MTSHashTable ht;
        auto value_ptr = CreateMTSPtr(value);
        for (const auto& key : keys) {
            ht.Insert(key, value_ptr);
        }
    }
}

BENCHMARK_DEFINE_F(HashTableBenchmark, FindAllUnorderedMap)(benchmark::State& s) {
    for (auto _ : s) {
        std::unordered_map<std::string, std::string> m;
        for (const auto& key : keys) {
            m.insert({key, value});
        }

        const auto now = std::chrono::steady_clock::now();
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(m.at(key));
        }
        const auto elapsed = std::chrono::steady_clock::now() - now;
        s.SetIterationTime(
          std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count());
    }
}

BENCHMARK_DEFINE_F(HashTableBenchmark, FindAll)(benchmark::State& s) {
    for (auto _ : s) {
        MTSHashTable ht;
        for (const auto& key : keys) {
            ht.Insert(key, CreateMTSPtr(value));
        }

        const auto now = std::chrono::steady_clock::now();
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(ht.Find(key));
        }
        const auto elapsed = std::chrono::steady_clock::now() - now;
        s.SetIterationTime(
          std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count());
    }
}

BENCHMARK_REGISTER_F(HashTableBenchmark, FindAllUnorderedMap)->UseManualTime();

BENCHMARK_REGISTER_F(HashTableBenchmark, FindAll)->UseManualTime();

BENCHMARK_F(HashTableBenchmark, CreateSharedPtr)(benchmark::State& s) {
    for (auto _ : s) {
        const auto size = keys.size();
        for (size_t i = 0; i < size; ++i) {
            benchmark::DoNotOptimize(CreateMTSPtr(value));
        }
    }
}

BENCHMARK_F(HashTableBenchmark, CopySharedPtr)(benchmark::State& s) {
    for (auto _ : s) {
        auto value_ptr = CreateMTSPtr(value);
        const auto size = keys.size();
        for (size_t i = 0; i < size; ++i) {
            MTSPtr copy;
            benchmark::DoNotOptimize(copy = value_ptr);
        }
    }
}

BENCHMARK_MAIN();
