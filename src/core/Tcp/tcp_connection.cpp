// src/core/Tcp/tcp_connection.cpp
#include "httpserver/core/Tcp/tcp_connection.hpp"
#include "httpserver/core/Buffer/connection_buffer_mgr.hpp"
#include "httpserver/core/Event/connection_event_mgr.hpp"
#include "httpserver/core/Async/async_scheduler.hpp"
#include <system_error>
#include <cstring>
#include <chrono>
#include <random>
#include <thread>
#include <iostream>
#include <chrono>
namespace httpserver::core {

// ============================================================================
// 构造函数（主动创建连接）
// ============================================================================
// 构造函数（主动创建连接）
TCPConnection::TCPConnection(
    std::shared_ptr<IIOHandler> io_handler,
    std::shared_ptr<IBufferManager> buffer_manager,
    std::shared_ptr<async::IScheduler> scheduler,
    std::shared_ptr<IEventDispatcher> dispatcher)
    : io_handler_(std::move(io_handler))
    , buffer_manager_(std::move(buffer_manager))
    , scheduler_(std::move(scheduler))
    , dispatcher_(std::move(dispatcher))
    , connection_buffer_(std::make_shared<ConnectionBufferManager>(
          IBufferFactory::CreateDefault(),   // 使用工厂方法
          IBufferOperator::CreateDefault()))
    , event_manager_(std::make_shared<ConnectionEventManager>(dispatcher_))
    , socket_fd_(-1)
    , state_(ConnectionState::DISCONNECTED)
    , bytes_sent_(0)
    , bytes_received_(0)
    , total_operations_(0)
    , connect_time_(std::chrono::steady_clock::now())
    , last_activity_time_(connect_time_)
    , last_heartbeat_time_(connect_time_)
    , connection_id_(0) {
    config_ = ConnectionConfig();
}

// 构造函数（从已有文件描述符创建）
TCPConnection::TCPConnection(
    int existing_fd,
    const SocketAddress& peer_addr,
    std::shared_ptr<IIOHandler> io_handler,
    std::shared_ptr<IBufferManager> buffer_manager,
    std::shared_ptr<async::IScheduler> scheduler,
    std::shared_ptr<IEventDispatcher> dispatcher)
    : io_handler_(std::move(io_handler))
    , buffer_manager_(std::move(buffer_manager))
    , scheduler_(std::move(scheduler))
    , dispatcher_(std::move(dispatcher))
    , connection_buffer_(std::make_shared<ConnectionBufferManager>(
          IBufferFactory::CreateDefault(),
          IBufferOperator::CreateDefault()))
    , event_manager_(std::make_shared<ConnectionEventManager>(dispatcher_))
    , socket_fd_(existing_fd)
    , peer_addr_(peer_addr)
    , state_(ConnectionState::CONNECTED)
    , bytes_sent_(0)
    , bytes_received_(0)
    , total_operations_(0)
    , connect_time_(std::chrono::steady_clock::now())
    , last_activity_time_(connect_time_)
    , last_heartbeat_time_(connect_time_)
    , connection_id_(0) {
    auto local = io_handler_->GetLocalAddress(socket_fd_);
    if (local) local_addr_ = *local;
    // 移除：event_manager_->SetConnection(shared_from_this());
    setup_socket();
}

// 初始化方法
void TCPConnection::init() {
    event_manager_->SetConnection(shared_from_this());
    
    std::cout << "🔧 TCPConnection::init() called, starting read thread for fd=" << socket_fd_ << std::endl;
    
    // 启动读线程
    std::thread([this]() {
        char buffer[8192];
        while (state_.load() == ConnectionState::CONNECTED) {
            ssize_t n = recv(socket_fd_, buffer, sizeof(buffer), 0);
            if (n > 0) {
                // 通过 event_manager_ 触发数据回调
                event_manager_->TriggerData(std::string_view(buffer, n));
            } else if (n == 0) {
                break;  // 连接关闭
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;  // 错误
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // 连接断开，触发关闭事件
        event_manager_->TriggerEvent(ConnectionEvent::DISCONNECTED);
    }).detach();
}

// 析构函数
TCPConnection::~TCPConnection() {
    Close();
}

// ============================================================================
// 私有辅助方法
// ============================================================================

bool TCPConnection::setup_socket() {
    if (socket_fd_ < 0) return false;

    if (config_.nonblocking) {
        io_handler_->SetNonBlocking(socket_fd_, true);
    }
    if (config_.no_delay) {
        io_handler_->SetTcpNoDelay(socket_fd_, true);
    }
    if (config_.keep_alive) {
        int keepalive = 1;
        io_handler_->SetSocketOption(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    }
    if (config_.reuse_address) {
        int reuse = 1;
        io_handler_->SetSocketOption(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }
    if (config_.send_buffer_size > 0) {
        io_handler_->SetSocketOption(socket_fd_, SOL_SOCKET, SO_SNDBUF,
                                     &config_.send_buffer_size, sizeof(config_.send_buffer_size));
    }
    if (config_.recv_buffer_size > 0) {
        io_handler_->SetSocketOption(socket_fd_, SOL_SOCKET, SO_RCVBUF,
                                     &config_.recv_buffer_size, sizeof(config_.recv_buffer_size));
    }
    return true;
}

std::error_code TCPConnection::handle_socket_error() {
    return io_handler_->GetLastSocketError();
}

void TCPConnection::update_activity_timestamp() {
    last_activity_time_ = std::chrono::steady_clock::now();
}

// ============================================================================
// 连接生命周期管理
// ============================================================================

std::error_code TCPConnection::Connect(const std::string& host, uint16_t port) {
    SocketAddress addr = SocketAddress::FromIpPort(host, port);
    return Connect(addr);
}

std::error_code TCPConnection::Connect(const SocketAddress& addr) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != ConnectionState::DISCONNECTED) {
        return std::error_code(static_cast<int>(ConnectionError::ALREADY_CONNECTED),
                               std::generic_category());
    }

    auto result = io_handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
    if (!result) {
        state_ = ConnectionState::ERROR;
        return result.error();
    }
    socket_fd_ = *result;
    peer_addr_ = addr;

    setup_socket();

    sockaddr_in sa;
    std::memcpy(&sa, &addr.addr, sizeof(sa));
    auto conn_result = io_handler_->Connect(socket_fd_, (sockaddr*)&sa, sizeof(sa));
    
    // ✅ 修复：使用 has_error() 而不是 !
    if (conn_result.has_error()) {
        io_handler_->CloseSocket(socket_fd_);
        socket_fd_ = -1;
        state_ = ConnectionState::ERROR;
        return conn_result.error();
    }
    auto local = io_handler_->GetLocalAddress(socket_fd_);
    if (local) local_addr_ = *local;

    state_ = ConnectionState::CONNECTED;
    connect_time_ = std::chrono::steady_clock::now();
    update_activity_timestamp();
init();
    ///event_manager_->SetConnection(shared_from_this());
    event_manager_->TriggerEvent(ConnectionEvent::CONNECTED);

    return {};
}

std::error_code TCPConnection::Disconnect() noexcept {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != ConnectionState::CONNECTED) return {};

    state_ = ConnectionState::DISCONNECTING;
    io_handler_->ShutdownSocket(socket_fd_, SHUT_RDWR);
    event_manager_->TriggerEvent(ConnectionEvent::DISCONNECTED);
    state_ = ConnectionState::DISCONNECTED;
    return {};
}

void TCPConnection::Close() noexcept {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (socket_fd_ >= 0) {
        io_handler_->CloseSocket(socket_fd_);
        socket_fd_ = -1;
        state_ = ConnectionState::CLOSING;
        event_manager_->TriggerEvent(ConnectionEvent::CLOSED);
    }
}

// ============================================================================
// 同步数据收发 (使用 Result<T>)
// ============================================================================

Result<size_t> TCPConnection::Send(std::string_view data) {
    return Send(data.data(), data.size());
}

Result<size_t> TCPConnection::Send(const void* data, size_t len) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != ConnectionState::CONNECTED) {
        return Result<size_t>(std::error_code(ENOTCONN, std::system_category()));
    }
    connection_buffer_->EnqueueSend(BufferView(static_cast<const char*>(data), len));
    auto ec = Flush();
    if (!ec) {
        return Result<size_t>(len);
    } else {
        return Result<size_t>(ec);
    }
}

Result<std::string> TCPConnection::Receive(size_t max_len) {
    if (max_len == 0) return Result<std::string>(std::string{});

    if (connection_buffer_->GetReceiveBufferSize() == 0) {
        auto buf = connection_buffer_->GetReceiveBuffer();
        if (buf.size == 0) {
            connection_buffer_->PrepareReceive(max_len);
            buf = connection_buffer_->GetReceiveBuffer();
        }

        auto result = io_handler_->Recv(socket_fd_, buf.data, buf.size, 0);
        if (!result) {
            return Result<std::string>(result.error());
        }
        ssize_t n = *result;
        if (n > 0) {
            connection_buffer_->CommitReceive(n);
            bytes_received_ += n;
            update_activity_timestamp();
        }
    }

    size_t available = connection_buffer_->GetReceiveBufferSize();
    size_t to_read = std::min(max_len, available);
    std::string result(to_read, '\0');
    auto view = connection_buffer_->GetReceiveBuffer();
    if (view.data) {
        std::memcpy(result.data(), view.data, to_read);
        // 简化：清空接收缓冲区（生产环境应只消费 to_read 字节）
        connection_buffer_->ClearAll();
    }
    return Result<std::string>(std::move(result));
}

Result<size_t> TCPConnection::Receive(void* buffer, size_t len) {
    auto str_res = Receive(len);
    if (!str_res.has_value()) {
        return Result<size_t>(str_res.error());
    }
    const std::string& s = str_res.value();
    size_t n = s.size();
    if (n > len) n = len;
    std::memcpy(buffer, s.data(), n);
    return Result<size_t>(n);
}

// ============================================================================
// 异步数据收发
// ============================================================================

AsyncResult<size_t> TCPConnection::SendAsync(std::string_view data) {
    return scheduler_->Schedule([this, data = std::string(data)]() -> size_t {
        auto result = Send(data);
        if (result.has_value()) {
            return result.value();
        } else {
            throw std::system_error(result.error());
        }
    });
}

AsyncResult<size_t> TCPConnection::SendAsync(const void* data, size_t len) {
    return SendAsync(std::string_view(static_cast<const char*>(data), len));
}

AsyncResult<std::string> TCPConnection::ReceiveAsync(size_t max_len) {
    return scheduler_->Schedule([this, max_len]() -> std::string {
        auto result = Receive(max_len);
        if (result.has_value()) {
            return result.value();
        } else {
            throw std::system_error(result.error());
        }
    });
}

AsyncResult<size_t> TCPConnection::ReceiveAsync(void* buffer, size_t len) {
    return scheduler_->Schedule([this, buffer, len]() -> size_t {
        auto result = Receive(buffer, len);
        if (result.has_value()) {
            return result.value();
        } else {
            throw std::system_error(result.error());
        }
    });
}

// ============================================================================
// 缓冲区管理
// ============================================================================

std::error_code TCPConnection::Flush() {
    while (!connection_buffer_->IsSendQueueEmpty()) {
        auto view = connection_buffer_->PeekSend();
        if (view.size == 0) break;

        auto result = io_handler_->Send(socket_fd_, view.data, view.size, 0);
        if (!result) {
            if (result.error() == std::error_code(EAGAIN, std::system_category()) ||
                result.error() == std::error_code(EWOULDBLOCK, std::system_category())) {
                // 等待可写事件（应由外部事件循环触发，这里简化返回错误）
                return result.error();
            }
            return result.error();
        }
        size_t sent = *result;
        if (sent > 0) {
            connection_buffer_->ConsumeSend(sent);
            bytes_sent_ += sent;
            update_activity_timestamp();
        }
        if (sent < view.size) break;
    }
    return {};
}

void TCPConnection::ClearBuffers() noexcept {
    connection_buffer_->ClearAll();
}

size_t TCPConnection::GetPendingSendBytes() const noexcept {
    return connection_buffer_->GetSendQueueSize();
}

size_t TCPConnection::GetPendingReceiveBytes() const noexcept {
    return connection_buffer_->GetReceiveBufferSize();
}

// ============================================================================
// 配置管理
// ============================================================================

void TCPConnection::Configure(const ConnectionConfig& config) {
    config_ = config;
    setup_socket();
    connection_buffer_->SetMaxSendQueueSize(config_.send_buffer_size);
}

ConnectionConfig TCPConnection::GetConfig() const {
    return config_;
}

void TCPConnection::UpdateConfig(std::function<void(ConnectionConfig&)> updater) {
    updater(config_);
    Configure(config_);
}

// ============================================================================
// 状态查询
// ============================================================================

ConnectionState TCPConnection::GetState() const {
    return state_.load();
}

bool TCPConnection::IsConnected() const {
    return state_.load() == ConnectionState::CONNECTED;
}

bool TCPConnection::IsReadable() const {
    return connection_buffer_->GetReceiveBufferSize() > 0;
}

bool TCPConnection::IsWritable() const {
    return connection_buffer_->GetSendQueueSize() < config_.send_buffer_size;
}

bool TCPConnection::HasError() const {
    return state_.load() == ConnectionState::ERROR;
}

// ============================================================================
// 地址信息
// ============================================================================

std::string TCPConnection::GetLocalAddress() const {
    return local_addr_.ToString();
}

std::string TCPConnection::GetPeerAddress() const {
    return peer_addr_.ToString();
}

uint16_t TCPConnection::GetLocalPort() const noexcept {
    return local_addr_.GetPort();
}

uint16_t TCPConnection::GetPeerPort() const noexcept {
    return peer_addr_.GetPort();
}

SocketAddress TCPConnection::GetLocalSocketAddress() const {
    return local_addr_;
}

SocketAddress TCPConnection::GetPeerSocketAddress() const {
    return peer_addr_;
}

// ============================================================================
// 统计信息
// ============================================================================

size_t TCPConnection::GetBytesSent() const noexcept {
    return bytes_sent_.load();
}

size_t TCPConnection::GetBytesReceived() const noexcept {
    return bytes_received_.load();
}

size_t TCPConnection::GetTotalOperations() const noexcept {
    return total_operations_.load();
}

std::chrono::steady_clock::time_point TCPConnection::GetConnectTime() const {
    return connect_time_;
}

std::chrono::steady_clock::time_point TCPConnection::GetLastActivityTime() const {
    return last_activity_time_;
}

// ============================================================================
// 超时等待
// ============================================================================

bool TCPConnection::WaitForData(std::chrono::milliseconds timeout) {
    auto result = io_handler_->PollRead(socket_fd_, static_cast<int>(timeout.count()));
    return result && *result;
}

bool TCPConnection::WaitForWritable(std::chrono::milliseconds timeout) {
    auto result = io_handler_->PollWrite(socket_fd_, static_cast<int>(timeout.count()));
    return result && *result;
}

// ============================================================================
// 文件描述符
// ============================================================================

int TCPConnection::GetFd() const {
    return socket_fd_;
}

// ============================================================================
// 事件回调
// ============================================================================

void TCPConnection::SetEventCallback(EventCallback callback) {
    event_manager_->SetEventCallback(std::move(callback));
}

void TCPConnection::SetDataCallback(DataCallback callback) {
    event_manager_->SetDataCallback(std::move(callback));
}

void TCPConnection::SetErrorCallback(ErrorCallback callback) {
    event_manager_->SetErrorCallback(std::move(callback));
}

// ============================================================================
// 连接ID
// ============================================================================

uint64_t TCPConnection::GetConnectionId() const noexcept {
    return connection_id_;
}

void TCPConnection::SetConnectionId(uint64_t id) {
    connection_id_ = id;
}

// ============================================================================
// 管理器关联
// ============================================================================

void TCPConnection::SetConnectionManager(std::shared_ptr<IConnectionManager> manager) {
    connection_manager_ = std::move(manager);
}

std::shared_ptr<IConnectionManager> TCPConnection::GetConnectionManager() const {
    return connection_manager_;
}

// ============================================================================
// 自定义数据
// ============================================================================

void TCPConnection::SetUserData(const std::string& key, std::any data) {
    std::lock_guard<std::mutex> lock(user_data_mutex_);
    user_data_[key] = std::move(data);
}

std::any TCPConnection::GetUserData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(user_data_mutex_);
    auto it = user_data_.find(key);
    return it != user_data_.end() ? it->second : std::any{};
}

bool TCPConnection::HasUserData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(user_data_mutex_);
    return user_data_.find(key) != user_data_.end();
}

void TCPConnection::RemoveUserData(const std::string& key) {
    std::lock_guard<std::mutex> lock(user_data_mutex_);
    user_data_.erase(key);
}

// ============================================================================
// 心跳
// ============================================================================

void TCPConnection::UpdateHeartbeat() {
    last_heartbeat_time_ = std::chrono::steady_clock::now();
}

bool TCPConnection::IsHeartbeatExpired(std::chrono::milliseconds timeout) const {
    auto now = std::chrono::steady_clock::now();
    return now - last_heartbeat_time_ > timeout;
}

} // namespace httpserver::core