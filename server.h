#pragma once

#include "async_operation_processor.h"
#include "buffer.h"
#include "client.h"
#include "listener.h"
#include "proactor.h"
#include "promise.h"

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
    Server() {
        processor_ = AsyncOperationProcessor::Create();
        assert(processor_ != nullptr);
        listener_ = Listener::Create(6379, processor_.get());
        assert(listener_ != nullptr);
        proactor_ = std::make_unique<Proactor>(processor_->GetRing());
    }

    void Run() {
        AcceptLoop();
        proactor_->Run();
    }

private:
    Task<void> AcceptLoop() {
        while (true) {
            auto conn = co_await listener_->Accept(/*cancel_token*/);
            auto client = new Client(conn);
            client->Echo();
        }
    }

    void Shutdown();

    std::unique_ptr<AsyncOperationProcessor> processor_;
    std::unique_ptr<Listener> listener_;
    std::unique_ptr<Proactor> proactor_;
};

} // namespace rdss
