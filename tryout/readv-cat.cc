#include "util.h"

#include <sys/uio.h>

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

bool ReadAndPrintFile(const std::string& file_name) {
    FileInfo file_info(file_name);
    if (!file_info.valid) {
        return false;
    }

    if (readv(file_info.fd, file_info.iovecs.data(), file_info.iovecs.size()) < 0) {
        std::cerr << "readv";
        return false;
    }

    for (const auto& iovec : file_info.iovecs) {
        std::cout << std::string_view(static_cast<char*>(iovec.iov_base), iovec.iov_len);
    }

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
