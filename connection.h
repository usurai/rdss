#pragma once

#include "dragonfly/io_buf.h"
#include "dragonfly/redis_parser.h"
#include "server.h"

#include <cassert>
#include <charconv>
#include <iostream>
#include <liburing.h>
#include <string>
#include <string_view>
#include <vector>

namespace rdss {

// TODO: move implementation to .cpp
struct Connection {
    enum class State { Alive, Closing };
    struct Argument {
        char* data;
        size_t length;
    };

    io_uring* ring;
    io_uring* write_ring;
    State state{State::Alive};
    int fd;
    base::IoBuf buffer;
    facade::RedisParser parser;
    facade::RespExpr::Vec vec;
    std::string query_buffer;
    size_t read_length{0};
    size_t cursor{0};
    std::vector<Argument> arguments;
    size_t arg_cursor{0};

    Connection(io_uring* ring_, io_uring* write_ring_, int fd_);
    bool QueueRead();
    bool ExpandQueryBuffer(size_t size = READ_SIZE);
    void* NextBufferToRead() { return query_buffer.data() + read_length; }
    size_t NextReadLengthLimit() { return query_buffer.size() - read_length; }
    void IncreaseReadLength(size_t new_length) { read_length += new_length; }
    void* AsData() { return this; }
    enum class ParseResult { Error, NeedsMore, Success };
    // Parse multi-bulk input.
    // TODO: Currently only supports bulk strings type.
    // TODO: Currently only supports one pass, add accumulate.
    ParseResult ParseBuffer();
    bool QueueWrite(bool link = false);
    void Reply(std::string reply);
    bool Close();
    void ReplyAndClose(std::string reply);
    void SetClosing() { state = State::Closing; }
    bool Alive() const { return state == State::Alive; }
    std::string Command() const { return vec[0].GetString(); }
};

} // namespace rdss
