#include "util.h"

#include <array>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <liburing.h>
#include <vector>

static constexpr size_t QD = 1024;

struct TransferUnit {
    enum class State { Reading, Writing };
    State state{State::Reading};
    size_t offset, length, index;
    std::vector<char> buffer;

    TransferUnit(size_t offset_, size_t length_, size_t index_)
      : offset(offset_)
      , length(length_)
      , index(index_)
      , buffer(length) {}
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
    io_uring_queue_init(QD, &ring, 0);

    const auto file_size{GetFileSize(src_fd)};
    std::vector<TransferUnit> transfer_units;
    size_t reading{0};
    size_t offset{0};
    size_t written{0};

    while (offset < file_size) {
        auto* sqe = io_uring_get_sqe(&ring);
        if (sqe == nullptr) {
            break;
        }
        const auto size{file_size - offset >= BLOCKSIZE ? BLOCKSIZE : file_size - offset};
        transfer_units.emplace_back(offset, size, transfer_units.size());
        io_uring_prep_read(sqe, src_fd, transfer_units.back().buffer.data(), size, offset);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(transfer_units.size() - 1));
        offset += size;
        ++reading;
        std::cout << "queue " << reading << "th read request\n";
    }
    if (reading) {
        io_uring_submit(&ring);
    }

    while (offset < file_size || written < file_size) {
        io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);
        if (cqe == nullptr) {
            std::cerr << "io_uring_wait_cqe\n";
            return 1;
        }
        auto& unit = transfer_units[static_cast<size_t>(cqe->user_data)];
        if (cqe->res < 0) {
            std::cerr << "async " << static_cast<int>(unit.state) << ":" << strerror(-cqe->res)
                      << '\n';
            return 1;
        }
        io_uring_cqe_seen(&ring, cqe);
        if (unit.state == TransferUnit::State::Reading) {
            unit.state = TransferUnit::State::Writing;
            auto* sqe = io_uring_get_sqe(&ring);
            if (sqe == nullptr) {
                std::cerr << "io_uring_get_sqe\n";
                return 1;
            }
            io_uring_prep_write(sqe, dest_fd, unit.buffer.data(), unit.length, unit.offset);
            io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(unit.index));
            io_uring_submit(&ring);
        } else {
            written += unit.length;
            if (offset < file_size) {
                auto* sqe = io_uring_get_sqe(&ring);
                if (sqe == nullptr) {
                    std::cerr << "io_uring_get_sqe\n";
                    return 1;
                }
                unit.state = TransferUnit::State::Reading;
                unit.length = file_size - offset > BLOCKSIZE ? BLOCKSIZE : file_size - offset;
                unit.offset = offset;
                io_uring_prep_read(sqe, src_fd, unit.buffer.data(), unit.length, offset);
                io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(unit.index));
                io_uring_submit(&ring);
                offset += unit.length;
            }
        }
    }

    io_uring_queue_exit(&ring);
    close(src_fd);
    close(dest_fd);

    return 0;
}
