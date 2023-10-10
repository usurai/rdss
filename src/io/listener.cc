#include "listener.h"

#include "sys/util.h"

#include <arpa/inet.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace rdss {

Listener::Listener(int listen_fd, RingExecutor* executor)
  : fd_(listen_fd)
  , executor_(executor) {}

std::unique_ptr<Listener> Listener::Create(int port, RingExecutor* executor) {
    const auto fd = CreateListeningSocket(port);
    if (fd == 0) {
        LOG(FATAL) << "Unable to create listener";
    }
    LOG(INFO) << "Listening on port " << port << " with fd " << fd;
    return std::unique_ptr<Listener>(new Listener(fd, executor));
}

} // namespace rdss
