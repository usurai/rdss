#pragma once

#include "async_operation_processor.h"
#include "buffer.h"
#include "client.h"
#include "listener.h"
#include "proactor.h"
#include "promise.h"
#include "data_structure_service.h"

#include <cassert>
#include <coroutine>
#include <memory>

namespace rdss {
static constexpr size_t QD = 1024;
static constexpr size_t READ_SIZE = 1024 * 16;
static constexpr size_t READ_THRESHOLD = 1024 * 32;

// TODO: Make these flags.
static constexpr bool SQ_POLL{true};
static constexpr bool DRAIN_CQ{false};
static constexpr bool SQE_ASYNC = false;
} // namespace rdss

namespace rdss {

class Server {
public:
    Server();

    void Run();

    // TODO
    // void Shutdown();

private:
    Task<void> AcceptLoop();

    std::unique_ptr<AsyncOperationProcessor> processor_;
    std::unique_ptr<Listener> listener_;
    std::unique_ptr<Proactor> proactor_;
    std::unique_ptr<DataStructureService> service_;
};

} // namespace rdss
