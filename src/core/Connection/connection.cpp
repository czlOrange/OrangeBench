// src/httpserver/core/connections/connection.cpp
#include "core/Connection/connection_interface.hpp"
#include "core/net/socket.hpp"          // ✅ 唯一的网络抽象层
#include "Component/buffer_manager.cpp"
#include "Component/event_manager.cpp"
#include "Component/heartbeat.cpp"
#include "Component/identity.cpp"
#include "Component/state_machine.cpp"
#include "Component/statistics.cpp"
#include "Component/user_data.cpp"

#include <mutex>
#include <cstring>
#include <expected>

namespace httpserver::core {

static std::error_code make_error_code(ConnectionError err) {
    return std::error_code(static_cast<int>(err), std::generic_category());
}

class TcpConnection : public IConnection, public std::enable_shared_from_this<TcpConnection> {
public:
    // ✅ 构造函数直接接受 Socket，不再有 IIOHandler 参数
    TcpConnection(std::unique_ptr<net::Socket> socket,
                  std::shared_ptr<IBufferManager> buffer_mgr,
                  std::shared_ptr<async::IScheduler> scheduler,
                  std::shared_ptr<IEventDispatcher> dispatcher)
        : socket_(std::move(socket))
        , buffer_mgr_(std::move(buffer_mgr))
        , scheduler_(std::move(scheduler))
        , event_manager_(std::move(dispatcher))
        , config_()
    {
        if (!socket_) {
            throw std::invalid_argument("Socket cannot be null");
        }
        if (!buffer_mgr_) buffer_mgr_ = IBufferManager::CreateDefault();
        if (!scheduler_) scheduler_ = async::IScheduler::CreateDefault();
        if (!event_manager_.GetDispatcher()) {
            event_manager_ = EventManager(IEventDispatcher::CreateDefault());
        }
        
        // 应用默认配置到 socket
        applyConfigToSocket();
    }

    ~TcpConnection() override { Close(); }

    // ---------- 生命周期 ----------
    std::error_code Connect(const std::string& host, uint16_t port) override {
        net::NetAddress addr(host, port);
        return ConnectInternal(addr);
    }

    std::error_code Connect(const SocketAddress& addr) override {
        net::NetAddress net_addr(addr.GetIp(), addr.GetPort());
        return ConnectInternal(net_addr);
    }

    std::error_code Disconnect() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_machine_.GetState() == ConnectionState::DISCONNECTED) {
            return std::error_code();
        }
        state_machine_.SetDisconnecting();
        closeInternal();
        return std::error_code();
    }

    void Close() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        closeInternal();
    }

    // ---------- 同步数据 ----------
    std::expected<size_t, std::error_code> Send(std::string_view data) override {
        return Send(data.data(), data.size());
    }

    std::expected<size_t, std::error_code> Send(const void* data, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        
        ssize_t sent = socket_->send(data, len);
        if (sent < 0) {
            int err = socket_->get_error();
            if (err == EAGAIN || err == EWOULDBLOCK) {
                if (!send_queue_.EnqueueSend({static_cast<const char*>(data), len})) {
                    return std::unexpected(make_error_code(ConnectionError::SEND_QUEUE_FULL));
                }
                registerWriteEvent();
                return len;
            }
            return std::unexpected(std::error_code(err, std::system_category()));
        }
        
        stats_.RecordSent(static_cast<size_t>(sent));
        stats_.RecordOperation();
        updateActivity();
        return static_cast<size_t>(sent);
    }

    std::expected<std::string, std::error_code> Receive(size_t max_len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        
        std::string buffer(max_len, '\0');
        ssize_t recvd = socket_->recv(buffer.data(), max_len);
        if (recvd < 0) {
            int err = socket_->get_error();
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return std::unexpected(make_error_code(ConnectionError::WOULD_BLOCK));
            }
            return std::unexpected(std::error_code(err, std::system_category()));
        }
        if (recvd == 0) {
            closeInternal();
            return std::unexpected(make_error_code(ConnectionError::CONNECTION_RESET));
        }
        
        buffer.resize(static_cast<size_t>(recvd));
        stats_.RecordReceived(static_cast<size_t>(recvd));
        stats_.RecordOperation();
        updateActivity();
        return buffer;
    }

    std::expected<size_t, std::error_code> Receive(void* buffer, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        
        ssize_t recvd = socket_->recv(buffer, len);
        if (recvd < 0) {
            int err = socket_->get_error();
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return std::unexpected(make_error_code(ConnectionError::WOULD_BLOCK));
            }
            return std::unexpected(std::error_code(err, std::system_category()));
        }
        if (recvd == 0) {
            closeInternal();
            return std::unexpected(make_error_code(ConnectionError::CONNECTION_RESET));
        }
        
        stats_.RecordReceived(static_cast<size_t>(recvd));
        stats_.RecordOperation();
        updateActivity();
        return static_cast<size_t>(recvd);
    }

    // ---------- 异步数据 ----------
    AsyncResult<size_t> SendAsync(std::string_view data) override {
        return SendAsync(data.data(), data.size());
    }

    AsyncResult<size_t> SendAsync(const void* data, size_t len) override {
        auto shared_data = std::make_shared<std::vector<char>>(
            static_cast<const char*>(data), static_cast<const char*>(data) + len);
        auto weak_self = weak_from_this();
        return scheduler_->Schedule([weak_self, shared_data]() -> size_t {
            auto self = weak_self.lock();
            if (!self) throw std::runtime_error("Connection destroyed");
            auto result = self->Send(shared_data->data(), shared_data->size());
            if (!result) throw std::system_error(result.error());
            return result.value();
        });
    }

    AsyncResult<std::string> ReceiveAsync(size_t max_len) override {
        auto weak_self = weak_from_this();
        return scheduler_->Schedule([weak_self, max_len]() -> std::string {
            auto self = weak_self.lock();
            if (!self) throw std::runtime_error("Connection destroyed");
            auto result = self->Receive(max_len);
            if (!result) throw std::system_error(result.error());
            return std::move(result.value());
        });
    }

    AsyncResult<size_t> ReceiveAsync(void* buffer, size_t len) override {
        auto weak_self = weak_from_this();
        return scheduler_->Schedule([weak_self, buffer, len]() -> size_t {
            auto self = weak_self.lock();
            if (!self) throw std::runtime_error("Connection destroyed");
            auto result = self->Receive(buffer, len);
            if (!result) throw std::system_error(result.error());
            return result.value();
        });
    }

    // ---------- 缓冲区管理 ----------
    std::error_code Flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) {
            return make_error_code(ConnectionError::NOT_CONNECTED);
        }
        
        while (!send_queue_.IsSendQueueEmpty()) {
            auto view = send_queue_.PeekSend();
            if (view.size == 0) break;
            
            ssize_t sent = socket_->send(view.data, view.size);
            if (sent < 0) {
                int err = socket_->get_error();
                if (err == EAGAIN || err == EWOULDBLOCK) {
                    registerWriteEvent();
                    break;
                }
                return std::error_code(err, std::system_category());
            }
            
            send_queue_.ConsumeSend(static_cast<size_t>(sent));
            stats_.RecordSent(static_cast<size_t>(sent));
            stats_.RecordOperation();
            updateActivity();
        }
        
        if (send_queue_.IsSendQueueEmpty()) {
            event_manager_.ModifyToReadOnly(socket_->fd());
        }
        return std::error_code();
    }

    void ClearBuffers() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        send_queue_.ClearAll();
        receive_buffer_.ClearAll();
    }

    size_t GetPendingSendBytes() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return send_queue_.GetSendQueueSize();
    }

    size_t GetPendingReceiveBytes() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return receive_buffer_.GetReceiveBufferSize();
    }

    // ---------- 配置 ----------
    void Configure(const ConnectionConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        applyConfigToSocket();
    }

    ConnectionConfig GetConfig() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    void UpdateConfig(std::function<void(ConnectionConfig&)> updater) override {
        std::lock_guard<std::mutex> lock(mutex_);
        updater(config_);
        applyConfigToSocket();
    }

    // ---------- 状态 ----------
    ConnectionState GetState() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_machine_.GetState();
    }

    bool IsConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_machine_.IsConnected();
    }

    bool IsReadable() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_->is_valid() && state_machine_.IsConnected();
    }

    bool IsWritable() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_->is_valid() && state_machine_.IsConnected();
    }

    bool HasError() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_machine_.HasError();
    }

    // ---------- 地址 ----------
    std::string GetLocalAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_->is_valid() ? socket_->local_address().to_string() : "";
    }

    std::string GetPeerAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_->is_valid() ? socket_->peer_address().to_string() : "";
    }

    uint16_t GetLocalPort() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_->is_valid() ? socket_->local_address().port() : 0;
    }

    uint16_t GetPeerPort() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_->is_valid() ? socket_->peer_address().port() : 0;
    }

    SocketAddress GetLocalSocketAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!socket_->is_valid()) return SocketAddress();
        auto addr = socket_->local_address();
        return SocketAddress(addr.ip(), addr.port());
    }

    SocketAddress GetPeerSocketAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!socket_->is_valid()) return SocketAddress();
        auto addr = socket_->peer_address();
        return SocketAddress(addr.ip(), addr.port());
    }

    // ---------- 统计 ----------
    size_t GetBytesSent() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_.GetBytesSent();
    }

    size_t GetBytesReceived() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_.GetBytesReceived();
    }

    size_t GetTotalOperations() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_.GetTotalOperations();
    }

    std::chrono::steady_clock::time_point GetConnectTime() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_.GetConnectTime();
    }

    std::chrono::steady_clock::time_point GetLastActivityTime() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_.GetLastActivityTime();
    }

    // ---------- 等待 ----------
    bool WaitForData(std::chrono::milliseconds timeout) override {
        // TODO: 通过 EventManager 实现真正的超时等待
        return IsReadable();
    }

    bool WaitForWritable(std::chrono::milliseconds timeout) override {
        // TODO: 通过 EventManager 实现真正的超时等待
        return IsWritable();
    }

    // ---------- 文件描述符 ----------
    int GetFd() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_->fd();
    }

    // ---------- 回调 ----------
    void SetEventCallback(EventCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        event_manager_.SetEventCallback(std::move(callback));
    }

    void SetDataCallback(DataCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        event_manager_.SetDataCallback(std::move(callback));
    }

    void SetErrorCallback(ErrorCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        event_manager_.SetErrorCallback(std::move(callback));
    }

    // ---------- 身份 ----------
    uint64_t GetConnectionId() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return identity_.GetId();
    }

    void SetConnectionId(uint64_t id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        identity_.SetId(id);
    }

    void SetConnectionManager(std::shared_ptr<IConnectionManager> manager) override {
        std::lock_guard<std::mutex> lock(mutex_);
        identity_.SetManager(std::move(manager));
    }

    std::shared_ptr<IConnectionManager> GetConnectionManager() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return identity_.GetManager();
    }

    // ---------- 用户数据 ----------
    void SetUserData(const std::string& key, std::any data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        user_data_.Set(key, std::move(data));
    }

    std::any GetUserData(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return user_data_.Get(key);
    }

    bool HasUserData(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return user_data_.Has(key);
    }

    void RemoveUserData(const std::string& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        user_data_.Remove(key);
    }

    // ---------- 心跳 ----------
    void UpdateHeartbeat() override {
        std::lock_guard<std::mutex> lock(mutex_);
        heartbeat_.Update();
    }

    bool IsHeartbeatExpired(std::chrono::milliseconds timeout) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return heartbeat_.IsExpired(timeout);
    }

private:
    std::error_code ConnectInternal(const net::NetAddress& addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_machine_.GetState() != ConnectionState::DISCONNECTED) {
            return make_error_code(ConnectionError::ALREADY_CONNECTED);
        }
        
        if (!socket_->is_valid()) {
            // 如果 socket 无效，重新创建
            socket_ = net::create_socket(net::SocketType::TCP);
            applyConfigToSocket();
        }
        
        socket_->set_nonblocking(true);
        
        if (!socket_->connect(addr)) {
            int err = socket_->get_error();
            if (err == EINPROGRESS) {
                state_machine_.SetConnecting();
                auto weak_self = weak_from_this();
                event_manager_.RegisterWrite(socket_->fd(), [weak_self]() {
                    if (auto self = weak_self.lock()) {
                        static_cast<TcpConnection*>(self.get())->onConnectComplete();
                    }
                });
                return std::error_code();
            }
            return std::error_code(err, std::system_category());
        }
        
        state_machine_.SetConnected();
        stats_.SetConnectTime(std::chrono::steady_clock::now());
        updateActivity();
        registerReadEvent();
        event_manager_.NotifyEvent(ConnectionEvent::CONNECTED, shared_from_this(), "");
        return std::error_code();
    }

    void applyConfigToSocket() {
        net::SocketOptions opts;
        opts.tcp_no_delay = config_.no_delay;
        opts.keep_alive = config_.keep_alive;
        opts.reuse_addr = true;
        socket_->set_options(opts);
    }

    void closeInternal() {
        if (socket_->is_valid()) {
            event_manager_.Unregister(socket_->fd());
            socket_->close();
        }
        state_machine_.SetDisconnected();
        send_queue_.ClearAll();
        receive_buffer_.ClearAll();
        event_manager_.NotifyEvent(ConnectionEvent::DISCONNECTED, shared_from_this(), "");
    }

    void registerReadEvent() {
        auto weak_self = weak_from_this();
        event_manager_.RegisterRead(socket_->fd(), [weak_self]() {
            if (auto self = weak_self.lock()) {
                static_cast<TcpConnection*>(self.get())->onReadEvent();
            }
        });
    }

    void registerWriteEvent() {
        auto weak_self = weak_from_this();
        event_manager_.RegisterWrite(socket_->fd(), [weak_self]() {
            if (auto self = weak_self.lock()) {
                static_cast<TcpConnection*>(self.get())->onWriteEvent();
            }
        });
    }

    void onConnectComplete() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_machine_.GetState() == ConnectionState::CONNECTING) {
            int err = socket_->get_error();
            if (err == 0) {
                state_machine_.SetConnected();
                stats_.SetConnectTime(std::chrono::steady_clock::now());
                updateActivity();
                registerReadEvent();
                event_manager_.NotifyEvent(ConnectionEvent::CONNECTED, shared_from_this(), "");
            } else {
                state_machine_.SetError();
                event_manager_.NotifyError(shared_from_this(), std::error_code(err, std::system_category()));
            }
        }
    }

    void onReadEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) return;
        
        auto region = receive_buffer_.GetReceiveBuffer();
        if (region.size == 0) return;
        
        ssize_t recvd = socket_->recv(const_cast<char*>(region.data), region.size);
        if (recvd < 0) {
            int err = socket_->get_error();
            if (err == EAGAIN || err == EWOULDBLOCK) return;
            closeInternal();
            event_manager_.NotifyError(shared_from_this(), std::error_code(err, std::system_category()));
            return;
        }
        if (recvd == 0) {
            closeInternal();
            event_manager_.NotifyEvent(ConnectionEvent::DISCONNECTED, shared_from_this(), "");
            return;
        }
        
        receive_buffer_.CommitReceive(static_cast<size_t>(recvd));
        stats_.RecordReceived(static_cast<size_t>(recvd));
        stats_.RecordOperation();
        updateActivity();
        
        auto read_view = receive_buffer_.GetReceiveBuffer();
        event_manager_.NotifyData(shared_from_this(), std::string_view(read_view.data, receive_buffer_.GetReceiveBufferSize()));
    }

    void onWriteEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) return;
        
        while (!send_queue_.IsSendQueueEmpty()) {
            auto view = send_queue_.PeekSend();
            if (view.size == 0) break;
            
            ssize_t sent = socket_->send(view.data, view.size);
            if (sent < 0) {
                int err = socket_->get_error();
                if (err == EAGAIN || err == EWOULDBLOCK) return;
                closeInternal();
                event_manager_.NotifyError(shared_from_this(), std::error_code(err, std::system_category()));
                return;
            }
            
            send_queue_.ConsumeSend(static_cast<size_t>(sent));
            stats_.RecordSent(static_cast<size_t>(sent));
            stats_.RecordOperation();
            updateActivity();
            event_manager_.NotifyEvent(ConnectionEvent::DATA_SENT, shared_from_this(), "");
        }
        
        if (send_queue_.IsSendQueueEmpty()) {
            event_manager_.ModifyToReadOnly(socket_->fd());
        }
    }

    void updateActivity() {
        stats_.UpdateActivity();
        heartbeat_.Update();
    }

    // ✅ 成员变量：彻底移除了 IIOHandler
    mutable std::mutex mutex_;
    std::unique_ptr<net::Socket> socket_;  // ✅ 唯一的网络抽象层
    std::shared_ptr<IBufferManager> buffer_mgr_;
    std::shared_ptr<async::IScheduler> scheduler_;
    
    SendBufferQueue send_queue_;
    ReceiveBuffer receive_buffer_;
    EventManager event_manager_;
    Heartbeat heartbeat_;
    Identity identity_;
    StateMachine state_machine_;
    Statistics stats_;
    UserData user_data_;
    
    ConnectionConfig config_;
};

// ✅ 新的工厂方法：只接受 Socket，不再接受 IIOHandler
std::shared_ptr<IConnection> IConnection::Create(
    std::unique_ptr<net::Socket> socket,
    std::shared_ptr<IBufferManager> buffer_manager,
    std::shared_ptr<async::IScheduler> scheduler,
    std::shared_ptr<IEventDispatcher> dispatcher) {
    
    return std::make_shared<TcpConnection>(
        std::move(socket),
        std::move(buffer_manager),
        std::move(scheduler),
        std::move(dispatcher));
}

} // namespace httpserver::core