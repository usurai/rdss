#include "util.h"

#include <iostream>
#include <liburing.h>

static constexpr size_t QD = 16;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        return 1;
    }

    io_uring ring;
    io_uring_queue_init(QD, &ring, 0);

    FileInfo in_file(argv[1]);

    auto* sqe = io_uring_get_sqe(&ring);
    if (sqe == nullptr) {
        std::cerr << "io_uring_get_sqe";
        return 1;
    }
    io_uring_prep_readv(sqe, in_file.fd, in_file.iovecs.data(), in_file.iovecs.size(), 0);
    io_uring_sqe_set_data(sqe, &in_file);
    io_uring_submit(&ring);

    io_uring_cqe* cqe;
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe == nullptr) {
        std::cerr << "io_uring_wait_cqe";
        return 1;
    }
    if (cqe->res <= 0) {
        std::cerr << "readv";
        return 1;
    }
    io_uring_cqe_seen(&ring, cqe);

    const auto out_file_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_file_fd < 0) {
        std::cerr << "open";
        return 1;
    }
    size_t offset{0};
    sqe = io_uring_get_sqe(&ring);
    if (sqe == nullptr) {
        std::cerr << "io_uring_get_sqe";
        return 1;
    }
    io_uring_prep_writev(sqe, out_file_fd, in_file.iovecs.data(), in_file.iovecs.size(), 0);
    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe == nullptr) {
        std::cerr << "io_uring_wait_cqe";
        return 1;
    }
    if (cqe->res <= 0) {
        std::cerr << "writev";
        return 1;
    }
    io_uring_cqe_seen(&ring, cqe);

    io_uring_queue_exit(&ring);
    close(out_file_fd);

    return 0;
}
