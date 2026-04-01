// src/httpserver/core/connections/connection.cpp
#include "httpserver/core/tcp_connections/Connection/connection.hpp"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>
#include <expected>

namespace httpserver::core {

// 连接实现类
class TcpConnection : public IConnection, public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(std::shared_ptr<IIOHandler> io_handler,
                  std::shared_ptr<IBufferManager> buffer_mgr,
                  std::shared_ptr<IAsyncScheduler> scheduler,
                  std::shared_ptr<IEventDispatcher> dispatcher)
        : io_handler_(std::move(io_handler))
        , buffer_mgr_(std::move(buffer_mgr))
        , scheduler_(std::move(scheduler))
        , dispatcher_(std::move(dispatcher))
        , fd_(-1)
        , state_(ConnectionState::DISCONNECTED)
        , config_()
        , bytes_sent_(0)
        , bytes_received_(0)
        , total_ops_(0)
        , connect_time_(std::chrono::steady_clock::now())
        , last_activity_(connect_time_)
        , heartbeat_time_(connect_time_)
        , conn_id_(0)
        , manager_(nullptr)
    {
        if (!io_handler_) io_handler_ = IIOHandler::CreateDefault();
        if (!buffer_mgr_) buffer_mgr_ = IBufferManager::CreateDefault();
        if (!scheduler_) scheduler_ = IScheduler::CreateDefault();
        if (!dispatcher_) dispatcher_ = IEventDispatcher::CreateDefault();
    }

    ~TcpConnection() override {
        Close();
    }

    // 连接生命周期
    std::error_code Connect(const std::string& host, uint16_t port) override {
        auto addr = SocketAddress::FromIpPort(host, port);
        return Connect(addr);
    }

    std::error_code Connect(const SocketAddress& addr) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::DISCONNECTED) {
            return make_error_code(ConnectionError::ALREADY_CONNECTED);
        }
        // 创建 socket
        auto fd_res = io_handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
        if (fd_res.has_error()) return fd_res.error();
        fd_ = fd_res.value();

        // 设置非阻塞
        auto set_nonblock = io_handler_->SetNonBlocking(fd_, true);
        if (set_nonblock.has_error()) {
            io_handler_->CloseSocket(fd_);
            fd_ = -1;
            return set_nonblock.error();
        }

        // 设置 TCP_NODELAY
        if (config_.no_delay) {
            auto set_nodelay = io_handler_->SetTcpNoDelay(fd_, true);
            if (set_nodelay.has_error()) {
                // 可选，非致命错误
            }
        }

        // 尝试连接
        auto connect_res = io_handler_->Connect(fd_, reinterpret_cast<const sockaddr*>(&addr.addr), addr.len);
        if (connect_res.has_error()) {
            // 非阻塞连接可能返回 EINPROGRESS
            if (connect_res.error().value() != EINPROGRESS) {
                io_handler_->CloseSocket(fd_);
                fd_ = -1;
                return connect_res.error();
            }
            // 连接进行中，状态设为 CONNECTING，稍后由事件机制完成
            state_ = ConnectionState::CONNECTING;
            // 注册写事件，等待连接完成
            auto weak_self = weak_from_this();
            dispatcher_->Register(fd_, EVENT_WRITE, [weak_self](int fd, EventMask events) {
                if (auto self = weak_self.lock()) {
                    self->onConnectComplete();
                }
            });
            return std::error_code();
        } else {
            // 立即连接成功
            state_ = ConnectionState::CONNECTED;
            connect_time_ = std::chrono::steady_clock::now();
            updateActivity();
            // 注册读事件
            registerReadEvent();
            return std::error_code();
        }
    }

    std::error_code Disconnect() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ConnectionState::DISCONNECTED) return std::error_code();
        state_ = ConnectionState::DISCONNECTING;
        closeInternal();
        return std::error_code();
    }

    void Close() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        closeInternal();
    }

    // 同步数据操作
    std::expected<size_t, std::error_code> Send(std::string_view data) override {
        return Send(data.data(), data.size());
    }

    std::expected<size_t, std::error_code> Send(const void* data, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::CONNECTED) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        auto result = io_handler_->Send(fd_, data, len, 0);
        if (result.has_error()) {
            if (result.error().value() == EAGAIN || result.error().value() == EWOULDBLOCK) {
                // 需要排队发送
                // 将数据加入发送队列
                if (!send_queue_.EnqueueSend(BufferView(static_cast<const char*>(data), len))) {
                    return std::unexpected(make_error_code(ConnectionError::SEND_QUEUE_FULL));
                }
                // 注册写事件，等待可写
                registerWriteEvent();
                return len;  // 表示已入队，实际发送稍后完成
            }
            return std::unexpected(result.error());
        }
        bytes_sent_ += result.value();
        total_ops_++;
        updateActivity();
        return result.value();
    }

    std::expected<std::string, std::error_code> Receive(size_t max_len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::CONNECTED) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        std::string buffer(max_len, '\0');
        auto result = io_handler_->Recv(fd_, buffer.data(), max_len, 0);
        if (result.has_error()) {
            if (result.error().value() == EAGAIN || result.error().value() == EWOULDBLOCK) {
                return std::unexpected(make_error_code(ConnectionError::WOULD_BLOCK));
            }
            return std::unexpected(result.error());
        }
        if (result.value() == 0) {
            // 对端关闭
            closeInternal();
            return std::unexpected(make_error_code(ConnectionError::CONNECTION_RESET));
        }
        buffer.resize(result.value());
        bytes_received_ += result.value();
        total_ops_++;
        updateActivity();
        return buffer;
    }

    std::expected<size_t, std::error_code> Receive(void* buffer, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::CONNECTED) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        auto result = io_handler_->Recv(fd_, buffer, len, 0);
        if (result.has_error()) {
            if (result.error().value() == EAGAIN || result.error().value() == EWOULDBLOCK) {
                return std::unexpected(make_error_code(ConnectionError::WOULD_BLOCK));
            }
            return std::unexpected(result.error());
        }
        if (result.value() == 0) {
            closeInternal();
            return std::unexpected(make_error_code(ConnectionError::CONNECTION_RESET));
        }
        bytes_received_ += result.value();
        total_ops_++;
        updateActivity();
        return result.value();
    }

    // 异步数据操作
    AsyncResult<size_t> SendAsync(std::string_view data) override {
        return SendAsync(data.data(), data.size());
    }

    AsyncResult<size_t> SendAsync(const void* data, size_t len) override {
        // 复制数据，因为异步操作需要保持有效
        auto shared_data = std::make_shared<std::vector<char>>(static_cast<const char*>(data), static_cast<const char*>(data) + len);
        auto weak_self = weak_from_this();
        // 使用调度器提交异步发送任务
        auto future = scheduler_->Schedule([weak_self, shared_data]() -> size_t {
            auto self = weak_self.lock();
            if (!self) throw std::runtime_error("Connection destroyed");
            auto result = self->Send(shared_data->data(), shared_data->size());
            if (!result) throw std::system_error(result.error());
            return result.value();
        });
        return future;
    }

    AsyncResult<std::string> ReceiveAsync(size_t max_len) override {
        auto weak_self = weak_from_this();
        auto future = scheduler_->Schedule([weak_self, max_len]() -> std::string {
            auto self = weak_self.lock();
            if (!self) throw std::runtime_error("Connection destroyed");
            auto result = self->Receive(max_len);
            if (!result) throw std::system_error(result.error());
            return result.value();
        });
        return future;
    }

    AsyncResult<size_t> ReceiveAsync(void* buffer, size_t len) override {
        // 注意：buffer 必须在异步操作完成前有效，此处简化，假设调用者保证生命周期
        auto weak_self = weak_from_this();
        auto future = scheduler_->Schedule([weak_self, buffer, len]() -> size_t {
            auto self = weak_self.lock();
            if (!self) throw std::runtime_error("Connection destroyed");
            auto result = self->Receive(buffer, len);
            if (!result) throw std::system_error(result.error());
            return result.value();
        });
        return future;
    }

    // 缓冲区管理
    std::error_code Flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::CONNECTED) {
            return make_error_code(ConnectionError::NOT_CONNECTED);
        }
        // 尝试发送队列中的所有数据
        while (!send_queue_.IsSendQueueEmpty()) {
            auto view = send_queue_.PeekSend();
            if (view.size == 0) break;
            auto result = io_handler_->Send(fd_, view.data, view.size, 0);
            if (result.has_error()) {
                if (result.error().value() == EAGAIN || result.error().value() == EWOULDBLOCK) {
                    // 等待下次写事件
                    registerWriteEvent();
                    break;
                }
                return result.error();
            }
            send_queue_.ConsumeSend(result.value());
            bytes_sent_ += result.value();
            total_ops_++;
            updateActivity();
        }
        return std::error_code();
    }

    void ClearBuffers() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        send_queue_.ClearAll();
    }

    size_t GetPendingSendBytes() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return send_queue_.GetSendQueueSize();
    }

    size_t GetPendingReceiveBytes() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return receive_buffer_.GetReceiveBufferSize();
    }

    // 配置管理
    void Configure(const ConnectionConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        // 应用配置到 socket
        if (fd_ != -1) {
            io_handler_->SetTcpNoDelay(fd_, config_.no_delay);
            // 其他选项可以在这里设置
        }
    }

    ConnectionConfig GetConfig() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    void UpdateConfig(std::function<void(ConnectionConfig&)> updater) override {
        std::lock_guard<std::mutex> lock(mutex_);
        updater(config_);
        // 应用更新后的配置
        if (fd_ != -1) {
            io_handler_->SetTcpNoDelay(fd_, config_.no_delay);
        }
    }

    // 状态查询
    ConnectionState GetState() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    bool IsConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == ConnectionState::CONNECTED;
    }

    bool IsReadable() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return false;
        auto res = io_handler_->PollRead(fd_, 0);
        return res.has_value() && res.value();
    }

    bool IsWritable() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return false;
        auto res = io_handler_->PollWrite(fd_, 0);
        return res.has_value() && res.value();
    }

    bool HasError() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == ConnectionState::ERROR;
    }

    // 地址信息
    std::string GetLocalAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return "";
        auto addr_res = io_handler_->GetLocalAddress(fd_);
        if (!addr_res.has_value()) return "";
        return addr_res.value().ToString();
    }

    std::string GetPeerAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return "";
        auto addr_res = io_handler_->GetPeerAddress(fd_);
        if (!addr_res.has_value()) return "";
        return addr_res.value().ToString();
    }

    uint16_t GetLocalPort() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return 0;
        auto addr_res = io_handler_->GetLocalAddress(fd_);
        if (!addr_res.has_value()) return 0;
        return addr_res.value().GetPort();
    }

    uint16_t GetPeerPort() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return 0;
        auto addr_res = io_handler_->GetPeerAddress(fd_);
        if (!addr_res.has_value()) return 0;
        return addr_res.value().GetPort();
    }

    SocketAddress GetLocalSocketAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return SocketAddress();
        auto addr_res = io_handler_->GetLocalAddress(fd_);
        if (!addr_res.has_value()) return SocketAddress();
        return addr_res.value();
    }

    SocketAddress GetPeerSocketAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return SocketAddress();
        auto addr_res = io_handler_->GetPeerAddress(fd_);
        if (!addr_res.has_value()) return SocketAddress();
        return addr_res.value();
    }

    // 统计信息
    size_t GetBytesSent() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_sent_;
    }

    size_t GetBytesReceived() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_received_;
    }

    size_t GetTotalOperations() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_ops_;
    }

    std::chrono::steady_clock::time_point GetConnectTime() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return connect_time_;
    }

    std::chrono::steady_clock::time_point GetLastActivityTime() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_activity_;
    }

    // 超时等待
    bool WaitForData(std::chrono::milliseconds timeout) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return false;
        auto res = io_handler_->PollRead(fd_, static_cast<int>(timeout.count()));
        return res.has_value() && res.value();
    }

    bool WaitForWritable(std::chrono::milliseconds timeout) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return false;
        auto res = io_handler_->PollWrite(fd_, static_cast<int>(timeout.count()));
        return res.has_value() && res.value();
    }

    // 文件描述符
    int GetFd() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return fd_;
    }

    // 事件回调
    void SetEventCallback(EventCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        event_cb_ = std::move(callback);
    }

    void SetDataCallback(DataCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        data_cb_ = std::move(callback);
    }

    void SetErrorCallback(ErrorCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        error_cb_ = std::move(callback);
    }

    // 连接ID
    uint64_t GetConnectionId() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return conn_id_;
    }

    void SetConnectionId(uint64_t id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        conn_id_ = id;
    }

    // 管理器关联
    void SetConnectionManager(std::shared_ptr<IConnectionManager> manager) override {
        std::lock_guard<std::mutex> lock(mutex_);
        manager_ = std::move(manager);
    }

    std::shared_ptr<IConnectionManager> GetConnectionManager() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return manager_;
    }

    // 自定义数据
    void SetUserData(const std::string& key, std::any data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        user_data_[key] = std::move(data);
    }

    std::any GetUserData(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_data_.find(key);
        if (it != user_data_.end()) return it->second;
        return std::any();
    }

    bool HasUserData(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return user_data_.find(key) != user_data_.end();
    }

    void RemoveUserData(const std::string& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        user_data_.erase(key);
    }

    // 心跳
    void UpdateHeartbeat() override {
        std::lock_guard<std::mutex> lock(mutex_);
        heartbeat_time_ = std::chrono::steady_clock::now();
    }

    bool IsHeartbeatExpired(std::chrono::milliseconds timeout) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        return (now - heartbeat_time_) > timeout;
    }

private:
    // 内部辅助函数
    void closeInternal() {
        if (fd_ != -1) {
            if (dispatcher_) dispatcher_->Unregister(fd_);
            io_handler_->CloseSocket(fd_);
            fd_ = -1;
        }
        state_ = ConnectionState::DISCONNECTED;
        send_queue_.ClearAll();
        receive_buffer_.ClearAll();
    }

    void registerReadEvent() {
        if (dispatcher_ && fd_ != -1) {
            auto weak_self = weak_from_this();
            dispatcher_->Register(fd_, EVENT_READ, [weak_self](int fd, EventMask events) {
                if (auto self = weak_self.lock()) {
                    self->onReadEvent();
                }
            });
        }
    }

    void registerWriteEvent() {
        if (dispatcher_ && fd_ != -1) {
            auto weak_self = weak_from_this();
            dispatcher_->Register(fd_, EVENT_WRITE, [weak_self](int fd, EventMask events) {
                if (auto self = weak_self.lock()) {
                    self->onWriteEvent();
                }
            });
        }
    }

    void onConnectComplete() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ConnectionState::CONNECTING) {
            // 检查连接是否成功
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                state_ = ConnectionState::CONNECTED;
                connect_time_ = std::chrono::steady_clock::now();
                updateActivity();
                registerReadEvent();
                if (event_cb_) event_cb_(ConnectionEvent::CONNECTED);
            } else {
                state_ = ConnectionState::ERROR;
                if (error_cb_) error_cb_(make_error_code(ConnectionError::CONNECT_FAILED));
            }
        }
    }

    void onReadEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::CONNECTED) return;
        // 读取数据到接收缓冲区
        auto view = receive_buffer_.GetReceiveBuffer();
        if (view.size == 0) return; // 无可用空间，可能需扩容
        auto result = io_handler_->Recv(fd_, view.data, view.size, 0);
        if (result.has_error()) {
            if (result.error().value() == EAGAIN || result.error().value() == EWOULDBLOCK) {
                // 无数据，下次再读
                return;
            }
            // 错误，关闭连接
            closeInternal();
            if (error_cb_) error_cb_(result.error());
            return;
        }
        if (result.value() == 0) {
            closeInternal();
            if (event_cb_) event_cb_(ConnectionEvent::DISCONNECTED);
            return;
        }
        receive_buffer_.CommitReceive(result.value());
        bytes_received_ += result.value();
        total_ops_++;
        updateActivity();
        if (data_cb_) data_cb_(BufferView(receive_buffer_.GetReceiveBuffer().data, receive_buffer_.GetReceiveBufferSize()));
    }

    void onWriteEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ConnectionState::CONNECTED) return;
        // 尝试发送队列中的数据
        while (!send_queue_.IsSendQueueEmpty()) {
            auto view = send_queue_.PeekSend();
            if (view.size == 0) break;
            auto result = io_handler_->Send(fd_, view.data, view.size, 0);
            if (result.has_error()) {
                if (result.error().value() == EAGAIN || result.error().value() == EWOULDBLOCK) {
                    // 仍然不可写，保持注册写事件
                    return;
                }
                // 错误，关闭连接
                closeInternal();
                if (error_cb_) error_cb_(result.error());
                return;
            }
            send_queue_.ConsumeSend(result.value());
            bytes_sent_ += result.value();
            total_ops_++;
            updateActivity();
            if (event_cb_) event_cb_(ConnectionEvent::DATA_SENT);
        }
        // 发送完毕，注销写事件
        if (send_queue_.IsSendQueueEmpty()) {
            if (dispatcher_) dispatcher_->Modify(fd_, EVENT_READ); // 只保留读事件
        }
    }

    void updateActivity() {
        last_activity_ = std::chrono::steady_clock::now();
        UpdateHeartbeat();
    }

    // 辅助错误码生成
    static std::error_code make_error_code(ConnectionError err) {
        return std::error_code(static_cast<int>(err), std::generic_category());
    }

    // 成员变量
    mutable std::mutex mutex_;
    std::shared_ptr<IIOHandler> io_handler_;
    std::shared_ptr<IBufferManager> buffer_mgr_;
    std::shared_ptr<IAsyncScheduler> scheduler_;
    std::shared_ptr<IEventDispatcher> dispatcher_;

    int fd_;
    ConnectionState state_;
    ConnectionConfig config_;

    // 发送队列和接收缓冲区
    ConnectionBufferManager send_queue_;   // 使用 ConnectionBufferManager 管理发送队列
    ConnectionBufferManager receive_buffer_; // 管理接收缓冲区

    // 统计
    size_t bytes_sent_;
    size_t bytes_received_;
    size_t total_ops_;
    std::chrono::steady_clock::time_point connect_time_;
    std::chrono::steady_clock::time_point last_activity_;
    std::chrono::steady_clock::time_point heartbeat_time_;

    uint64_t conn_id_;
    std::shared_ptr<IConnectionManager> manager_;

    // 回调
    EventCallback event_cb_;
    DataCallback data_cb_;
    ErrorCallback error_cb_;

    // 用户数据
    std::unordered_map<std::string, std::any> user_data_;
};

// 工厂方法实现
std::shared_ptr<IConnection> IConnection::Create(
    std::shared_ptr<IIOHandler> io_handler,
    std::shared_ptr<IBufferManager> buffer_manager,
    std::shared_ptr<IAsyncScheduler> scheduler,
    std::shared_ptr<IEventDispatcher> dispatcher) {
    return std::make_shared<TcpConnection>(
        std::move(io_handler),
        std::move(buffer_manager),
        std::move(scheduler),
        std::move(dispatcher));
}

} // namespace httpserver::core