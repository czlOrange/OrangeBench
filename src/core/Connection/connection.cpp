// src/httpserver/core/connections/connection.cpp
#include "httpserver/core/Connection/connection_interface.hpp"
#include "Component/buffer_manager.cpp"   // 包含组件实现（因为无头文件）
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

// 辅助错误码生成 
static std::error_code make_error_code(ConnectionError err) {
    return std::error_code(static_cast<int>(err), std::generic_category());
}

class TcpConnection : public IConnection, public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(std::shared_ptr<IIOHandler> io_handler,
                  std::shared_ptr<IBufferManager> buffer_mgr,
                  std::shared_ptr<async::IScheduler> scheduler,
                  std::shared_ptr<IEventDispatcher> dispatcher)
        : io_handler_(std::move(io_handler))
        , buffer_mgr_(std::move(buffer_mgr))
        , scheduler_(std::move(scheduler))
        , event_manager_(std::move(dispatcher))
        , fd_(-1)
        , config_()
    {
        if (!io_handler_) io_handler_ = IIOHandler::CreateDefault();
        if (!buffer_mgr_) buffer_mgr_ = IBufferManager::CreateDefault();
        if (!scheduler_) scheduler_ = async::IScheduler::CreateDefault();
        if (!event_manager_.GetDispatcher()) {
            event_manager_ = EventManager(IEventDispatcher::CreateDefault());
        }
    }

    ~TcpConnection() override { Close(); }

    // ---------- 生命周期 ----------
    std::error_code Connect(const std::string& host, uint16_t port) override {
        auto addr = SocketAddress::FromIpPort(host, port);
        return Connect(addr);
    }

    std::error_code Connect(const SocketAddress& addr) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_machine_.GetState() != ConnectionState::DISCONNECTED) {
            return make_error_code(ConnectionError::ALREADY_CONNECTED);
        }
        auto fd_res = io_handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
        if (fd_res.has_error()) return fd_res.error();
        fd_ = fd_res.value();

        auto set_nonblock = io_handler_->SetNonBlocking(fd_, true);
        if (set_nonblock.has_error()) {
            io_handler_->CloseSocket(fd_);
            fd_ = -1;
            return set_nonblock.error();
        }
        if (config_.no_delay) {
            io_handler_->SetTcpNoDelay(fd_, true);
        }

        auto connect_res = io_handler_->Connect(fd_, reinterpret_cast<const sockaddr*>(&addr.addr), addr.len);
        if (connect_res.has_error()) {
            if (connect_res.error().value() != EINPROGRESS) {
                io_handler_->CloseSocket(fd_);
                fd_ = -1;
                return connect_res.error();
            }
            state_machine_.SetConnecting();
            auto weak_self = weak_from_this();
            event_manager_.RegisterWrite(fd_, [weak_self]() {
                if (auto self = weak_self.lock()) {
                    static_cast<TcpConnection*>(self.get())->onConnectComplete();
                }
            });
            return std::error_code();
        } else {
            state_machine_.SetConnected();
            stats_.SetConnectTime(std::chrono::steady_clock::now());
            updateActivity();
            registerReadEvent();
            event_manager_.NotifyEvent(ConnectionEvent::CONNECTED, shared_from_this(), "");
            return std::error_code();
        }
    }

    std::error_code Disconnect() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_machine_.GetState() == ConnectionState::DISCONNECTED) return std::error_code();
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
        auto result = io_handler_->Send(fd_, data, len, 0);
        if (result.has_error()) {
            auto err = result.error();
            if (err.value() == EAGAIN || err.value() == EWOULDBLOCK) {
                if (!send_queue_.EnqueueSend({static_cast<const char*>(data), len})) {
                    return std::unexpected(make_error_code(ConnectionError::SEND_QUEUE_FULL));
                }
                registerWriteEvent();
                return len;
            }
            return std::unexpected(err);
        }
        stats_.RecordSent(result.value());
        stats_.RecordOperation();
        updateActivity();
        return result.value();
    }

    std::expected<std::string, std::error_code> Receive(size_t max_len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        std::string buffer(max_len, '\0');
        auto result = io_handler_->Recv(fd_, buffer.data(), max_len, 0);
        if (result.has_error()) {
            auto err = result.error();
            if (err.value() == EAGAIN || err.value() == EWOULDBLOCK) {
                return std::unexpected(make_error_code(ConnectionError::WOULD_BLOCK));
            }
            return std::unexpected(err);
        }
        if (result.value() == 0) {
            closeInternal();
            return std::unexpected(make_error_code(ConnectionError::CONNECTION_RESET));
        }
        buffer.resize(result.value());
        stats_.RecordReceived(result.value());
        stats_.RecordOperation();
        updateActivity();
        return buffer;
    }

    std::expected<size_t, std::error_code> Receive(void* buffer, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) {
            return std::unexpected(make_error_code(ConnectionError::NOT_CONNECTED));
        }
        auto result = io_handler_->Recv(fd_, buffer, len, 0);
        if (result.has_error()) {
            auto err = result.error();
            if (err.value() == EAGAIN || err.value() == EWOULDBLOCK) {
                return std::unexpected(make_error_code(ConnectionError::WOULD_BLOCK));
            }
            return std::unexpected(err);
        }
        if (result.value() == 0) {
            closeInternal();
            return std::unexpected(make_error_code(ConnectionError::CONNECTION_RESET));
        }
        stats_.RecordReceived(result.value());
        stats_.RecordOperation();
        updateActivity();
        return result.value();
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
            auto result = io_handler_->Send(fd_, view.data, view.size, 0);
            if (result.has_error()) {
                if (result.error().value() == EAGAIN || result.error().value() == EWOULDBLOCK) {
                    registerWriteEvent();
                    break;
                }
                return result.error();
            }
            send_queue_.ConsumeSend(result.value());
            stats_.RecordSent(result.value());
            stats_.RecordOperation();
            updateActivity();
        }
        if (send_queue_.IsSendQueueEmpty()) {
            event_manager_.ModifyToReadOnly(fd_);
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
        if (fd_ != -1) {
            io_handler_->SetTcpNoDelay(fd_, config_.no_delay);
        }
    }

    ConnectionConfig GetConfig() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    void UpdateConfig(std::function<void(ConnectionConfig&)> updater) override {
        std::lock_guard<std::mutex> lock(mutex_);
        updater(config_);
        if (fd_ != -1) {
            io_handler_->SetTcpNoDelay(fd_, config_.no_delay);
        }
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
        return state_machine_.HasError();
    }

    // ---------- 地址 ----------
    std::string GetLocalAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return "";
        auto addr = io_handler_->GetLocalAddress(fd_);
        return addr ? addr->ToString() : "";
    }

    std::string GetPeerAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return "";
        auto addr = io_handler_->GetPeerAddress(fd_);
        return addr ? addr->ToString() : "";
    }

    uint16_t GetLocalPort() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return 0;
        auto addr = io_handler_->GetLocalAddress(fd_);
        return addr ? addr->GetPort() : 0;
    }

    uint16_t GetPeerPort() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return 0;
        auto addr = io_handler_->GetPeerAddress(fd_);
        return addr ? addr->GetPort() : 0;
    }

    SocketAddress GetLocalSocketAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return SocketAddress();
        auto addr = io_handler_->GetLocalAddress(fd_);
        return addr ? *addr : SocketAddress();
    }

    SocketAddress GetPeerSocketAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ == -1) return SocketAddress();
        auto addr = io_handler_->GetPeerAddress(fd_);
        return addr ? *addr : SocketAddress();
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

    // ---------- 文件描述符 ----------
    int GetFd() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return fd_;
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
    // 内部辅助函数
    void closeInternal() {
        if (fd_ != -1) {
            event_manager_.Unregister(fd_);
            io_handler_->CloseSocket(fd_);
            fd_ = -1;
        }
        state_machine_.SetDisconnected();
        send_queue_.ClearAll();
        receive_buffer_.ClearAll();
        event_manager_.NotifyEvent(ConnectionEvent::DISCONNECTED, shared_from_this(), "");
    }

    void registerReadEvent() {
        auto weak_self = weak_from_this();
        event_manager_.RegisterRead(fd_, [weak_self]() {
            if (auto self = weak_self.lock()) {
                static_cast<TcpConnection*>(self.get())->onReadEvent();
            }
        });
    }

    void registerWriteEvent() {
        auto weak_self = weak_from_this();
        event_manager_.RegisterWrite(fd_, [weak_self]() {
            if (auto self = weak_self.lock()) {
                static_cast<TcpConnection*>(self.get())->onWriteEvent();
            }
        });
    }

    void onConnectComplete() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_machine_.GetState() == ConnectionState::CONNECTING) {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                state_machine_.SetConnected();
                stats_.SetConnectTime(std::chrono::steady_clock::now());
                updateActivity();
                registerReadEvent();
                event_manager_.NotifyEvent(ConnectionEvent::CONNECTED, shared_from_this(), "");
            } else {
                state_machine_.SetError();
                event_manager_.NotifyError(shared_from_this(), make_error_code(ConnectionError::CONNECT_FAILED));
            }
        }
    }

    void onReadEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) return;

        auto region = receive_buffer_.GetReceiveBuffer();
        if (region.size == 0) return;
        auto result = io_handler_->Recv(fd_, const_cast<char*>(region.data), region.size, 0);
        if (result.has_error()) {
            auto err = result.error();
            if (err.value() == EAGAIN || err.value() == EWOULDBLOCK) return;
            closeInternal();
            event_manager_.NotifyError(shared_from_this(), err);
            return;
        }
        if (result.value() == 0) {
            closeInternal();
            event_manager_.NotifyEvent(ConnectionEvent::DISCONNECTED, shared_from_this(), "");
            return;
        }
        receive_buffer_.CommitReceive(result.value());
        stats_.RecordReceived(result.value());
        stats_.RecordOperation();
        updateActivity();

        auto read_view = receive_buffer_.GetReceiveBuffer(); // 注意：这里得到的是整个可读区域，实际应返回已提交的数据
        // 简化：直接通知全部可读数据（生产环境中可能需要更精细控制）
        event_manager_.NotifyData(shared_from_this(), std::string_view(read_view.data, receive_buffer_.GetReceiveBufferSize()));
    }

    void onWriteEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_machine_.IsConnected()) return;

        while (!send_queue_.IsSendQueueEmpty()) {
            auto view = send_queue_.PeekSend();
            if (view.size == 0) break;
            auto result = io_handler_->Send(fd_, view.data, view.size, 0);
            if (result.has_error()) {
                auto err = result.error();
                if (err.value() == EAGAIN || err.value() == EWOULDBLOCK) return;
                closeInternal();
                event_manager_.NotifyError(shared_from_this(), err);
                return;
            }
            send_queue_.ConsumeSend(result.value());
            stats_.RecordSent(result.value());
            stats_.RecordOperation();
            updateActivity();
            event_manager_.NotifyEvent(ConnectionEvent::DATA_SENT, shared_from_this(), "");
        }
        if (send_queue_.IsSendQueueEmpty()) {
            event_manager_.ModifyToReadOnly(fd_);
        }
    }

    void updateActivity() {
        stats_.UpdateActivity();
        heartbeat_.Update();
    }

    // 成员变量
    mutable std::mutex mutex_;
    std::shared_ptr<IIOHandler> io_handler_;
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

    int fd_;
    ConnectionConfig config_;
};

// 工厂方法
std::shared_ptr<IConnection> IConnection::Create(
    std::shared_ptr<IIOHandler> io_handler,
    std::shared_ptr<IBufferManager> buffer_manager,
    std::shared_ptr<async::IScheduler> scheduler,
    std::shared_ptr<IEventDispatcher> dispatcher) {
    return std::make_shared<TcpConnection>(
        std::move(io_handler),
        std::move(buffer_manager),
        std::move(scheduler),
        std::move(dispatcher));
}

} // namespace httpserver::core