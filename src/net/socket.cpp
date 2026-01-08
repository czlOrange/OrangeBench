#include "httpserver/net/socket.hpp"
#include "httpserver/net/address.hpp"
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <memory>

namespace httpserver::net {

static std::string get_socket_error() {
    return strerror(errno);
}

// ==================== 简化的Socket实现，仅用于测试 ====================

// 为测试目的，我们创建一个简单的Socket实现，使用现有的NetAddress接口
class SimpleSocketImpl : public Socket {
private:
    int fd_;
    SocketType type_;
    
public:
    SimpleSocketImpl(SocketType type) : fd_(-1), type_(type) {
        int domain = 0;
        int sock_type = 0;
        int protocol = 0;
        
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
                domain = AF_UNIX;
                sock_type = SOCK_STREAM;
                protocol = 0;
                break;
            case SocketType::UNIX_DGRAM:
                domain = AF_UNIX;
                sock_type = SOCK_DGRAM;
                protocol = 0;
                break;
        }
        
        fd_ = ::socket(domain, sock_type, protocol);
        if (fd_ < 0) {
            throw std::runtime_error("创建套接字失败: " + get_socket_error());
        }
    }
    
    SimpleSocketImpl(int fd, SocketType type) : fd_(fd), type_(type) {
        if (fd_ < 0) {
            throw std::invalid_argument("无效的文件描述符");
        }
    }
    
    ~SimpleSocketImpl() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    
    // ==================== 基础操作 ====================
    
    bool bind(const NetAddress& addr) override {
        // 对于Unix域套接字，我们暂时不支持
        if (type_ == SocketType::UNIX_STREAM || type_ == SocketType::UNIX_DGRAM) {
            errno = ENOTSUP; // 不支持的操作
            return false;
        }
        
        // 只支持IPv4地址绑定
        struct sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(addr.port());
        
        // 将IP地址字符串转换为网络字节序
        const char* ip_str = "0.0.0.0";
        try {
            ip_str = addr.ip().c_str();
        } catch (...) {
            ip_str = "0.0.0.0";
        }
        
        if (inet_pton(AF_INET, ip_str, &sa.sin_addr) <= 0) {
            sa.sin_addr.s_addr = INADDR_ANY;
        }
        
        return ::bind(fd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) == 0;
    }
    
    bool listen(int backlog) override {
        if (type_ != SocketType::TCP && type_ != SocketType::UNIX_STREAM) {
            errno = EOPNOTSUPP;
            return false;
        }
        return ::listen(fd_, backlog) == 0;
    }
    
    std::unique_ptr<Socket> accept(NetAddress* peer_addr) override {
        if (type_ != SocketType::TCP && type_ != SocketType::UNIX_STREAM) {
            errno = EOPNOTSUPP;
            return nullptr;
        }
        
        struct sockaddr_in sa{};
        socklen_t addrlen = sizeof(sa);
        int client_fd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&sa), &addrlen);
        
        if (client_fd < 0) {
            return nullptr;
        }
        
        // 如果调用者提供了peer_addr参数，填充对端地址
        if (peer_addr) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            *peer_addr = NetAddress(ip, ntohs(sa.sin_port));
        }
        
        return std::make_unique<SimpleSocketImpl>(client_fd, type_);
    }
    
    bool connect(const NetAddress& addr) override {
        // 只支持IPv4连接
        struct sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(addr.port());
        
        const char* ip_str = "127.0.0.1";
        try {
            ip_str = addr.ip().c_str();
        } catch (...) {
            ip_str = "127.0.0.1";
        }
        
        if (inet_pton(AF_INET, ip_str, &sa.sin_addr) <= 0) {
            errno = EINVAL;
            return false;
        }
        
        return ::connect(fd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) == 0;
    }
    
    // ==================== 数据读写 ====================
    
    ssize_t send(const void* data, size_t len) override {
        return ::send(fd_, data, len, 0);
    }
    
    ssize_t recv(void* buf, size_t len) override {
        return ::recv(fd_, buf, len, 0);
    }
    
    ssize_t sendto(const void* data, size_t len, const NetAddress& addr) override {
        // 只支持IPv4的sendto
        struct sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(addr.port());
        
        const char* ip_str = "127.0.0.1";
        try {
            ip_str = addr.ip().c_str();
        } catch (...) {
            ip_str = "127.0.0.1";
        }
        
        if (inet_pton(AF_INET, ip_str, &sa.sin_addr) <= 0) {
            errno = EINVAL;
            return -1;
        }
        
        return ::sendto(fd_, data, len, 0, 
                       reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));
    }
    
    ssize_t recvfrom(void* buf, size_t len, NetAddress* src_addr) override {
        struct sockaddr_in sa{};
        socklen_t addrlen = sizeof(sa);
        
        ssize_t n = ::recvfrom(fd_, buf, len, 0, 
                              reinterpret_cast<struct sockaddr*>(&sa), &addrlen);
        
        if (n >= 0 && src_addr) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            *src_addr = NetAddress(ip, ntohs(sa.sin_port));
        }
        
        return n;
    }
    
    // ==================== 配置参数 ====================
    
    bool set_nonblocking(bool nonblocking) override {
        int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags < 0) return false;
        
        if (nonblocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        
        return ::fcntl(fd_, F_SETFL, flags) == 0;
    }
    
    bool set_options(const SocketOptions& options) override {
        bool success = true;
        
        // SO_REUSEADDR
        int reuse_addr = options.reuse_addr ? 1 : 0;
        if (!set_option(SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr))) {
            success = false;
        }
        
        // TCP_NODELAY (仅TCP)
        if (type_ == SocketType::TCP) {
            int nodelay = options.tcp_no_delay ? 1 : 0;
            if (!set_option(IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay))) {
                success = false;
            }
        }
        
        // SO_KEEPALIVE
        int keep_alive = options.keep_alive ? 1 : 0;
        if (!set_option(SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive))) {
            success = false;
        }
        
        return success;
    }
    
    bool get_option(int level, int optname, void* optval, socklen_t* optlen) override {
        return ::getsockopt(fd_, level, optname, optval, optlen) == 0;
    }
    
    bool set_option(int level, int optname, const void* optval, socklen_t optlen) override {
        return ::setsockopt(fd_, level, optname, optval, optlen) == 0;
    }
    
    // ==================== 查询状态 ====================
    
    int fd() const override { return fd_; }
    
    bool is_valid() const override { return fd_ >= 0; }
    
    NetAddress local_address() const override {
        struct sockaddr_in sa{};
        socklen_t addrlen = sizeof(sa);
        
        if (::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&sa), &addrlen) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            return NetAddress(ip, ntohs(sa.sin_port));
        }
        
        return NetAddress(); // 返回默认地址
    }
    
    NetAddress peer_address() const override {
        struct sockaddr_in sa{};
        socklen_t addrlen = sizeof(sa);
        
        if (::getpeername(fd_, reinterpret_cast<struct sockaddr*>(&sa), &addrlen) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            return NetAddress(ip, ntohs(sa.sin_port));
        }
        
        return NetAddress(); // 返回默认地址
    }
    
    SocketType type() const override { return type_; }
    
    // ==================== 关闭和错误处理 ====================
    
    void close() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    
    int get_error() const override {
        int error = 0;
        socklen_t len = sizeof(error);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            return errno;
        }
        return error;
    }
    
    std::string error_string() const override {
        return get_socket_error();
    }
};

// ==================== 工厂函数实现 ====================

std::unique_ptr<Socket> create_socket(SocketType type) {
    return std::make_unique<SimpleSocketImpl>(type);
}

std::unique_ptr<Socket> create_socket_from_fd(int fd, SocketType type) {
    return std::make_unique<SimpleSocketImpl>(fd, type);
}

} // namespace httpserver::net