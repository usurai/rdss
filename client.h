#pragma once

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
struct Client {
    enum class State { Reading, Writting, Error };

    struct Argument {
        char* data;
        size_t length;
    };

    io_uring* ring;

    State state{State::Reading};
    int fd;

    std::string query_buffer;
    size_t read_length{0};
    size_t cursor{0};

    std::vector<Argument> arguments;
    size_t arg_cursor{0};

    Client(io_uring* ring_, int fd_)
      : ring(ring_)
      , fd(fd_) {
        query_buffer.resize(READ_SIZE);
    }

    bool Reading() const { return state == Client::State::Reading; }

    bool Writting() const { return state == Client::State::Writting; }

    bool HasError() const { return state == Client::State::Error; }

    bool QueueRead() {
        assert(NextReadLengthLimit() > 0);

        auto* sqe = io_uring_get_sqe(ring);
        if (sqe == nullptr) {
            return false;
        }
        io_uring_prep_read(sqe, fd, NextBufferToRead(), NextReadLengthLimit(), 0);
        io_uring_sqe_set_data(sqe, AsData());
        io_uring_submit(ring);
        return true;
    }

    bool ExpandQueryBuffer(size_t size = READ_SIZE) {
        if (query_buffer.size() + size > READ_THRESHOLD) {
            return false;
        }
        query_buffer.resize(query_buffer.size() + size);
        return true;
    }

    void* NextBufferToRead() { return query_buffer.data() + read_length; }

    size_t NextReadLengthLimit() { return query_buffer.size() - read_length; }

    void IncreaseReadLength(size_t new_length) { read_length += new_length; }

    void* AsData() { return this; }

    enum class ParseResult { Error, NeedsMore, Success };

    // TODO: implement
    // TODO: currently only supports one pass, add accumulate.
    ParseResult ParseBuffer() {
        assert(cursor == 0);
        if (query_buffer[cursor] != '*') {
            return ParseResult::Error;
        }
        ++cursor;

        auto newline = query_buffer.find_first_of('\r', cursor);
        if (newline == std::string::npos || query_buffer[newline + 1] != '\n') {
            return ParseResult::Error;
        }

        int argc;
        auto [_, ec] = std::from_chars(
          query_buffer.data() + cursor, query_buffer.data() + newline, argc);
        if (ec != std::errc()) {
            return ParseResult::Error;
        }
        arguments.resize(argc);
        cursor = newline + 2;

        std::cout << "argc:" << argc << '\n';
        return ParseResult::Error;
    }

    bool QueueWrite(const std::string_view& sv) {
        auto* sqe = io_uring_get_sqe(ring);
        if (sqe == nullptr) {
            return false;
        }
        io_uring_prep_write(sqe, fd, sv.data(), sv.size(), 0);
        io_uring_sqe_set_data(sqe, AsData());
        io_uring_submit(ring);
        return true;
    }

    // TODO: implement
    void Reply(const std::string& reply) { assert(QueueWrite(reply)); }

    void SetError() { state = State::Error; }

    std::string_view Command() const {
        return std::string_view(arguments[0].data, arguments[0].length);
    }
};

} // namespace rdss
