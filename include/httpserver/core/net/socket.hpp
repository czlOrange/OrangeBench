// include/httpserver/net/socket.hpp
#pragma once

#include "address.hpp"
#include <memory>
#include <functional>

namespace httpserver::net {

// Socket类型
enum class SocketType {
    TCP,             // 网络型面向连接的可靠字节流套接字（基于TCP协议）
    UDP,             // 网络型无连接的不可靠数据报套接字（基于UDP协议）
    UNIX_STREAM,     // Unix域面向连接的可靠字节流套接字（本地进程通信）
    UNIX_DGRAM       // Unix域无连接的不可靠数据报套接字（本地进程通信）
};

// Socket选项
struct SocketOptions {
    bool reuse_addr = true;      // SO_REUSEADDR：允许复用本地地址（解决TIME_WAIT端口占用）
    bool reuse_port = false;     // SO_REUSEPORT：允许多个进程/线程绑定同一端口（提高并发）
    bool tcp_no_delay = true;    // TCP_NODELAY：禁用Nagle算法（降低TCP传输延迟）
    bool keep_alive = true;      // SO_KEEPALIVE：开启TCP保活（检测对端是否断开连接）
    int recv_timeout_ms = 0;     // SO_RCVTIMEO：接收数据超时时间（毫秒，0表示无超时）
    int send_timeout_ms = 0;     // SO_SNDTIMEO：发送数据超时时间（毫秒，0表示无超时）
    int recv_buffer_size = 0;    // SO_RCVBUF：接收缓冲区大小（0使用系统默认）
    int send_buffer_size = 0;    // SO_SNDBUF：发送缓冲区大小（0使用系统默认）
};

/**
 * @brief Socket基础抽象类
 * 
 * 提供跨平台的Socket操作接口，便于后续支持不同平台
 */
class Socket {
public:
    // 
    virtual ~Socket() = default;
    
    // 基础操作
    virtual bool bind(const NetAddress& addr) = 0;
    virtual bool listen(int backlog = 1024) = 0;
    virtual std::unique_ptr<Socket> accept(NetAddress* peer_addr = nullptr) = 0;
    virtual bool connect(const NetAddress& addr) = 0;
    
    // 数据读写
    virtual ssize_t send(const void* data, size_t len) = 0;
    virtual ssize_t recv(void* buf, size_t len) = 0;
    virtual ssize_t sendto(const void* data, size_t len, const NetAddress& addr) = 0;
    virtual ssize_t recvfrom(void* buf, size_t len, NetAddress* src_addr = nullptr) = 0;
    
    // 配置参数
    virtual bool set_nonblocking(bool nonblocking) = 0;
    virtual bool set_options(const SocketOptions& options) = 0;
    virtual bool get_option(int level, int optname, void* optval, socklen_t* optlen) = 0;
    virtual bool set_option(int level, int optname, const void* optval, socklen_t optlen) = 0;
    
    // 查询通信中C/S连接的套接字状态
    virtual int fd() const = 0;                     // 获取套接字对应的文件描述符（唯一标识）
    virtual bool is_valid() const = 0;              // 检查套接字是否有效（fd合法且未关闭）
    virtual NetAddress local_address() const = 0;   // 获取套接字绑定的本地地址（IP+端口/本地路径）
    virtual NetAddress peer_address() const = 0;    // 获取套接字连接的对端地址（仅连接态有效）
    virtual SocketType type() const = 0;            // 获取套接字类型（TCP/UDP/UNIX_STREAM等）
    
    // 关闭
    virtual void close() = 0;
    
    // 错误处理
    virtual int get_error() const = 0;
    virtual std::string error_string() const = 0;
};

// 工厂函数(屏蔽子类细节、统一创建接口)(纯虚函数接口文件无法直接类的对象,因此用工厂模式,不需要关心底层是谁用什么方式实现的)
std::unique_ptr<Socket> create_socket(SocketType type);
std::unique_ptr<Socket> create_socket_from_fd(int fd, SocketType type);

} // namespace httpserver::net