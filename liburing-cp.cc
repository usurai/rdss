#include "util.h"

#include <array>
#include <cassert>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <liburing.h>
#include <vector>

static constexpr size_t QD = 1024;

struct TransferUnit {
    enum class State { Reading, Writing };
    State state{State::Reading};
    size_t offset, cursor, length, index;
    std::vector<char> buffer;

    TransferUnit(size_t offset_, size_t length_, size_t index_)
      : offset(offset_)
      , cursor(0)
      , length(length_)
      , index(index_)
      , buffer(length) {}

    void* Buffer() { return buffer.data() + cursor; }
    size_t Offset() const { return offset + cursor; }
    size_t Length() const { return length - cursor; }
    bool IsRead() const { return state == State::Reading; }
    bool AdvanceCursor(size_t adv) {
        cursor += adv;
        return cursor == length;
    }
    void TurnWritting() {
        cursor = 0;
        state = State::Writing;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        return 1;
    }

    const auto src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) {
        std::cerr << "open";
        return 1;
    }

    const auto dest_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd < 0) {
        std::cerr << "open";
        close(src_fd);
        return 1;
    }

    io_uring ring;
    auto ret = io_uring_queue_init(QD, &ring, 0);
    if (ret) {
        std::cerr << "io_uring_queue_init: " << strerror(-ret) << '\n';
        return 1;
    }

    const auto file_size{GetFileSize(src_fd)};
    std::vector<TransferUnit> transfer_units;
    size_t reading{0};
    size_t writting{0};
    size_t offset{0};
    size_t written{0};

    auto new_read = [&](int unit_index = -1) {
        assert(file_size > offset);
        const auto size{file_size - offset >= BLOCKSIZE ? BLOCKSIZE : file_size - offset};
        if (unit_index == -1) {
            transfer_units.emplace_back(offset, size, transfer_units.size());
            unit_index = transfer_units.size() - 1;
        } else {
            auto& unit = transfer_units[unit_index];
            assert(unit.length >= size);
            unit.offset = offset;
            unit.cursor = 0;
            unit.length = size;
            unit.state = TransferUnit::State::Reading;
        }
        offset += size;
        return unit_index;
    };

    auto queue_op = [&](TransferUnit& unit) {
        auto* sqe = io_uring_get_sqe(&ring);
        if (sqe == nullptr) {
            std::cerr << "io_uring_get_sqe\n";
            return 1;
        }
        if (unit.IsRead()) {
            io_uring_prep_read(sqe, src_fd, unit.Buffer(), unit.Length(), unit.Offset());
        } else {
            io_uring_prep_write(sqe, dest_fd, unit.Buffer(), unit.Length(), unit.Offset());
        }
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(unit.index));
        return 0;
    };

    bool queued_new{false};
    while (offset < file_size || written < file_size) {
        while (offset < file_size && reading + writting < QD) {
            if (queue_op(transfer_units[new_read()])) {
                return 1;
            }
            ++reading;
            queued_new = true;
        }
        if (queued_new) {
            io_uring_submit(&ring);
            queued_new = false;
        }

        io_uring_cqe* cqe;
        auto ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret) {
            std::cerr << "io_uring_wait_cqe:" << strerror(-ret) << '\n';
            return 1;
        }
        auto& unit = transfer_units[static_cast<size_t>(cqe->user_data)];
        if (cqe->res < 0) {
            std::cerr << "async " << static_cast<int>(unit.state) << ":" << strerror(-cqe->res)
                      << '\n';
            return 1;
        }
        io_uring_cqe_seen(&ring, cqe);

        if (!unit.AdvanceCursor(cqe->res)) {
            if (queue_op(unit)) {
                return 1;
            }
            queued_new = true;
        } else {
            unit.cursor = 0;
            if (unit.IsRead()) {
                unit.TurnWritting();
                if (queue_op(unit)) {
                    return 1;
                }
                --reading;
                ++writting;
                queued_new = true;
            } else {
                written += unit.length;
                if (offset < file_size) {
                    new_read(unit.index);
                    if (queue_op(unit)) {
                        return 1;
                    }
                    --writting;
                    ++reading;
                    queued_new = true;
                }
            }
        }
    }

    io_uring_queue_exit(&ring);
    close(src_fd);
    close(dest_fd);

    return 0;
}
