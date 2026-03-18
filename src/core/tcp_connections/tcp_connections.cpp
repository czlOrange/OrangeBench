// src/httpserver/core/connections/tcp_connection.cpp
#include "connection.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <stdexcept>
#include <iostream>

#ifdef HTTPSERVER_SSL_SUPPORT
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace httpserver::core {

class TCPConnection : public IConnection {
private:
    // 内部状态
    int socket_fd_{-1};
    ConnectionState state_{ConnectionState::DISCONNECTED};
    ConnectionConfig config_;
    
    // SSL上下文（如果启用）
#ifdef HTTPSERVER_SSL_SUPPORT
    SSL_CTX* ssl_ctx_{nullptr};
    SSL* ssl_{nullptr};
#endif
    
    // 统计信息
    size_t bytes_sent_{0};
    size_t bytes_received_{0};
    
    // 事件回调
    EventCallback event_callback_;
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    
    // 地址信息
    sockaddr_in local_addr_{};
    sockaddr_in peer_addr_{};
    
    // 缓冲区
    std::vector<char> send_buffer_;
    std::vector<char> recv_buffer_;
    
    // 私有方法
    bool setup_socket();
    bool set_nonblocking(bool enable);
    bool set_tcp_nodelay(bool enable);
    std::error_code handle_socket_error();
    void notify_event(ConnectionEvent event);
    
#ifdef HTTPSERVER_SSL_SUPPORT
    bool init_ssl();
    void cleanup_ssl();
#endif

public:
    TCPConnection();
    explicit TCPConnection(int existing_fd, const sockaddr_in& peer_addr);
    ~TCPConnection() override;
    
    // 禁用拷贝
    TCPConnection(const TCPConnection&) = delete;
    TCPConnection& operator=(const TCPConnection&) = delete;
    
    // 连接管理
    std::error_code Connect() override;
    std::error_code Disconnect() noexcept override;
    void Close() noexcept override;
    
    // 数据发送（同步）
    AsyncResult<size_t> Send(std::string_view data) override;
    AsyncResult<size_t> Send(const void* data, size_t len) override;
    
    // 数据接收（同步）
    AsyncResult<std::string> Recv(size_t max_len = 4096) override;
    AsyncResult<size_t> Recv(void* buffer, size_t len) override;
    
    // 数据发送（异步-回调）
    void SendAsync(std::string_view data,
                  std::function<void(std::error_code, size_t)> callback) override;
    
    // 数据接收（异步-回调）
    void RecvAsync(size_t max_len,
                  std::function<void(std::error_code, std::string)> callback) override;
    
    // 缓冲区管理
    std::error_code Flush() override;
    void ClearBuffers() noexcept override;
    
    // 配置管理
    void Configure(const ConnectionConfig& config) override;
    ConnectionConfig GetConfig() const override;
    
    // 状态查询
    ConnectionState GetState() const override;
    bool IsConnected() const override;
    bool IsReadable() const override;
    bool IsWritable() const override;
    
    // 地址信息
    std::string GetLocalAddress() const override;
    std::string GetPeerAddress() const override;
    uint16_t GetLocalPort() const noexcept override;
    uint16_t GetPeerPort() const noexcept override;
    
    // 统计信息
    size_t GetBytesSent() const noexcept override;
    size_t GetBytesReceived() const noexcept override;
    size_t GetPendingSendBytes() const noexcept override;
    size_t GetPendingReceiveBytes() const noexcept override;
    
    // 超时等待
    bool WaitForData(std::chrono::milliseconds timeout) override;
    bool WaitForWritable(std::chrono::milliseconds timeout) override;
    
    // 获取文件描述符
    int GetFd() const override;
    
    // 事件回调设置
    void SetEventCallback(EventCallback callback) override;
    void SetDataCallback(DataCallback callback) override;
    void SetErrorCallback(ErrorCallback callback) override;
};

// TCPConnection 实现
TCPConnection::TCPConnection() {
    send_buffer_.reserve(config_.send_buffer_size);
    recv_buffer_.reserve(config_.recv_buffer_size);
}

TCPConnection::TCPConnection(int existing_fd, const sockaddr_in& peer_addr)
    : socket_fd_(existing_fd), peer_addr_(peer_addr) {
    
    socklen_t addr_len = sizeof(local_addr_);
    getsockname(socket_fd_, (sockaddr*)&local_addr_, &addr_len);
    
    set_nonblocking(config_.nonblocking);
    set_tcp_nodelay(config_.no_delay);
    
    state_ = ConnectionState::CONNECTED;
    notify_event(ConnectionEvent::CONNECTED);
}

TCPConnection::~TCPConnection() {
    Close();
}

bool TCPConnection::setup_socket() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    int opt = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    return set_nonblocking(config_.nonblocking) && 
           set_tcp_nodelay(config_.no_delay);
}

bool TCPConnection::set_nonblocking(bool enable) {
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags < 0) return false;
    
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    return fcntl(socket_fd_, F_SETFL, flags) == 0;
}

bool TCPConnection::set_tcp_nodelay(bool enable) {
    int opt = enable ? 1 : 0;
    return setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == 0;
}

std::error_code TCPConnection::Connect() {
    if (state_ != ConnectionState::DISCONNECTED) {
        return std::make_error_code(std::errc::already_connected);
    }
    
    if (!setup_socket()) {
        return handle_socket_error();
    }
    
    state_ = ConnectionState::CONNECTING;
    
    // 这里假设已经设置了peer_addr_
    if (connect(socket_fd_, (sockaddr*)&peer_addr_, sizeof(peer_addr_)) < 0) {
        if (errno != EINPROGRESS) {
            return handle_socket_error();
        }
    }
    
    // 对于非阻塞socket，需要等待连接完成
    if (config_.nonblocking) {
        pollfd pfd{socket_fd_, POLLOUT, 0};
        if (poll(&pfd, 1, config_.recv_timeout.count()) <= 0) {
            return std::make_error_code(std::errc::timed_out);
        }
        
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error != 0) {
            return std::error_code(error, std::system_category());
        }
    }
    
    state_ = ConnectionState::CONNECTED;
    notify_event(ConnectionEvent::CONNECTED);
    
    return std::error_code{};
}

std::error_code TCPConnection::Disconnect() noexcept {
    if (state_ != ConnectionState::CONNECTED) {
        return std::make_error_code(std::errc::not_connected);
    }
    
    state_ = ConnectionState::DISCONNECTING;
    shutdown(socket_fd_, SHUT_RDWR);
    
    state_ = ConnectionState::DISCONNECTED;
    notify_event(ConnectionEvent::DISCONNECTED);
    
    return std::error_code{};
}

void TCPConnection::Close() noexcept {
    if (socket_fd_ >= 0) {
#ifdef HTTPSERVER_SSL_SUPPORT
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
#endif
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    if (state_ != ConnectionState::DISCONNECTED) {
        state_ = ConnectionState::DISCONNECTED;
        notify_event(ConnectionEvent::DISCONNECTED);
    }
    
#ifdef HTTPSERVER_SSL_SUPPORT
    cleanup_ssl();
#endif
}

AsyncResult<size_t> TCPConnection::Send(std::string_view data) {
    return Send(data.data(), data.size());
}

AsyncResult<size_t> TCPConnection::Send(const void* data, size_t len) {
    if (state_ != ConnectionState::CONNECTED) {
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    }
    
    ssize_t sent = 0;
#ifdef HTTPSERVER_SSL_SUPPORT
    if (config_.use_ssl && ssl_) {
        sent = SSL_write(ssl_, data, len);
        if (sent <= 0) {
            int err = SSL_get_error(ssl_, sent);
            return std::unexpected(std::error_code(err, std::system_category()));
        }
    } else 
#endif
    {
        sent = send(socket_fd_, data, len, 0);
        if (sent < 0) {
            return std::unexpected(handle_socket_error());
        }
    }
    
    bytes_sent_ += sent;
    notify_event(ConnectionEvent::DATA_SENT);
    
    return sent;
}

AsyncResult<std::string> TCPConnection::Recv(size_t max_len) {
    std::string buffer;
    buffer.resize(max_len);
    
    auto result = Recv(buffer.data(), buffer.size());
    if (!result) {
        return std::unexpected(result.error());
    }
    
    buffer.resize(*result);
    return buffer;
}

AsyncResult<size_t> TCPConnection::Recv(void* buffer, size_t len) {
    if (state_ != ConnectionState::CONNECTED) {
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    }
    
    ssize_t received = 0;
#ifdef HTTPSERVER_SSL_SUPPORT
    if (config_.use_ssl && ssl_) {
        received = SSL_read(ssl_, buffer, len);
        if (received <= 0) {
            int err = SSL_get_error(ssl_, received);
            return std::unexpected(std::error_code(err, std::system_category()));
        }
    } else 
#endif
    {
        received = recv(socket_fd_, buffer, len, 0);
        if (received < 0) {
            return std::unexpected(handle_socket_error());
        } else if (received == 0) {
            // 连接关闭
            state_ = ConnectionState::CLOSING;
            return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        }
    }
    
    bytes_received_ += received;
    notify_event(ConnectionEvent::DATA_RECEIVED);
    
    if (data_callback_) {
        data_callback_(shared_from_this(), 
                      std::string_view(static_cast<const char*>(buffer), received));
    }
    
    return received;
}

void TCPConnection::SendAsync(std::string_view data,
                             std::function<void(std::error_code, size_t)> callback) {
    // 在实际实现中，这里应该将任务提交到I/O线程池或事件循环
    std::thread([self = shared_from_this(), data = std::string(data), callback]() {
        auto result = self->Send(data);
        if (callback) {
            if (result) {
                callback(std::error_code{}, *result);
            } else {
                callback(result.error(), 0);
            }
        }
    }).detach();
}

void TCPConnection::RecvAsync(size_t max_len,
                             std::function<void(std::error_code, std::string)> callback) {
    std::thread([self = shared_from_this(), max_len, callback]() {
        auto result = self->Recv(max_len);
        if (callback) {
            if (result) {
                callback(std::error_code{}, *result);
            } else {
                callback(result.error(), std::string{});
            }
        }
    }).detach();
}

std::error_code TCPConnection::Flush() {
    // 对于TCP socket，数据通常立即发送
    // 但对于缓冲区中的数据，可能需要特殊处理
    return std::error_code{};
}

void TCPConnection::ClearBuffers() noexcept {
    send_buffer_.clear();
    recv_buffer_.clear();
}

void TCPConnection::Configure(const ConnectionConfig& config) {
    config_ = config;
    send_buffer_.reserve(config_.send_buffer_size);
    recv_buffer_.reserve(config_.recv_buffer_size);
    
    if (socket_fd_ >= 0) {
        set_nonblocking(config_.nonblocking);
        set_tcp_nodelay(config_.no_delay);
    }
}

ConnectionConfig TCPConnection::GetConfig() const {
    return config_;
}

ConnectionState TCPConnection::GetState() const {
    return state_;
}

bool TCPConnection::IsConnected() const {
    return state_ == ConnectionState::CONNECTED;
}

bool TCPConnection::IsReadable() const {
    if (socket_fd_ < 0) return false;
    
    pollfd pfd{socket_fd_, POLLIN, 0};
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}

bool TCPConnection::IsWritable() const {
    if (socket_fd_ < 0) return false;
    
    pollfd pfd{socket_fd_, POLLOUT, 0};
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLOUT);
}

std::string TCPConnection::GetLocalAddress() const {
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr_.sin_addr, buffer, sizeof(buffer));
    return buffer;
}

std::string TCPConnection::GetPeerAddress() const {
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr_.sin_addr, buffer, sizeof(buffer));
    return buffer;
}

uint16_t TCPConnection::GetLocalPort() const noexcept {
    return ntohs(local_addr_.sin_port);
}

uint16_t TCPConnection::GetPeerPort() const noexcept {
    return ntohs(peer_addr_.sin_port);
}

size_t TCPConnection::GetBytesSent() const noexcept {
    return bytes_sent_;
}

size_t TCPConnection::GetBytesReceived() const noexcept {
    return bytes_received_;
}

size_t TCPConnection::GetPendingSendBytes() const noexcept {
    return send_buffer_.size();
}

size_t TCPConnection::GetPendingReceiveBytes() const noexcept {
    return recv_buffer_.size();
}

bool TCPConnection::WaitForData(std::chrono::milliseconds timeout) {
    if (socket_fd_ < 0) return false;
    
    pollfd pfd{socket_fd_, POLLIN, 0};
    int result = poll(&pfd, 1, timeout.count());
    
    return result > 0 && (pfd.revents & POLLIN);
}

bool TCPConnection::WaitForWritable(std::chrono::milliseconds timeout) {
    if (socket_fd_ < 0) return false;
    
    pollfd pfd{socket_fd_, POLLOUT, 0};
    int result = poll(&pfd, 1, timeout.count());
    
    return result > 0 && (pfd.revents & POLLOUT);
}

int TCPConnection::GetFd() const {
    return socket_fd_;
}

void TCPConnection::SetEventCallback(EventCallback callback) {
    event_callback_ = std::move(callback);
}

void TCPConnection::SetDataCallback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

void TCPConnection::SetErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

std::error_code TCPConnection::handle_socket_error() {
    std::error_code ec(errno, std::system_category());
    
    if (error_callback_) {
        error_callback_(shared_from_this(), ec);
    }
    
    notify_event(ConnectionEvent::ERROR_OCCURRED);
    return ec;
}

void TCPConnection::notify_event(ConnectionEvent event) {
    if (event_callback_) {
        event_callback_(event, shared_from_this());
    }
}

// 工厂函数
std::shared_ptr<IConnection> CreateTCPConnection() {
    return std::make_shared<TCPConnection>();
}

std::shared_ptr<IConnection> CreateTCPConnection(int fd, const sockaddr_in& peer_addr) {
    return std::make_shared<TCPConnection>(fd, peer_addr);
}

} // namespace httpserver::core