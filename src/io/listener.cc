#include "listener.h"

#include <arpa/inet.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace rdss {

Listener::Listener(int listen_fd, RingExecutor* executor)
  : fd_(listen_fd)
  , executor_(executor) {}

std::unique_ptr<Listener> Listener::Create(int port, RingExecutor* executor) {
    auto create_listening_socket = [port]() {
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
        if (listen(sock, 1000) < 0) {
            LOG(ERROR) << "listen: " << strerror(errno);
            return 0;
        }

        return sock;
    };

    const auto fd = create_listening_socket();
    if (fd == 0) {
        LOG(FATAL) << "Unable to create listener";
    }
    LOG(INFO) << "Listening on port " << port << " with fd " << fd;
    return std::unique_ptr<Listener>(new Listener(fd, executor));
}

} // namespace rdss
