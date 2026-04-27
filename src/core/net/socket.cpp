// src/net/socket.cpp
#include "httpserver/net/socket.hpp"
#include "httpserver/core/net/io_handler.hpp"
#include "httpserver/net/address.hpp"

#include <sys/socket.h>
#include <sys/types.h>
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

} // unnamed namespace

class SocketImpl : public Socket {
public:
    // 构造函数：主动创建 socket
    explicit SocketImpl(SocketType type, std::shared_ptr<core::IIOHandler> io_handler)
        : io_handler_(std::move(io_handler)), type_(type), fd_(-1), is_connected_(false) {
        int domain, socktype, protocol;
        socket_type_to_native(type_, domain, socktype, protocol);
        auto result = io_handler_->CreateSocket(domain, socktype, protocol);
        if (!result) {
            throw std::system_error(result.error(), "创建 socket 失败");
        }
        fd_ = result.value();
    }

    // 构造函数：从已有 fd 包装（常用于 accept）
    SocketImpl(int fd, SocketType type, std::shared_ptr<core::IIOHandler> io_handler)
        : io_handler_(std::move(io_handler)), type_(type), fd_(fd), is_connected_(false) {
        if (fd_ < 0) {
            throw std::invalid_argument("无效的文件描述符");
        }
        // 检查是否已连接
        auto result = io_handler_->GetPeerAddress(fd_);
        is_connected_ = result.has_value();
    }

    ~SocketImpl() override { close(); }

    // 禁用拷贝
    SocketImpl(const SocketImpl&) = delete;
    SocketImpl& operator=(const SocketImpl&) = delete;

    // 移动构造
    SocketImpl(SocketImpl&& other) noexcept
        : io_handler_(std::move(other.io_handler_))
        , type_(other.type_)
        , fd_(other.fd_)
        , is_connected_(other.is_connected_)
        , last_error_(other.last_error_) {
        other.fd_ = -1;
        other.is_connected_ = false;
        other.last_error_ = std::error_code();
    }

    // 移动赋值
    SocketImpl& operator=(SocketImpl&& other) noexcept {
        if (this != &other) {
            close();
            io_handler_ = std::move(other.io_handler_);
            type_ = other.type_;
            fd_ = other.fd_;
            is_connected_ = other.is_connected_;
            last_error_ = other.last_error_;
            other.fd_ = -1;
            other.is_connected_ = false;
            other.last_error_ = std::error_code();
        }
        return *this;
    }

    // ========== 基础操作 ==========
    bool bind(const NetAddress& addr) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return false;
        }
        struct sockaddr_in sa;
        if (!netaddress_to_sockaddr_in(addr, sa)) {
            last_error_ = std::error_code(errno, std::system_category());
            return false;
        }
        auto result = io_handler_->Bind(fd_, (sockaddr*)&sa, sizeof(sa));
        if (!result) {
            last_error_ = result.error();
            return false;
        }
        last_error_ = std::error_code();
        return true;
    }

    bool listen(int backlog) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return false;
        }
        if (type_ != SocketType::TCP) {
            last_error_ = std::error_code(EOPNOTSUPP, std::system_category());
            return false;
        }
        auto result = io_handler_->Listen(fd_, backlog);
        if (!result) {
            last_error_ = result.error();
            return false;
        }
        last_error_ = std::error_code();
        return true;
    }

    std::unique_ptr<Socket> accept(NetAddress* peer_addr) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return nullptr;
        }
        if (type_ != SocketType::TCP) {
            last_error_ = std::error_code(EOPNOTSUPP, std::system_category());
            return nullptr;
        }
        struct sockaddr_in sa;
        socklen_t len = sizeof(sa);
        auto result = io_handler_->Accept(fd_, (sockaddr*)&sa, &len);
        if (!result) {
            last_error_ = result.error();
            return nullptr;
        }
        int client_fd = result.value();
        if (peer_addr) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            *peer_addr = NetAddress(ip, ntohs(sa.sin_port));
        }
        last_error_ = std::error_code();
        return std::make_unique<SocketImpl>(client_fd, type_, io_handler_);
    }

    bool connect(const NetAddress& addr) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return false;
        }
        struct sockaddr_in sa;
        if (!netaddress_to_sockaddr_in(addr, sa)) {
            last_error_ = std::error_code(errno, std::system_category());
            return false;
        }
        auto result = io_handler_->Connect(fd_, (sockaddr*)&sa, sizeof(sa));
        if (!result) {
            last_error_ = result.error();
            if (last_error_ == std::error_code(EINPROGRESS, std::system_category())) {
                return true;  // 非阻塞模式
            }
            return false;
        }
        is_connected_ = true;
        last_error_ = std::error_code();
        return true;
    }

    // ========== 数据读写 ==========
    ssize_t send(const void* data, size_t len) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return -1;
        }
        auto result = io_handler_->Send(fd_, data, len, 0);
        if (!result) {
            last_error_ = result.error();
            return -1;
        }
        last_error_ = std::error_code();
        return result.value();
    }

    ssize_t recv(void* buf, size_t len) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return -1;
        }
        auto result = io_handler_->Recv(fd_, buf, len, 0);
        if (!result) {
            last_error_ = result.error();
            return -1;
        }
        last_error_ = std::error_code();
        return result.value();
    }

    ssize_t sendto(const void* data, size_t len, const NetAddress& addr) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return -1;
        }
        if (type_ != SocketType::UDP) {
            last_error_ = std::error_code(EOPNOTSUPP, std::system_category());
            return -1;
        }
        struct sockaddr_in sa;
        if (!netaddress_to_sockaddr_in(addr, sa)) {
            last_error_ = std::error_code(errno, std::system_category());
            return -1;
        }
        auto result = io_handler_->SendTo(fd_, data, len, 0, (sockaddr*)&sa, sizeof(sa));
        if (!result) {
            last_error_ = result.error();
            return -1;
        }
        last_error_ = std::error_code();
        return result.value();
    }

    ssize_t recvfrom(void* buf, size_t len, NetAddress* src_addr) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return -1;
        }
        if (type_ != SocketType::UDP) {
            last_error_ = std::error_code(EOPNOTSUPP, std::system_category());
            return -1;
        }
        struct sockaddr_in sa;
        socklen_t addrlen = sizeof(sa);
        auto result = io_handler_->RecvFrom(fd_, buf, len, 0, (sockaddr*)&sa, &addrlen);
        if (!result) {
            last_error_ = result.error();
            return -1;
        }
        ssize_t n = result.value();
        if (src_addr && n >= 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            *src_addr = NetAddress(ip, ntohs(sa.sin_port));
        }
        last_error_ = std::error_code();
        return n;
    }

    // ========== 配置参数 ==========
    bool set_nonblocking(bool nonblocking) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return false;
        }
        auto result = io_handler_->SetNonBlocking(fd_, nonblocking);
        if (!result) {
            last_error_ = result.error();
            return false;
        }
        last_error_ = std::error_code();
        return true;
    }

    bool set_options(const SocketOptions& options) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return false;
        }
        bool success = true;

        // SO_REUSEADDR
        if (!io_handler_->SetSocketOption(fd_, SOL_SOCKET, SO_REUSEADDR,
                                          &options.reuse_addr, sizeof(options.reuse_addr))) {
            success = false;
        }
#ifdef SO_REUSEPORT
        if (!io_handler_->SetSocketOption(fd_, SOL_SOCKET, SO_REUSEPORT,
                                          &options.reuse_port, sizeof(options.reuse_port))) {
            success = false;
        }
#endif
        if (type_ == SocketType::TCP) {
            if (!io_handler_->SetTcpNoDelay(fd_, options.tcp_no_delay)) {
                success = false;
            }
        }
        if (!io_handler_->SetSocketOption(fd_, SOL_SOCKET, SO_KEEPALIVE,
                                          &options.keep_alive, sizeof(options.keep_alive))) {
            success = false;
        }
        if (options.recv_timeout_ms > 0) {
            struct timeval tv;
            tv.tv_sec = options.recv_timeout_ms / 1000;
            tv.tv_usec = (options.recv_timeout_ms % 1000) * 1000;
            if (!io_handler_->SetSocketOption(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) {
                success = false;
            }
        }
        if (options.send_timeout_ms > 0) {
            struct timeval tv;
            tv.tv_sec = options.send_timeout_ms / 1000;
            tv.tv_usec = (options.send_timeout_ms % 1000) * 1000;
            if (!io_handler_->SetSocketOption(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv))) {
                success = false;
            }
        }
        if (options.recv_buffer_size > 0) {
            if (!io_handler_->SetSocketOption(fd_, SOL_SOCKET, SO_RCVBUF,
                                              &options.recv_buffer_size, sizeof(options.recv_buffer_size))) {
                success = false;
            }
        }
        if (options.send_buffer_size > 0) {
            if (!io_handler_->SetSocketOption(fd_, SOL_SOCKET, SO_SNDBUF,
                                              &options.send_buffer_size, sizeof(options.send_buffer_size))) {
                success = false;
            }
        }

        if (success) {
            last_error_ = std::error_code();
        } else {
            last_error_ = io_handler_->GetLastSocketError();
        }
        return success;
    }

    bool get_option(int level, int optname, void* optval, socklen_t* optlen) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return false;
        }
        auto result = io_handler_->GetSocketOption(fd_, level, optname, optval, optlen);
        if (!result) {
            last_error_ = result.error();
            return false;
        }
        last_error_ = std::error_code();
        return true;
    }

    bool set_option(int level, int optname, const void* optval, socklen_t optlen) override {
        if (!is_valid()) {
            last_error_ = std::error_code(EBADF, std::system_category());
            return false;
        }
        auto result = io_handler_->SetSocketOption(fd_, level, optname, optval, optlen);
        if (!result) {
            last_error_ = result.error();
            return false;
        }
        last_error_ = std::error_code();
        return true;
    }

    // ========== 状态查询 ==========
    int fd() const override { return fd_; }
    bool is_valid() const override { return fd_ >= 0; }

    NetAddress local_address() const override {
        if (!is_valid()) return NetAddress();
        auto result = io_handler_->GetLocalAddress(fd_);
        if (!result) return NetAddress();
        const auto& sa = result.value();
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa.addr.sin_addr, ip, sizeof(ip));
        return NetAddress(ip, ntohs(sa.addr.sin_port));
    }

    NetAddress peer_address() const override {
        if (!is_valid()) return NetAddress();
        auto result = io_handler_->GetPeerAddress(fd_);
        if (!result) return NetAddress();
        const auto& sa = result.value();
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa.addr.sin_addr, ip, sizeof(ip));
        return NetAddress(ip, ntohs(sa.addr.sin_port));
    }

    SocketType type() const override { return type_; }

    void close() override {
        if (fd_ >= 0) {
            io_handler_->CloseSocket(fd_);
            fd_ = -1;
            is_connected_ = false;
            last_error_ = std::error_code();
        }
    }

    int get_error() const override {
        if (last_error_.value() != 0) {
            return last_error_.value();
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
    std::shared_ptr<core::IIOHandler> io_handler_;
    SocketType type_;
    int fd_;
    bool is_connected_;
    mutable std::error_code last_error_;
};

// ========== 工厂函数 ==========
std::unique_ptr<Socket> create_socket(SocketType type) {
    auto io_handler = core::IIOHandler::CreateDefault();
    return std::make_unique<SocketImpl>(type, std::move(io_handler));
}

std::unique_ptr<Socket> create_socket_from_fd(int fd, SocketType type) {
    auto io_handler = core::IIOHandler::CreateDefault();
    return std::make_unique<SocketImpl>(fd, type, std::move(io_handler));
}

} // namespace httpserver::net