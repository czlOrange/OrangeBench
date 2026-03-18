// src/httpserver/core/connections/connection_state.hpp
#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace httpserver::core {

// 连接状态枚举
enum class ConnectionState : uint8_t {
    DISCONNECTED = 0,    // 未连接
    CONNECTING,          // 连接中
    CONNECTED,           // 已连接
    DISCONNECTING,       // 断开中
    CLOSING,             // 关闭中
    ERROR                // 错误状态
};

// 连接事件枚举
enum class ConnectionEvent : uint8_t {
    CONNECTED = 0,       // 连接建立
    DISCONNECTED,        // 连接断开
    DATA_SENT,           // 数据发送完成
    DATA_RECEIVED,       // 数据接收完成
    ERROR_OCCURRED,      // 发生错误
    CLOSED               // 连接关闭
};

// 连接配置
struct ConnectionConfig {
    // 缓冲区设置
    size_t send_buffer_size = 8192;
    size_t recv_buffer_size = 8192;
    
    // 超时设置
    std::chrono::milliseconds connect_timeout{3000};
    std::chrono::milliseconds send_timeout{5000};
    std::chrono::milliseconds recv_timeout{10000};
    std::chrono::milliseconds idle_timeout{30000};
    
    // TCP选项
    bool no_delay = true;        // TCP_NODELAY
    bool keep_alive = true;      // SO_KEEPALIVE
    bool reuse_address = true;   // SO_REUSEADDR
    bool nonblocking = true;     // 非阻塞模式
    
    // SSL选项
    bool use_ssl = false;
    std::string ssl_cert_file;
    std::string ssl_key_file;
    std::string ssl_ca_file;
    
    // 其他选项
    int max_retries = 3;
    size_t max_packet_size = 65536;
};

// 错误码增强
enum class ConnectionError {
    NONE = 0,
    SOCKET_CREATE_FAILED,
    CONNECT_FAILED,
    SSL_HANDSHAKE_FAILED,
    TIMEOUT,
    BUFFER_OVERFLOW,
    CONNECTION_RESET,
    WOULD_BLOCK
};

} // namespace httpserver::core