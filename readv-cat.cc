#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <array>
#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

static constexpr size_t BLOCKSIZE = 4096;

size_t GetFileSize(int fd) {
    struct stat st;
    if (fstat(fd, &st) < 0) {
        std::cerr << "fstat";
        return 0;
    }
    if (S_ISBLK(st.st_mode)) {
        uint64_t bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
            std::cerr << "ioctl";
            return 0;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode)) {
        return st.st_size;
    }
    return 0;
}

bool ReadAndPrintFile(const std::string& file_name) {
    auto fd = open(file_name.data(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "open";
        return false;
    }

    const auto file_size = GetFileSize(fd);
    if (file_size == 0) {
        return false;
    }
    const auto blocks = file_size / BLOCKSIZE + (file_size % BLOCKSIZE ? 1 : 0);

    std::vector<char> buffer(file_size);
    std::vector<iovec> iovecs(blocks);

    size_t offset{0};
    for (auto& iovec : iovecs) {
        iovec.iov_base = buffer.data() + offset;
        iovec.iov_len = (file_size - offset > BLOCKSIZE) ? BLOCKSIZE : file_size - offset;
        offset += iovec.iov_len;
    }

    if (readv(fd, iovecs.data(), blocks) < 0) {
        std::cerr << "readv";
        return false;
    }

    for (const auto& iovec : iovecs) {
        std::cout << std::string_view(static_cast<char*>(iovec.iov_base), iovec.iov_len);
    }

    close(fd);

    return true;
}

int main(int argc, char* argv[]) {
    assert(argc >= 2);
    for (int i = 1; i < argc; ++i) {
        if (!ReadAndPrintFile(argv[i])) {
            std::cerr << "Error reading " << argv[i];
            return 1;
        }
    }
    return 0;
}
