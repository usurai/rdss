#include "io/listener.h"
#include "io/promise.h"
#include "runtime/util.h"
#include "sys/util.h"

#include <glog/logging.h>

#include <future>

using namespace rdss;

Task<void> Echo(RingExecutor* src_exr, Connection* conn_ptr, std::atomic<size_t>& connections) {
    if (src_exr != conn_ptr->GetExecutor()) {
        co_await Transfer(src_exr, conn_ptr->GetExecutor());
    }

    std::unique_ptr<Connection> conn(conn_ptr);
    conn->TryRegisterFD();

    std::array<char, 4096> buffer;
    std::error_code error;
    size_t bytes_read, bytes_written;
    while (true) {
        std::tie(error, bytes_read) = co_await conn->Recv(buffer);
        if (error) {
            LOG(ERROR) << "recv: " << error.message();
            break;
        }
        if (bytes_read == 0) {
            break;
        }

        std::tie(error, bytes_written) = co_await conn->Send(
          std::string_view(buffer.data(), bytes_read));
        if (error) {
            LOG(ERROR) << "send: " << error.message();
            break;
        }
        if (bytes_written == 0) {
            break;
        }
    }
    connections.fetch_sub(1, std::memory_order_relaxed);
}

class EchoServer {
public:
    EchoServer(int port, size_t num_executors) {
        io_executors_.reserve(num_executors);
        for (size_t i = 0; i < num_executors; ++i) {
            io_executors_.emplace_back(std::make_unique<RingExecutor>(
              "client_executor_" + std::to_string(i), RingConfig{}, i));
        }
        listener_ = Listener::Create(port, io_executors_[0].get());
    }

    Task<void> AcceptLoop(RingExecutor* this_exr) {
        size_t ce_index{0};
        while (true) {
            auto conn = co_await listener_->Accept(io_executors_[ce_index].get());
            if (connections_.load(std::memory_order_relaxed) >= 10000) {
                delete conn;
                LOG(INFO) << "Reject connection since max connection has been reached.";
                continue;
            }
            connections_.fetch_add(1, std::memory_order_relaxed);
            ce_index = (ce_index + 1) % io_executors_.size();
            Echo(this_exr, conn, connections_);
        }
    }

    void Run() {
        SetNofileLimit(std::numeric_limits<uint16_t>::max());
        io_uring src_ring;
        auto ret = io_uring_queue_init(16, &src_ring, 0);
        if (ret) {
            LOG(FATAL) << "io_uring_queue_init:" << strerror(-ret);
        }

        ScheduleOn(&src_ring, io_executors_[0].get(), [this, exr = io_executors_[0].get()]() {
            this->AcceptLoop(exr);
        });

        std::promise<void> p;
        p.get_future().wait();
    }

private:
    std::vector<std::unique_ptr<RingExecutor>> io_executors_;
    std::unique_ptr<Listener> listener_;
    std::atomic<size_t> connections_{0};
};

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    const auto non_flag_index = google::ParseCommandLineFlags(&argc, &argv, true);
    FLAGS_alsologtostderr = 1;

    if (argc < 2) {
        std::cerr << "Usage:\n" << argv[0] << " [port] [num_io_executors (default to 1))]\n";
        return 1;
    }

    const size_t num_io_executors = ((argc == 3) ? std::atoi(argv[2]) : 1);
    EchoServer s(std::atoi(argv[1]), num_io_executors);
    s.Run();

    return 0;
}
