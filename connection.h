#pragma once

#include "async_operation.h"
#include "async_operation_processor.h"
#include "buffer.h"

#include <string>

namespace rdss {

/*** TODO: COROUTINE usage
    //create
    cospawn conn->xxx()
    // xxx()
    while (conn->is_closed())
        co_await n = network->read()
        res, args_ = parser->parse()
        if (res) service->queue_command(this)
    // reply()
        write_buffer->append(xxx)
        ++to_write
***/

class AwaitableRecv;
// class AwaitableSend;
// class AwaitableClose;

class Connection {
public:
    Connection(int fd, AsyncOperationProcessor* processor)
      : fd_(fd)
      , processor_(processor) {}

    AwaitableRecv Recv(Buffer::SinkType buffer);

    void Close() { close(fd_); }

    // AwaitableSend Send(std::string);
    // AwaitableClose Close();

private:
    int fd_;
    AsyncOperationProcessor* processor_;
};

/***
struct Connection {
    enum class State { Alive, Closing };

    io_uring* ring;
    io_uring* write_ring;
    State state{State::Alive};
    int fd;

    std::unique_ptr<Buffer> read_buffer;
    std::unique_ptr<RedisParser> parser{nullptr};
    std::string output_buffer;

    Connection(io_uring* ring_, io_uring* write_ring_, int fd_);
    void InitParser();
    bool QueueRead();
    void* AsData() { return this; }
    bool QueueWrite(bool link = false);
    void Reply(std::string reply);
    bool Close();
    void ReplyAndClose(std::string reply);
    // TODO: This seems useless.
    void SetClosing() { state = State::Closing; }
    bool Alive() const { return state == State::Alive; }
};
***/

} // namespace rdss
