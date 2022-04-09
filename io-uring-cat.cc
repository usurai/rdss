#include "util.h"

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

static constexpr size_t QUEUE_DEPTH = 4;

struct IoSqRing {
    unsigned *head, *tail, *ring_mask, *ring_entries, *flags, *array;
};

struct IoCqRing {
    unsigned *head, *tail, *ring_mask, *ring_entries;
    io_uring_cqe* cqes;
};

struct Submitter {
    int ring_fd;
    bool init{false};
    IoSqRing sq_ring;
    io_uring_sqe* sqes;
    IoCqRing cq_ring;
    Submitter();
    int Submit(const std::string& file_name);
    void Read();
};

#define read_barrier() __asm__ __volatile__("" ::: "memory")
#define write_barrier() __asm__ __volatile__("" ::: "memory")

int io_uring_setup(unsigned entries, struct io_uring_params* p) {
    return (int)syscall(__NR_io_uring_setup, entries, p);
}

int io_uring_enter(
  int ring_fd, unsigned int to_submit, unsigned int min_complete, unsigned int flags) {
    return (int)syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete, flags, NULL, 0);
}

int Submitter::Submit(const std::string& file_name) {
    auto* file_info = new FileInfo(file_name);
    if (!file_info->valid) {
        return 1;
    }

    auto tail = *(sq_ring.tail);
    const auto next_tail = tail + 1;
    read_barrier();
    const auto index = tail & (*sq_ring.ring_mask);
    auto& sqe = sqes[index];
    sqe.fd = file_info->fd;
    sqe.flags = 0;
    sqe.opcode = IORING_OP_READV;
    sqe.addr = reinterpret_cast<uint64_t>(file_info->iovecs.data());
    sqe.len = file_info->iovecs.size();
    std::cout << "submit, iovecs size: " << sqe.len << " "
              << "tail: " << tail << '\n';
    sqe.off = 0;
    sqe.user_data = reinterpret_cast<uint64_t>(file_info);
    sq_ring.array[index] = index;
    tail = next_tail;

    if (*(sq_ring.tail) != tail) {
        *(sq_ring.tail) = tail;
        write_barrier();
    }

    if (io_uring_enter(ring_fd, 1, 1, IORING_ENTER_GETEVENTS) < 0) {
        std::cerr << "io_uring_enter";
        delete file_info;
        return 1;
    }

    return 0;
}

void Submitter::Read() {
    auto head = *(cq_ring.head);
    while (true) {
        read_barrier();

        if (head == *(cq_ring.tail)) {
            break;
        }
        std::cout << "new ceq\n";
        auto& cqe = cq_ring.cqes[head & (*cq_ring.ring_mask)];
        head++;
        if (cqe.res < 0) {
            std::cerr << "error";
            continue;
        }

        auto* file_info = reinterpret_cast<FileInfo*>(cqe.user_data);
        for (const auto& iovec : file_info->iovecs) {
            std::cout << std::string_view(static_cast<char*>(iovec.iov_base), iovec.iov_len);
        }
        delete (file_info);
    }
    *cq_ring.head = head;
    write_barrier();
}

Submitter::Submitter() {
    io_uring_params params;
    memset(&params, 0, sizeof(params));
    ring_fd = io_uring_setup(QUEUE_DEPTH, &params);
    if (ring_fd < 0) {
        std::cerr << "io_uring_setup";
        return;
    }
    assert(params.features & IORING_FEAT_SINGLE_MMAP);

    auto sring_size{params.sq_off.array + params.sq_entries * sizeof(unsigned)};
    auto cring_size{params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe)};
    sring_size = std::max(sring_size, cring_size);
    cring_size = sring_size;

    auto* sq_ptr = static_cast<unsigned*>(mmap(
      0,
      sring_size,
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      ring_fd,
      IORING_OFF_SQ_RING));
    if (sq_ptr == MAP_FAILED) {
        std::cerr << "mmap";
    }
    sq_ring.head = sq_ptr + params.sq_off.head;
    sq_ring.tail = sq_ptr + params.sq_off.tail;
    sq_ring.ring_mask = sq_ptr + params.sq_off.ring_mask;
    sq_ring.ring_entries = sq_ptr + params.sq_off.ring_entries;
    sq_ring.flags = sq_ptr + params.sq_off.flags;
    sq_ring.array = sq_ptr + params.sq_off.array;

    // auto* cq_ptr = static_cast<unsigned*>(mmap(
    //   0,
    //   cring_size,
    //   PROT_READ | PROT_WRITE,
    //   MAP_SHARED | MAP_POPULATE,
    //   ring_fd,
    //   IORING_OFF_CQ_RING));
    // if (cq_ptr == MAP_FAILED) {
    //     std::cerr << "mmap";
    // }
    auto* cq_ptr = sq_ptr;
    cq_ring.head = cq_ptr + params.cq_off.head;
    cq_ring.tail = cq_ptr + params.cq_off.tail;
    cq_ring.ring_mask = cq_ptr + params.cq_off.ring_mask;
    cq_ring.ring_entries = cq_ptr + params.cq_off.ring_entries;
    cq_ring.cqes = reinterpret_cast<io_uring_cqe*>(cq_ptr) + params.cq_off.cqes;

    sqes = static_cast<io_uring_sqe*>(mmap(
      0,
      params.sq_entries * sizeof(io_uring_sqe),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      ring_fd,
      IORING_OFF_SQES));
    if (sqes == MAP_FAILED) {
        std::cerr << "mmap";
    }

    init = true;
}

int main(int argc, char* argv[]) {
    Submitter submitter;
    if (!submitter.init) {
        std::cerr << "io_uring setup";
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (submitter.Submit(argv[i])) {
            std::cerr << "error reading " << argv[i];
            return 1;
        }
        submitter.Read();
    }

    return 0;
}
