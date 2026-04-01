// src/net/socket.cpp
#include "../../include/httpserver/net/socket.hpp"
#include "../../include/httpserver/net/address.hpp"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <system_error>

namespace httpserver::net {

namespace {

std::string last_error() {
    return std::strerror(errno);
}

void socket_type_to_native(SocketType type, int& domain, int& sock_type, int& protocol) {
    switch (type) {
        case SocketType::TCP:
            domain = AF_INET;
            sock_type = SOCK_STREAM;
            protocol = 0;
            break;
        case SocketType::UDP:
            domain = AF_INET;
            sock_type = SOCK_DGRAM;
            protocol = 0;
            break;
        case SocketType::UNIX_STREAM:
        case SocketType::UNIX_DGRAM:
            throw std::runtime_error("Unix 域套接字暂不支持");
        default:
            throw std::invalid_argument("未知的 SocketType");
    }
}

bool netaddress_to_sockaddr_in(const NetAddress& addr, struct sockaddr_in& sa) {
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(addr.port());

    const std::string& ip = addr.ip();
    if (ip.empty() || ip == "0.0.0.0") {
        sa.sin_addr.s_addr = INADDR_ANY;
        return true;
    }
    
    if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) <= 0) {
        errno = EINVAL;
        return false;
    }
    return true;
}

template<typename T>
bool set_socket_option(int fd, int level, int optname, const T& value) {
    return ::setsockopt(fd, level, optname, &value, sizeof(value)) == 0;
}

bool set_timeout_option(int fd, int level, int optname, int timeout_ms) {
    if (timeout_ms <= 0) return true;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return set_socket_option(fd, level, optname, tv);
}

// 检查 socket 是否已绑定
bool is_bound(int fd) {
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&sa), &len) != 0) {
        return false;
    }
    // 检查是否绑定了具体地址（非 0.0.0.0:0）
    return sa.sin_port != 0;
}

} // unnamed namespace

class SocketImpl : public Socket {
public:
    explicit SocketImpl(SocketType type)
        : fd_(-1), type_(type), is_connected_(false), last_error_(0) {
        int domain, socktype, protocol;
        socket_type_to_native(type_, domain, socktype, protocol);
        fd_ = ::socket(domain, socktype, protocol);
        if (fd_ < 0) {
            throw std::system_error(errno, std::system_category(), 
                                    "创建 socket 失败");
        }
    }

    SocketImpl(int fd, SocketType type)
        : fd_(fd), type_(type), is_connected_(false), last_error_(0) {
        if (fd_ < 0) {
            throw std::invalid_argument("无效的文件描述符");
        }
        struct sockaddr_in sa;
        socklen_t len = sizeof(sa);
        if (::getpeername(fd_, reinterpret_cast<struct sockaddr*>(&sa), &len) == 0) {
            is_connected_ = true;
        }
    }

    ~SocketImpl() override {
        close();
    }

    SocketImpl(const SocketImpl&) = delete;
    SocketImpl& operator=(const SocketImpl&) = delete;

    // 移动构造 - 确保原对象完全失效
    SocketImpl(SocketImpl&& other) noexcept
        : fd_(other.fd_)
        , type_(other.type_)
        , is_connected_(other.is_connected_)
        , last_error_(other.last_error_) {
        other.fd_ = -1;
        // other.type_ = SocketType::TCP;
        other.is_connected_ = false;
        other.last_error_ = 0;
    }

    // 移动赋值
    SocketImpl& operator=(SocketImpl&& other) noexcept {
        if (this != &other) {
            // 释放当前资源
            if (fd_ >= 0) {
                ::close(fd_);
            }
            
            // 转移所有权
            fd_ = other.fd_;
            type_ = other.type_;
            is_connected_ = other.is_connected_;
            last_error_ = other.last_error_;
            
            // 原对象放弃所有权
            other.fd_ = -1;
            other.type_ = SocketType::TCP;
            other.is_connected_ = false;
            other.last_error_ = 0;
        }
        return *this;
    }

    bool bind(const NetAddress& addr) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return false;
        }

        struct sockaddr_in sa;
        if (!netaddress_to_sockaddr_in(addr, sa)) {
            last_error_ = errno;
            return false;
        }
        
        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) != 0) {
            last_error_ = errno;
            return false;
        }
        
        last_error_ = 0;
        return true;
    }

    bool listen(int backlog) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return false;
        }
        
        if (type_ != SocketType::TCP) {
            last_error_ = EOPNOTSUPP;
            return false;
        }
        
        // 可选：检查是否已绑定，如果未绑定则自动绑定
        // 这里直接调用 listen，让系统决定（Linux 会自动绑定到随机端口）
        if (::listen(fd_, backlog) != 0) {
            last_error_ = errno;
            return false;
        }
        
        last_error_ = 0;
        return true;
    }

    std::unique_ptr<Socket> accept(NetAddress* peer_addr) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return nullptr;
        }
        
        if (type_ != SocketType::TCP) {
            last_error_ = EOPNOTSUPP;
            return nullptr;
        }

        struct sockaddr_in sa;
        socklen_t len = sizeof(sa);
        int client_fd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&sa), &len);
        
        if (client_fd < 0) {
            last_error_ = errno;
            return nullptr;
        }
        
        if (peer_addr) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            *peer_addr = NetAddress(ip, ntohs(sa.sin_port));
        }
        
        last_error_ = 0;
        return std::make_unique<SocketImpl>(client_fd, type_);
    }

    bool connect(const NetAddress& addr) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return false;
        }

        struct sockaddr_in sa;
        if (!netaddress_to_sockaddr_in(addr, sa)) {
            last_error_ = errno;
            return false;
        }
        
        int ret = ::connect(fd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));
        if (ret == 0) {
            is_connected_ = true;
            last_error_ = 0;
            return true;
        }
        
        last_error_ = errno;
        
        if (last_error_ == EINPROGRESS) {
            return true;
        }
        
        return false;
    }

    ssize_t send(const void* data, size_t len) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return -1;
        }
        
        ssize_t ret = ::send(fd_, data, len, 0);
        if (ret < 0) {
            last_error_ = errno;
        } else {
            last_error_ = 0;
        }
        return ret;
    }

    ssize_t recv(void* buf, size_t len) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return -1;
        }
        
        ssize_t ret = ::recv(fd_, buf, len, 0);
        if (ret < 0) {
            last_error_ = errno;
        } else {
            last_error_ = 0;
        }
        return ret;
    }

    ssize_t sendto(const void* data, size_t len, const NetAddress& addr) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return -1;
        }
        
        if (type_ != SocketType::UDP) {
            last_error_ = EOPNOTSUPP;
            return -1;
        }
        
        struct sockaddr_in sa;
        if (!netaddress_to_sockaddr_in(addr, sa)) {
            last_error_ = errno;
            return -1;
        }
        
        ssize_t ret = ::sendto(fd_, data, len, 0,
                               reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));
        if (ret < 0) {
            last_error_ = errno;
        } else {
            last_error_ = 0;
        }
        return ret;
    }

    ssize_t recvfrom(void* buf, size_t len, NetAddress* src_addr) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return -1;
        }
        
        if (type_ != SocketType::UDP) {
            last_error_ = EOPNOTSUPP;
            return -1;
        }
        
        struct sockaddr_in sa;
        socklen_t addrlen = sizeof(sa);
        ssize_t n = ::recvfrom(fd_, buf, len, 0,
                               reinterpret_cast<struct sockaddr*>(&sa), &addrlen);
        
        if (n < 0) {
            last_error_ = errno;
            return n;
        }
        
        last_error_ = 0;
        
        if (n >= 0 && src_addr) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            *src_addr = NetAddress(ip, ntohs(sa.sin_port));
        }
        
        return n;
    }

    bool set_nonblocking(bool nonblocking) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return false;
        }
        
        int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags < 0) {
            last_error_ = errno;
            return false;
        }
        
        if (nonblocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        
        if (::fcntl(fd_, F_SETFL, flags) != 0) {
            last_error_ = errno;
            return false;
        }
        
        last_error_ = 0;
        return true;
    }

    bool set_options(const SocketOptions& options) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return false;
        }
        
        bool success = true;
        
        if (!set_socket_option(fd_, SOL_SOCKET, SO_REUSEADDR, 
                               static_cast<int>(options.reuse_addr))) {
            last_error_ = errno;
            success = false;
        }
        
#ifdef SO_REUSEPORT
        if (!set_socket_option(fd_, SOL_SOCKET, SO_REUSEPORT,
                               static_cast<int>(options.reuse_port))) {
            last_error_ = errno;
            success = false;
        }
#endif
        
        if (type_ == SocketType::TCP) {
            if (!set_socket_option(fd_, IPPROTO_TCP, TCP_NODELAY,
                                   static_cast<int>(options.tcp_no_delay))) {
                last_error_ = errno;
                success = false;
            }
        }
        
        if (!set_socket_option(fd_, SOL_SOCKET, SO_KEEPALIVE,
                               static_cast<int>(options.keep_alive))) {
            last_error_ = errno;
            success = false;
        }
        
        if (!set_timeout_option(fd_, SOL_SOCKET, SO_RCVTIMEO, options.recv_timeout_ms)) {
            last_error_ = errno;
            success = false;
        }
        
        if (!set_timeout_option(fd_, SOL_SOCKET, SO_SNDTIMEO, options.send_timeout_ms)) {
            last_error_ = errno;
            success = false;
        }
        
        if (options.recv_buffer_size > 0) {
            if (!set_socket_option(fd_, SOL_SOCKET, SO_RCVBUF, options.recv_buffer_size)) {
                last_error_ = errno;
                success = false;
            }
        }
        
        if (options.send_buffer_size > 0) {
            if (!set_socket_option(fd_, SOL_SOCKET, SO_SNDBUF, options.send_buffer_size)) {
                last_error_ = errno;
                success = false;
            }
        }
        
        if (success) {
            last_error_ = 0;
        }
        return success;
    }

    bool get_option(int level, int optname, void* optval, socklen_t* optlen) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return false;
        }
        
        if (::getsockopt(fd_, level, optname, optval, optlen) != 0) {
            last_error_ = errno;
            return false;
        }
        
        last_error_ = 0;
        return true;
    }

    bool set_option(int level, int optname, const void* optval, socklen_t optlen) override {
        if (!is_valid()) {
            last_error_ = EBADF;
            return false;
        }
        
        if (::setsockopt(fd_, level, optname, optval, optlen) != 0) {
            last_error_ = errno;
            return false;
        }
        
        last_error_ = 0;
        return true;
    }

    int fd() const override { return fd_; }
    
    bool is_valid() const override { return fd_ >= 0; }
    
    NetAddress local_address() const override {
        if (!is_valid()) return NetAddress();
        
        struct sockaddr_in sa;
        socklen_t len = sizeof(sa);
        if (::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&sa), &len) != 0) {
            return NetAddress();
        }
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
        return NetAddress(ip, ntohs(sa.sin_port));
    }

    NetAddress peer_address() const override {
        if (!is_valid()) return NetAddress();
        
        struct sockaddr_in sa;
        socklen_t len = sizeof(sa);
        if (::getpeername(fd_, reinterpret_cast<struct sockaddr*>(&sa), &len) != 0) {
            return NetAddress();
        }
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
        return NetAddress(ip, ntohs(sa.sin_port));
    }

    SocketType type() const override { return type_; }

    void close() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        is_connected_ = false;
        last_error_ = 0;
    }

    int get_error() const override {
        if (last_error_ != 0) {
            return last_error_;
        }
        
        if (!is_valid()) return EBADF;
        
        int error = 0;
        socklen_t len = sizeof(error);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            return errno;
        }
        return error;
    }

    std::string error_string() const override {
        return std::strerror(get_error());
    }

private:
    int fd_;
    SocketType type_;
    bool is_connected_;
    mutable int last_error_;
};

std::unique_ptr<Socket> create_socket(SocketType type) {
    return std::make_unique<SocketImpl>(type);
}

std::unique_ptr<Socket> create_socket_from_fd(int fd, SocketType type) {
    return std::make_unique<SocketImpl>(fd, type);
}

} // namespace httpserver::net