// src/httpserver/core/connections/connection_state.cpp
#include "../../../../include/httpserver/core/tcp_connections/Connection/connection_state.hpp"

#include <string>
#include <sstream>

namespace httpserver::core {

// ============================================================================
// 枚举转字符串（用于日志/调试）
// ============================================================================

std::string to_string(ConnectionState state) {
    switch (state) {
        case ConnectionState::DISCONNECTED: return "DISCONNECTED";
        case ConnectionState::CONNECTING:   return "CONNECTING";
        case ConnectionState::CONNECTED:    return "CONNECTED";
        case ConnectionState::DISCONNECTING:return "DISCONNECTING";
        case ConnectionState::CLOSING:      return "CLOSING";
        case ConnectionState::ERROR:        return "ERROR";
        default:                            return "UNKNOWN";
    }
}

std::string to_string(ConnectionEvent event) {
    switch (event) {
        case ConnectionEvent::CONNECTED:      return "CONNECTED";
        case ConnectionEvent::DISCONNECTED:   return "DISCONNECTED";
        case ConnectionEvent::DATA_SENT:      return "DATA_SENT";
        case ConnectionEvent::DATA_RECEIVED:  return "DATA_RECEIVED";
        case ConnectionEvent::ERROR_OCCURRED: return "ERROR_OCCURRED";
        case ConnectionEvent::CLOSED:         return "CLOSED";
        default:                              return "UNKNOWN";
    }
}

std::string to_string(ConnectionError error) {
    switch (error) {
        case ConnectionError::NONE:               return "NONE";
        case ConnectionError::SOCKET_CREATE_FAILED: return "SOCKET_CREATE_FAILED";
        case ConnectionError::CONNECT_FAILED:     return "CONNECT_FAILED";
        case ConnectionError::SSL_HANDSHAKE_FAILED:return "SSL_HANDSHAKE_FAILED";
        case ConnectionError::TIMEOUT:            return "TIMEOUT";
        case ConnectionError::BUFFER_OVERFLOW:    return "BUFFER_OVERFLOW";
        case ConnectionError::CONNECTION_RESET:   return "CONNECTION_RESET";
        case ConnectionError::WOULD_BLOCK:        return "WOULD_BLOCK";
        default:                                  return "UNKNOWN";
    }
}

// ============================================================================
// 配置验证
// ============================================================================

bool validate_config(const ConnectionConfig& config) {
    // 缓冲区大小至少 1KB
    if (config.send_buffer_size < 1024 || config.recv_buffer_size < 1024) {
        return false;
    }
    // 超时时间不能为负数
    if (config.connect_timeout.count() < 0 ||
        config.send_timeout.count() < 0 ||
        config.recv_timeout.count() < 0 ||
        config.idle_timeout.count() < 0) {
        return false;
    }
    // 最大包大小合理范围
    if (config.max_packet_size < 1024 || config.max_packet_size > 16 * 1024 * 1024) {
        return false;
    }
    // SSL 相关：若启用 SSL，必须提供证书和私钥
    if (config.use_ssl) {
        if (config.ssl_cert_file.empty() || config.ssl_key_file.empty()) {
            return false;
        }
    }
    return true;
}

// 可选：输出配置到流（用于调试）
std::string to_string(const ConnectionConfig& config) {
    std::ostringstream oss;
    oss << "ConnectionConfig {\n"
        << "  send_buffer_size: " << config.send_buffer_size << "\n"
        << "  recv_buffer_size: " << config.recv_buffer_size << "\n"
        << "  connect_timeout: " << config.connect_timeout.count() << " ms\n"
        << "  send_timeout: " << config.send_timeout.count() << " ms\n"
        << "  recv_timeout: " << config.recv_timeout.count() << " ms\n"
        << "  idle_timeout: " << config.idle_timeout.count() << " ms\n"
        << "  no_delay: " << (config.no_delay ? "true" : "false") << "\n"
        << "  keep_alive: " << (config.keep_alive ? "true" : "false") << "\n"
        << "  reuse_address: " << (config.reuse_address ? "true" : "false") << "\n"
        << "  nonblocking: " << (config.nonblocking ? "true" : "false") << "\n"
        << "  use_ssl: " << (config.use_ssl ? "true" : "false") << "\n"
        << "  ssl_cert_file: " << config.ssl_cert_file << "\n"
        << "  ssl_key_file: " << config.ssl_key_file << "\n"
        << "  ssl_ca_file: " << config.ssl_ca_file << "\n"
        << "  max_retries: " << config.max_retries << "\n"
        << "  max_packet_size: " << config.max_packet_size << "\n"
        << "}";
    return oss.str();
}

} // namespace httpserver::core