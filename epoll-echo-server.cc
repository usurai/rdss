#include <cassert>
#include <arpa/inet.h> // For inet_ntop
#include <glog/logging.h>
#include <sys/epoll.h>

#include <fcntl.h>
#include <iostream>
#include <netdb.h> // For addrinfo, getaddrinfo()
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

int SetSocketFlags(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        LOG(ERROR) << "fcntl";
        return -1;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        LOG(ERROR) << "fcntl";
        return -1;
    }

    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        LOG(ERROR) << "fcntl";
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG(ERROR) << "fcntl";
        return -1;
    }
    return fd;
}

int CreateListenSocket() {
    const std::string node{"0.0.0.0"};
    const std::string port{"8081"};

    addrinfo* addrs;
    addrinfo hints{.ai_flags = AI_PASSIVE, .ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    if (getaddrinfo(node.c_str(), port.c_str(), &hints, &addrs)) {
        LOG(ERROR) << "getaddrinfo";
    }

    for (auto* addr = addrs; addr; addr = addr->ai_next) {
        int fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (bind(fd, addr->ai_addr, addr->ai_addrlen)) {
            close(fd);
            continue;
        }
        freeaddrinfo(addrs);
        // TODO: get backlog size from system
        const int backlog_size{128};
        if (listen(fd, backlog_size) == -1) {
            LOG(ERROR) << "listen";
        }
        LOG(INFO) << "Listening on " << node << ':' << port << '\n';
        // spdlog::info("Listening on {}:{}", node, port);
        if (SetSocketFlags(fd) == -1) {
            return -1;
        }
        const int enable = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            LOG(ERROR) << "setsocketopt";
            return -1;
        }
        return fd;
    }
    freeaddrinfo(addrs);
    return -1;
}

bool AcceptListening(int listen_fd, int epoll_fd) {
    while (true) {
        sockaddr addr;
        socklen_t addr_len = sizeof(addr);
        int conn_sock = accept4(listen_fd, &addr, &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (conn_sock >= 0) {
            char buffer[INET6_ADDRSTRLEN];
            LOG(INFO) << "Incoming connection from "
                      << inet_ntop(
                           AF_INET,
                           &(reinterpret_cast<sockaddr_in*>(&addr)->sin_addr),
                           buffer,
                           INET6_ADDRSTRLEN)
                      << ", conn_sock: " << conn_sock << '\n';

            epoll_event ev{.events = EPOLLIN | EPOLLRDHUP};
            ev.data.fd = conn_sock;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                LOG(ERROR) << "epoll_ctl";
                return false;
            }
            continue;
        }

        switch (errno) {
        case EAGAIN:
            return true;
        default:
            LOG(ERROR) << "accept4";
            return false;
        }
    }
}

int ThreadFunction(int thread_id, int listen_fd) {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        LOG(ERROR) << "epoll_create1";
        return -1;
    }

    epoll_event ev{.events = EPOLLIN | EPOLLET | EPOLLERR};
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        LOG(ERROR) << "epoll_ctl: listen_fd";
        return -1;
    }
    LOG(INFO) << "Thread #" << thread_id << "started.";

    int nfds;
    constexpr int kMaxEvents{10};
    epoll_event events[kMaxEvents];
    while (true) {
        nfds = epoll_wait(epoll_fd, events, kMaxEvents, -1);
        if (nfds == -1) {
            LOG(ERROR) << "epoll_wait";
            return -1;
        }
        for (auto i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listen_fd) {
                if (!AcceptListening(listen_fd, epoll_fd)) {
                    return -1;
                }
            } else {
                int fd = events[i].data.fd;
                constexpr int buffer_size{1024};
                char buffer[buffer_size];
                int bytes_read;
                while ((bytes_read = read(fd, buffer, buffer_size)) > 0) {
                    // LOG(INFO) << "Read: " << std::string(buffer, static_cast<size_t>(bytes_read));
                    assert(write(fd, buffer, static_cast<size_t>(bytes_read)) == bytes_read);
                }
            }
        }
    }

    return 0;
}

unsigned int GetThreadCount() {
    const auto processor_count{std::thread::hardware_concurrency()};
    if (processor_count == 0) {
        return 4;
    }
    return processor_count;
}

int main(int argc, char* argv[]) {
    // google::InitGoogleLogging(argv[0]);

    int listen_fd = CreateListenSocket();
    if (listen_fd <= 0) {
        LOG(ERROR) << "CreateListenSocket";
        return -1;
    }

    // const auto thread_count{GetThreadCount()};
    const auto thread_count{8};
    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(ThreadFunction, i, listen_fd);
    }

    for (int i = 0; i < thread_count; ++i) {
        threads[i].join();
    }
    return 0;
}
