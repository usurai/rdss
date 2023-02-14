#include "connection.h"

#include "constants.h"

#include <glog/logging.h>

namespace rdss {

Connection::~Connection() {
    if (active_) {
        close(fd_);
    }
    LOG(INFO) << "Closing connection " << fd_;
}

AwaitableRecv Connection::Recv(Buffer::SinkType buffer) {
    return AwaitableRecv(processor_, fd_, std::move(buffer));
}

AwaitableCancellableRecv
Connection::CancellableRecv(Buffer::SinkType buffer, CancellationToken* token) {
    return AwaitableCancellableRecv(processor_, fd_, std::move(buffer), token);
}

AwaitableSend Connection::Send(std::string data) {
    return AwaitableSend(processor_, fd_, std::move(data));
}

void Connection::Close() {
    if (!active_) {
        return;
    }
    close(fd_);
    active_ = false;
}

/***
Connection::Connection(io_uring* ring_, io_uring* write_ring_, int fd_)
  : ring(ring_)
  , write_ring(write_ring_)
  , fd(fd_)
  , read_buffer(std::make_unique<Buffer>(kReadBufferLength)) {}

void Connection::InitParser() {
    assert(parser == nullptr);
    assert(read_buffer != nullptr);
    auto buf = read_buffer->Source();
    assert(!buf.empty());
    if (buf[0] == '*') {
        parser = std::make_unique<MultiBulkParser>(read_buffer.get());
    } else {
        parser = std::make_unique<InlineParser>(read_buffer.get());
    }
}

bool Connection::QueueRead() {
    // TODO: Handle error here.
    read_buffer->EnsureAvailable(kReadBufferLength);

    auto* sqe = io_uring_get_sqe(ring);
    if (sqe == nullptr) {
        return false;
    }
    io_uring_prep_read(sqe, fd, read_buffer->Data(), kReadBufferLength, 0);
    io_uring_sqe_set_data(sqe, AsData());
    if (SQE_ASYNC) {
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
    }
    return true;
}

bool Connection::QueueWrite(bool link) {
    auto* sqe = io_uring_get_sqe(write_ring);
    if (sqe == nullptr) {
        return false;
    }
    io_uring_prep_write(
      sqe, fd, output_buffer.data(), static_cast<unsigned int>(output_buffer.size()), 0);
    io_uring_sqe_set_data(sqe, AsData());
    if (SQE_ASYNC) {
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
    }
    if (link) {
        sqe->flags |= IOSQE_IO_LINK;
    }
    // assert(io_uring_submit(ring) == 1);
    io_uring_submit(write_ring);
    return true;
}

void Connection::Reply(std::string reply) {
    output_buffer = std::move(reply);
    assert(QueueWrite());
    // state = State::Writting;
}

bool Connection::Close() {
    auto* sqe = io_uring_get_sqe(write_ring);
    if (sqe == nullptr) {
        return false;
    }
    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data(sqe, AsData());
    io_uring_submit(write_ring);
    return true;
}

void Connection::ReplyAndClose(std::string reply) {
    output_buffer = std::move(reply);
    assert(QueueWrite(true));
    assert(Close());
    SetClosing();
}
***/

} // namespace rdss
