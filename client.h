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

    // Parse multi-bulk input.
    // TODO: Currently only supports bulk strings type.
    // TODO: Currently only supports one pass, add accumulate.
    ParseResult ParseBuffer() {
        assert(cursor == 0);
        if (query_buffer[cursor] != '*') {
            return ParseResult::Error;
        }
        ++cursor;

        // Finds index of next CRLF starting from cursor. Returns 0 if not found.
        auto next_newline = [&]() {
            auto newline = query_buffer.find_first_of('\r', cursor);
            if (
              newline == std::string::npos || newline + 1 >= read_length
              || query_buffer[newline + 1] != '\n') {
                return 0UL;
            }
            return newline;
        };

        // Tries to parse query_buffer[cursor, cursor+newline] to int.
        // Returns [success, result]: returns fail on parsed int <= 0 even if parsing succeed.
        auto parse_int = [&](size_t newline) {
            int argc{0};
            // TODO: This seems extremely slow for large number(INT_MAX)
            auto [ptr, ec] = std::from_chars(
              query_buffer.data() + cursor, query_buffer.data() + newline, argc);
            if (ec != std::errc() || ptr != query_buffer.data() + newline || argc < 0) {
                return std::make_pair(false, 0UL);
            }
            return std::make_pair(true, static_cast<size_t>(argc));
        };

        auto newline = next_newline();
        if (!newline) {
            return ParseResult::Error;
        }

        auto [success, argc] = parse_int(newline);
        if (!success || argc == 0) {
            return ParseResult::Error;
        }
        arguments.resize(argc);
        cursor = newline + 2;

        assert(arg_cursor == 0);
        while (cursor < read_length) {
            if (query_buffer[cursor] != '$') {
                return ParseResult::Error;
            }
            ++cursor;

            if ((newline = next_newline()) == 0) {
                return ParseResult::Error;
            }
            const auto [success, str_len] = parse_int(newline);
            if (!success) {
                return ParseResult::Error;
            }

            cursor = newline + 2;
            if (cursor + str_len + 2 > read_length) {
                return ParseResult::Error;
            }

            if (
              query_buffer[cursor + str_len] != '\r'
              || query_buffer[cursor + str_len + 1] != '\n') {
                return ParseResult::Error;
            }

            arguments[arg_cursor].data = query_buffer.data() + cursor;
            arguments[arg_cursor].length = str_len;

            cursor += arguments[arg_cursor].length + 2;
            if (++arg_cursor == arguments.size()) {
                return ParseResult::Success;
            }
        }
        return ParseResult::Error;
    }

    bool QueueWrite() {
        auto* sqe = io_uring_get_sqe(ring);
        if (sqe == nullptr) {
            return false;
        }
        io_uring_prep_write(sqe, fd, query_buffer.data(), query_buffer.size(), 0);
        io_uring_sqe_set_data(sqe, AsData());
        io_uring_submit(ring);
        return true;
    }

    void Reply(std::string reply) {
        query_buffer = std::move(reply);
        assert(QueueWrite());
    }

    void SetError() { state = State::Error; }

    std::string_view Command() const {
        return std::string_view(arguments[0].data, arguments[0].length);
    }
};

} // namespace rdss
