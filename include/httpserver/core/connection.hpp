// include/httpserver/core/connection.hpp
#pragma once

#include "../net/socket.hpp"
#include "../net/buffer.hpp"
#include <functional>
#include <memory>

namespace httpserver::core {

// 连接事件
enum class ConnectionEvent {
    CONNECTED,      // 连接建立
    DATA_RECEIVED,  // 收到数据
    DATA_SENT,      // 数据发送完成
    DISCONNECTED,   // 连接断开
    ERROR_OCCURRED  // 发生错误
};

// 连接状态
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

/**
 * @brief 连接抽象类
 * 
 * 管理单个网络连接的生命周期，提供高级接口
 */
class Connection : public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;
    using Callback = std::function<void(ConnectionEvent, const Connection::Ptr&, const std::string&)>;
    
    // 创建连接
    static Ptr create(std::unique_ptr<net::Socket> socket);
    static Ptr create_tcp(const std::string& ip, uint16_t port);
    
    virtual ~Connection() = default;
    
    // 连接管理
    virtual bool connect();
    virtual void disconnect();
    virtual void close();
    
    // 数据发送（多种方式）
    virtual size_t send(const void* data, size_t len);
    virtual size_t send(const std::string& data);
    virtual void send_async(const void* data, size_t len);
    virtual void send_async(const std::string& data);
    
    // 数据接收
    virtual size_t recv(void* buf, size_t len);
    virtual std::string recv_string(size_t max_len = 4096);
    
    // 缓冲区操作
    virtual net::RingBuffer& input_buffer() = 0;
    virtual net::RingBuffer& output_buffer() = 0;
    
    // 配置
    virtual void set_nonblocking(bool nonblocking);
    virtual void set_keep_alive(bool keep_alive);
    virtual void set_no_delay(bool no_delay);
    virtual void set_timeout(int recv_timeout_ms, int send_timeout_ms);
    
    // 事件回调
    virtual void set_event_callback(Callback callback);
    
    // 状态查询
    virtual int fd() const;
    virtual ConnectionState state() const;
    virtual bool is_connected() const;
    virtual bool is_disconnected() const;
    virtual std::string local_address() const;
    virtual std::string peer_address() const;
    
    // 统计信息
    virtual size_t bytes_received() const;
    virtual size_t bytes_sent() const;
    virtual size_t pending_bytes() const;  // 待发送字节数
    
protected:
    Connection(std::unique_ptr<net::Socket> socket);
    
    // 子类需要实现的接口
    virtual void on_readable() = 0;    // 可读时调用
    virtual void on_writable() = 0;    // 可写时调用
    virtual void on_error(int error) = 0; // 错误时调用
    
private:
    std::unique_ptr<net::Socket> socket_;
    Callback event_callback_;
    ConnectionState state_{ConnectionState::DISCONNECTED};
    size_t bytes_received_{0};
    size_t bytes_sent_{0};
};

} // namespace httpserver::core