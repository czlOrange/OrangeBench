// include/httpserver/core/tcp_connections/IO/io_handler.hpp
#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <system_error>
#include <variant>
#include <optional>
#include <utility>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

namespace httpserver::core {

using SocketFd = int;

// Result 类型（兼容 C++17）
template<typename T>
class Result {
public:
    Result(T&& value) : data_(std::forward<T>(value)) {}
    Result(const T& value) : data_(value) {}
    Result(std::error_code ec) : data_(ec) {}
    
    bool has_value() const { return std::holds_alternative<T>(data_); }
    bool has_error() const { return std::holds_alternative<std::error_code>(data_); }
    
    T& value() & { return std::get<T>(data_); }
    const T& value() const& { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }
    
    std::error_code error() const { return std::get<std::error_code>(data_); }
    
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    
    explicit operator bool() const { return has_value(); }
    
private:
    std::variant<T, std::error_code> data_;
};

template<>
class Result<void> {
public:
    Result() = default;
    Result(std::error_code ec) : error_(ec) {}
    
    bool has_value() const { return !error_.has_value(); }
    bool has_error() const { return error_.has_value(); }
    
    void value() const {
        if (has_error()) throw std::system_error(error_.value());
    }
    
    std::error_code error() const { return error_.value_or(std::error_code{}); }
    
private:
    std::optional<std::error_code> error_;
};

template<typename T>
using IOResult = Result<T>;

// SocketAddress
struct SocketAddress {
    sockaddr_in addr = {};
    socklen_t len = sizeof(addr);
    
    static SocketAddress FromIpPort(const std::string& ip, uint16_t port);
    std::string ToString() const;
    std::string GetIp() const;
    uint16_t GetPort() const;
};

// IO 处理器接口
class IIOHandler {
public:
    virtual ~IIOHandler() = default;
    
    virtual IOResult<SocketFd> CreateSocket(int domain, int type, int protocol) = 0;
    virtual IOResult<void> CloseSocket(SocketFd fd) noexcept = 0;
    virtual IOResult<void> ShutdownSocket(SocketFd fd, int how) noexcept = 0;
    
    virtual IOResult<void> SetSocketOption(SocketFd fd, int level, int optname,
                                           const void* optval, socklen_t optlen) = 0;
    virtual IOResult<void> SetNonBlocking(SocketFd fd, bool enable) = 0;
    virtual IOResult<void> SetTcpNoDelay(SocketFd fd, bool enable) = 0;
    
    virtual IOResult<void> Connect(SocketFd fd, const sockaddr* addr, socklen_t addrlen) = 0;
    virtual IOResult<void> Bind(SocketFd fd, const sockaddr* addr, socklen_t addrlen) = 0;
    virtual IOResult<void> Listen(SocketFd fd, int backlog) = 0;
    virtual IOResult<SocketFd> Accept(SocketFd fd, sockaddr* addr, socklen_t* addrlen) = 0;
    
    virtual IOResult<ssize_t> Send(SocketFd fd, const void* buf, size_t len, int flags) = 0;
    virtual IOResult<ssize_t> Recv(SocketFd fd, void* buf, size_t len, int flags) = 0;
    virtual IOResult<ssize_t> SendTo(SocketFd fd, const void* buf, size_t len, int flags,
                                      const sockaddr* dest_addr, socklen_t addrlen) = 0;
    virtual IOResult<ssize_t> RecvFrom(SocketFd fd, void* buf, size_t len, int flags,
                                        sockaddr* src_addr, socklen_t* addrlen) = 0;
    
    virtual IOResult<bool> PollRead(SocketFd fd, int timeout_ms) = 0;
    virtual IOResult<bool> PollWrite(SocketFd fd, int timeout_ms) = 0;
    virtual IOResult<bool> PollError(SocketFd fd, int timeout_ms) = 0;
    
    virtual IOResult<SocketAddress> GetLocalAddress(SocketFd fd) = 0;
    virtual IOResult<SocketAddress> GetPeerAddress(SocketFd fd) = 0;
    
    virtual std::error_code GetLastSocketError() = 0;
    virtual std::string ErrorToString(int error_code) = 0;
    
    static std::shared_ptr<IIOHandler> CreateDefault();
};

} // namespace httpserver::core