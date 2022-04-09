#include "util.h"

#include <cassert>
#include <liburing.h>

static constexpr size_t QUEUE_DEPTH = 1;

int Submit(io_uring* ring, const std::string& file_path) {
    auto file_info = new FileInfo(file_path);
    if (!file_info->valid) {
        return 1;
    }

    io_uring_sqe* sqe = io_uring_get_sqe(ring);
    io_uring_prep_readv(sqe, file_info->fd, file_info->iovecs.data(), file_info->iovecs.size(), 0);
    io_uring_sqe_set_data(sqe, file_info);
    io_uring_submit(ring);
    return 0;
}

int GetCompletion(io_uring* ring) {
    io_uring_cqe* cqe;
    if (io_uring_wait_cqe(ring, &cqe) < 0) {
        std::cerr << "io_uring_wait_cqe";
        return 1;
    }
    if (cqe->res < 0) {
        std::cout << "readv";
        return 1;
    }
    auto* file_info = reinterpret_cast<FileInfo*>(io_uring_cqe_get_data(cqe));
    for (const auto& iovec : file_info->iovecs) {
        std::cout << std::string_view(static_cast<char*>(iovec.iov_base), iovec.iov_len);
    }
    io_uring_cqe_seen(ring, cqe);
    delete file_info;
    return 0;
}

int main(int argc, char* argv[]) {
    assert(argc >= 2);

    io_uring ring;
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);

    for (int i = 1; i < argc; ++i) {
        if (Submit(&ring, argv[i])) {
            return 1;
        }
        if (GetCompletion(&ring)) {
            return 1;
        }
    }
    io_uring_queue_exit(&ring);
    return 0;
}
