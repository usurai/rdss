#include "util.h"

#include <arpa/inet.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>

namespace rdss {

bool SetNofileLimit(uint32_t limit) {
    int ret;
    rlimit rlim;
    if ((ret = getrlimit(RLIMIT_NOFILE, &rlim)) != 0) {
        LOG(ERROR) << "getrlimit: " << strerror(errno);
        return false;
    }
    LOG(INFO) << "NOFILE rlimit: " << rlim.rlim_cur << ' ' << rlim.rlim_max;

    rlim.rlim_cur = limit;
    rlim.rlim_max = std::max<uint32_t>(rlim.rlim_max, limit);
    if ((ret = setrlimit(RLIMIT_NOFILE, &rlim)) != 0) {
        LOG(FATAL) << "setrlimit: " << strerror(errno);
    }

    if ((ret = getrlimit(RLIMIT_NOFILE, &rlim)) != 0) {
        LOG(ERROR) << "getrlimit: " << strerror(errno);
        return false;
    }

    LOG(INFO) << "NOFILE rlimit after modification: " << rlim.rlim_cur << ' ' << rlim.rlim_max;
    return (rlim.rlim_cur == limit && rlim.rlim_max == limit);
}

int CreateListeningSocket(uint16_t port) {
    // socket
    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        LOG(ERROR) << "socket: " << strerror(errno);
        return 0;
    }

    int enable{1};
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        LOG(ERROR) << "setsockopt" << strerror(errno);
        return 0;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind
    if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG(ERROR) << "bind: " << strerror(errno);
        return 0;
    }

    // listen
    // TODO: Make backlog constant or config
    if (listen(sock, 1000) < 0) {
        LOG(ERROR) << "listen: " << strerror(errno);
        return 0;
    }

    return sock;
}

} // namespace rdss
