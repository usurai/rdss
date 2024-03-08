#include "service/commands/string_commands.h"
#include "service/data_structure_service.h"
#include "util.h"

constexpr size_t kKeys = 10'000'000;
const std::string key_prefix = "memtier-";
std::vector<std::string> keys;
std::unique_ptr<DataStructureService> service;
const std::string set_str = "SET";
std::string value(32, 'x');
Clock sys_clock(true);
Config config{};
std::vector<std::vector<std::string_view>> commands;

static void SetBenchSetup(const benchmark::State& s) {
    if (keys.empty()) {
        GenerateRandomKeys(keys, kKeys, key_prefix, std::to_string(kKeys).size());
    }
    value = std::string(s.range(4), 'x');
    if (commands.size() < s.range(1)) {
        commands.resize(s.range(1), std::vector<std::string_view>{set_str, set_str, value});
    }

    service = std::make_unique<DataStructureService>(&config, nullptr, &sys_clock);
    RegisterStringCommands(service.get());
}

struct SetTask {
    static void Set(size_t num_shards, size_t shard_index, size_t cnt) {
        const auto key_index = (keys.size() / num_shards * shard_index + cnt) % keys.size();
        commands[shard_index][1] = keys[key_index];
        Result result;
        service->Invoke(commands[shard_index], result);
        assert(result.type == Result::Type::kOk);
    }

    template<typename... Args>
    Task<void> operator()(Args&&... args) {
        return ShardTask(Set, std::forward<Args>(args)...);
    }
};

struct EmptyTask {
    static void Nop(size_t, size_t, size_t) {}

    template<typename... Args>
    Task<void> operator()(Args&&... args) {
        return ShardTask(Nop, std::forward<Args>(args)...);
    }
};

static void SetBenchTeardown(const benchmark::State& state) {
    assert(service->Stats().commands_processed >= state.range(0) * state.range(1));
}

BENCHMARK(BenchSharded<EmptyTask>)
  ->UseManualTime()
  ->ArgsProduct({
    {1},                       // wait batch
    {2, 4, 8, 10, 12, 16, 24}, // num of client executor
    {20, 100},                 // connections per client
    {4'000'000},               // num of op per connection
    {8},                       // value size
    {0},                       // sqpoll
  })
  ->ArgNames({"wait_batch:", "cli_exrs", "conns_per_cli", "op_per_conn", "val_size", "sqpoll"});

BENCHMARK(BenchSharded<SetTask>)
  ->Name("specific")
  ->UseManualTime()
  ->Setup(SetBenchSetup)
  ->Teardown(SetBenchTeardown)
  ->ArgsProduct({
    {1},       // wait batch
    {8},       // num of client executor
    {20},      // connections per client
    {400'000}, // num of op per connection
    {8},       // value size
    {0},       // sqpoll
  })
  ->ArgNames({"wait_batch:", "cli_exrs", "conns_per_cli", "op_per_conn", "val_size", "sqpoll"});

BENCHMARK(BenchSharded<SetTask>)
  ->UseManualTime()
  ->Setup(SetBenchSetup)
  ->Teardown(SetBenchTeardown)
  ->ArgsProduct({
    {1},                       // wait batch
    {2, 4, 8, 10, 12, 16, 24}, // num of client executor
    {20, 100},                 // connections per client
    {4'000'000},               // num of op per connection
    {8},                       // value size
    {0},                       // sqpoll
  })
  ->ArgNames({"wait_batch:", "cli_exrs", "conns_per_cli", "op_per_conn", "val_size", "sqpoll"});

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
