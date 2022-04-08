#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <vector>

static constexpr size_t BLOCKSIZE = 4096;

struct FileInfo {
    int fd;
    bool valid{false};
    std::vector<iovec> iovecs;
    std::vector<char> buffer;

    FileInfo(const std::string& file_path);

    ~FileInfo() {
        if (valid) {
            close(fd);
        }
    }
};

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

FileInfo::FileInfo(const std::string& file_name) {
    fd = open(file_name.data(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "open";
    }

    const auto file_size = GetFileSize(fd);
    if (file_size == 0) {
        return;
    }
    const auto blocks = file_size / BLOCKSIZE + (file_size % BLOCKSIZE ? 1 : 0);

    buffer.resize(file_size);
    iovecs.resize(blocks);

    size_t offset{0};
    for (auto& iovec : iovecs) {
        iovec.iov_base = buffer.data() + offset;
        iovec.iov_len = (file_size - offset > BLOCKSIZE) ? BLOCKSIZE : file_size - offset;
        offset += iovec.iov_len;
    }
    valid = true;
}
