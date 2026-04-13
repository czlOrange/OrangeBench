// src/core/net/io_handler.cpp
#include "core/net/io_handler.hpp"

#include <sys/socket.h>
#include <sys/poll.h>
#include <cstring>
#include <system_error>

namespace httpserver::core {

// ============================================================================
// SocketAddress 实现
// ============================================================================

SocketAddress SocketAddress::FromIpPort(const std::string& ip, uint16_t port) {
    SocketAddress sa;
    sa.addr.sin_family = AF_INET;
    sa.addr.sin_port = htons(port);
    if (ip.empty() || ip == "0.0.0.0") {
        sa.addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, ip.c_str(), &sa.addr.sin_addr) != 1) {
            sa.addr.sin_addr.s_addr = INADDR_ANY;
        }
    }
    sa.len = sizeof(sa.addr);
    return sa;
}

std::string SocketAddress::ToString() const {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

std::string SocketAddress::GetIp() const {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return ip;
}

uint16_t SocketAddress::GetPort() const {
    return ntohs(addr.sin_port);
}

// ============================================================================
// PosixIOHandler 实现
// ============================================================================

class PosixIOHandler : public IIOHandler {
public:
    IOResult<SocketFd> CreateSocket(int domain, int type, int protocol) override {
        int fd = ::socket(domain, type, protocol);
        if (fd == -1) {
            return std::error_code(errno, std::system_category());
        }
        return fd;
    }

    IOResult<void> CloseSocket(SocketFd fd) noexcept override {
        if (::close(fd) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<void> ShutdownSocket(SocketFd fd, int how) noexcept override {
        if (::shutdown(fd, how) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<void> SetSocketOption(SocketFd fd, int level, int optname,
                                   const void* optval, socklen_t optlen) override {
        if (::setsockopt(fd, level, optname, optval, optlen) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<void> SetNonBlocking(SocketFd fd, bool enable) override {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            return std::error_code(errno, std::system_category());
        }
        if (enable) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        if (::fcntl(fd, F_SETFL, flags) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<void> SetTcpNoDelay(SocketFd fd, bool enable) override {
        int val = enable ? 1 : 0;
        if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<void> Connect(SocketFd fd, const sockaddr* addr, socklen_t addrlen) override {
        if (::connect(fd, addr, addrlen) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<void> Bind(SocketFd fd, const sockaddr* addr, socklen_t addrlen) override {
        if (::bind(fd, addr, addrlen) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<void> Listen(SocketFd fd, int backlog) override {
        if (::listen(fd, backlog) == -1) {
            return std::error_code(errno, std::system_category());
        }
        return {};
    }

    IOResult<SocketFd> Accept(SocketFd fd, sockaddr* addr, socklen_t* addrlen) override {
        int client = ::accept(fd, addr, addrlen);
        if (client == -1) {
            return std::error_code(errno, std::system_category());
        }
        return client;
    }

    IOResult<ssize_t> Send(SocketFd fd, const void* buf, size_t len, int flags) override {
        ssize_t ret = ::send(fd, buf, len, flags);
        if (ret == -1) {
            return std::error_code(errno, std::system_category());
        }
        return ret;
    }

    IOResult<ssize_t> Recv(SocketFd fd, void* buf, size_t len, int flags) override {
        ssize_t ret = ::recv(fd, buf, len, flags);
        if (ret == -1) {
            return std::error_code(errno, std::system_category());
        }
        return ret;
    }

    IOResult<ssize_t> SendTo(SocketFd fd, const void* buf, size_t len, int flags,
                             const sockaddr* dest_addr, socklen_t addrlen) override {
        ssize_t ret = ::sendto(fd, buf, len, flags, dest_addr, addrlen);
        if (ret == -1) {
            return std::error_code(errno, std::system_category());
        }
        return ret;
    }

    IOResult<ssize_t> RecvFrom(SocketFd fd, void* buf, size_t len, int flags,
                               sockaddr* src_addr, socklen_t* addrlen) override {
        ssize_t ret = ::recvfrom(fd, buf, len, flags, src_addr, addrlen);
        if (ret == -1) {
            return std::error_code(errno, std::system_category());
        }
        return ret;
    }

    IOResult<bool> PollRead(SocketFd fd, int timeout_ms) override {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret == -1) {
            return std::error_code(errno, std::system_category());
        }
        return (ret > 0 && (pfd.revents & POLLIN));
    }

    IOResult<bool> PollWrite(SocketFd fd, int timeout_ms) override {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret == -1) {
            return std::error_code(errno, std::system_category());
        }
        return (ret > 0 && (pfd.revents & POLLOUT));
    }

    IOResult<bool> PollError(SocketFd fd, int timeout_ms) override {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLERR | POLLHUP | POLLNVAL;
        pfd.revents = 0;
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret == -1) {
            return std::error_code(errno, std::system_category());
        }
        return (ret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)));
    }

    IOResult<SocketAddress> GetLocalAddress(SocketFd fd) override {
        SocketAddress addr;
        socklen_t len = sizeof(addr.addr);
        if (::getsockname(fd, (sockaddr*)&addr.addr, &len) == -1) {
            return std::error_code(errno, std::system_category());
        }
        addr.len = len;
        return addr;
    }

    IOResult<SocketAddress> GetPeerAddress(SocketFd fd) override {
        SocketAddress addr;
        socklen_t len = sizeof(addr.addr);
        if (::getpeername(fd, (sockaddr*)&addr.addr, &len) == -1) {
            return std::error_code(errno, std::system_category());
        }
        addr.len = len;
        return addr;
    }

    std::error_code GetLastSocketError() override {
        return std::error_code(errno, std::system_category());
    }

    std::string ErrorToString(int error_code) override {
        return std::strerror(error_code);
    }
};

// 工厂方法实现
std::shared_ptr<IIOHandler> IIOHandler::CreateDefault() {
    return std::make_shared<PosixIOHandler>();
}

} // namespace httpserver::core