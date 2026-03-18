// src/httpserver/core/connections/io_handler.hpp
#pragma once

#include "./Connection/connection_state.hpp"
#include <memory>
#include <functional>
#include <system_error>

namespace httpserver::core {

// 文件描述符类型
using SocketFd = int;

// Socket地址包装
struct SocketAddress {
    // IPv4 socket地址结构，包含IP和端口（网络字节序）
    sockaddr_in addr;  
    
    // 地址结构的长度，通常为sizeof(sockaddr_in)
    socklen_t len;     
    
    // 从IP字符串和端口号创建SocketAddress对象（自动处理字节序转换）
    static SocketAddress FromIpPort(const std::string& ip, uint16_t port);
    
    // 将地址转换为"IP:Port"格式的字符串（如"192.168.1.1:8080"）
    std::string ToString() const;
    
    // 获取IP地址字符串（如"192.168.1.1"）
    std::string GetIp() const;
    
    // 获取端口号（返回主机字节序）
    uint16_t GetPort() const;
};

// IO操作结果
template<typename T>
using IOResult = std::expected<T, std::error_code>;

// IO处理器抽象接口
class IIOHandler {
public:
    virtual ~IIOHandler() = default;
    
    // Socket创建与管理
    virtual IOResult<SocketFd> CreateSocket(int domain, int type, int protocol) = 0;
    virtual IOResult<void> CloseSocket(SocketFd fd) noexcept = 0;
    virtual IOResult<void> ShutdownSocket(SocketFd fd, int how) noexcept = 0;
    
    // Socket选项设置
    virtual IOResult<void> SetSocketOption(SocketFd fd, int level, int optname, 
                                          const void* optval, socklen_t optlen) = 0;
    virtual IOResult<void> SetNonBlocking(SocketFd fd, bool enable) = 0;
    virtual IOResult<void> SetTcpNoDelay(SocketFd fd, bool enable) = 0;
    
    // 连接操作
    virtual IOResult<void> Connect(SocketFd fd, const sockaddr* addr, socklen_t addrlen) = 0;
    virtual IOResult<void> Bind(SocketFd fd, const sockaddr* addr, socklen_t addrlen) = 0;
    virtual IOResult<void> Listen(SocketFd fd, int backlog) = 0;
    virtual IOResult<SocketFd> Accept(SocketFd fd, sockaddr* addr, socklen_t* addrlen) = 0;
    
    // 数据收发
    virtual IOResult<ssize_t> Send(SocketFd fd, const void* buf, size_t len, int flags) = 0;
    virtual IOResult<ssize_t> Recv(SocketFd fd, void* buf, size_t len, int flags) = 0;
    virtual IOResult<ssize_t> SendTo(SocketFd fd, const void* buf, size_t len, int flags,
                                    const sockaddr* dest_addr, socklen_t addrlen) = 0;
    virtual IOResult<ssize_t> RecvFrom(SocketFd fd, void* buf, size_t len, int flags,
                                      sockaddr* src_addr, socklen_t* addrlen) = 0;
    
    // IO多路复用
    virtual IOResult<bool> PollRead(SocketFd fd, int timeout_ms) = 0;
    virtual IOResult<bool> PollWrite(SocketFd fd, int timeout_ms) = 0;
    virtual IOResult<bool> PollError(SocketFd fd, int timeout_ms) = 0;
    
    // 地址信息
    virtual IOResult<SocketAddress> GetLocalAddress(SocketFd fd) = 0;
    virtual IOResult<SocketAddress> GetPeerAddress(SocketFd fd) = 0;
    
    // 错误处理
    virtual std::error_code GetLastSocketError() = 0;
    virtual std::string ErrorToString(int error_code) = 0;
    
    // 工厂方法
    static std::shared_ptr<IIOHandler> CreateDefault();
};

// POSIX系统IO处理器
class PosixIOHandler : public IIOHandler {
public:
    IOResult<SocketFd> CreateSocket(int domain, int type, int protocol) override;
    IOResult<void> CloseSocket(SocketFd fd) noexcept override;
    // ... 实现所有虚函数
};

} // namespace httpserver::core