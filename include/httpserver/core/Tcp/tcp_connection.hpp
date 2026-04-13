// include/httpserver/core/Tcp/tcp_connection.hpp
#pragma once

#include "httpserver/core/Connection/connection_interface.hpp"
#include "httpserver/core/Connection/connection_manager.hpp"
#include "httpserver/core/net/io_handler.hpp"
#include "httpserver/core/Buffer/buffer_manager.hpp"
#include "httpserver/core/Async/async_scheduler.hpp"
#include "httpserver/core/Event/event_dispatcher.hpp"
#include "httpserver/core/Buffer/connection_buffer_mgr.hpp"
#include "httpserver/core/Event/connection_event_mgr.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <any>
#include <chrono>
#include <string_view>

namespace httpserver::core {

class TCPConnection : public IConnection {
public:
    // 主动连接
    TCPConnection(
        std::shared_ptr<IIOHandler> io_handler,
        std::shared_ptr<IBufferManager> buffer_manager,
        std::shared_ptr<async::IScheduler> scheduler,
        std::shared_ptr<IEventDispatcher> dispatcher);

    // 从已有 fd 创建
    TCPConnection(
        int existing_fd,
        const SocketAddress& peer_addr,
        std::shared_ptr<IIOHandler> io_handler,
        std::shared_ptr<IBufferManager> buffer_manager,
        std::shared_ptr<async::IScheduler> scheduler,
        std::shared_ptr<IEventDispatcher> dispatcher);

    ~TCPConnection() override;

    TCPConnection(const TCPConnection&) = delete;
    TCPConnection& operator=(const TCPConnection&) = delete;
    
    // 初始化（必须在 shared_ptr 创建后调用）
    void init();
    
    // 连接生命周期
    std::error_code Connect(const std::string& host, uint16_t port) override;
    std::error_code Connect(const SocketAddress& addr) override;
    std::error_code Disconnect() noexcept override;
    void Close() noexcept override;

    // 同步数据操作（使用 IOResult，即 Result<T>）
    IOResult<size_t> Send(std::string_view data) override;
    IOResult<size_t> Send(const void* data, size_t len) override;
    IOResult<std::string> Receive(size_t max_len) override;
    IOResult<size_t> Receive(void* buffer, size_t len) override;

    // 异步数据操作
    AsyncResult<size_t> SendAsync(std::string_view data) override;
    AsyncResult<size_t> SendAsync(const void* data, size_t len) override;
    AsyncResult<std::string> ReceiveAsync(size_t max_len) override;
    AsyncResult<size_t> ReceiveAsync(void* buffer, size_t len) override;

    // 缓冲区管理
    std::error_code Flush() override;
    void ClearBuffers() noexcept override;
    size_t GetPendingSendBytes() const noexcept override;
    size_t GetPendingReceiveBytes() const noexcept override;

    // 配置管理
    void Configure(const ConnectionConfig& config) override;
    ConnectionConfig GetConfig() const override;
    void UpdateConfig(std::function<void(ConnectionConfig&)> updater) override;

    // 状态查询
    ConnectionState GetState() const override;
    bool IsConnected() const override;
    bool IsReadable() const override;
    bool IsWritable() const override;
    bool HasError() const override;

    // 地址信息
    std::string GetLocalAddress() const override;
    std::string GetPeerAddress() const override;
    uint16_t GetLocalPort() const noexcept override;
    uint16_t GetPeerPort() const noexcept override;
    SocketAddress GetLocalSocketAddress() const override;
    SocketAddress GetPeerSocketAddress() const override;

    // 统计信息
    size_t GetBytesSent() const noexcept override;
    size_t GetBytesReceived() const noexcept override;
    size_t GetTotalOperations() const noexcept override;
    std::chrono::steady_clock::time_point GetConnectTime() const override;
    std::chrono::steady_clock::time_point GetLastActivityTime() const override;

    // 超时等待
    bool WaitForData(std::chrono::milliseconds timeout) override;
    bool WaitForWritable(std::chrono::milliseconds timeout) override;

    // 文件描述符
    int GetFd() const override;

    // 事件回调
    void SetEventCallback(EventCallback callback) override;
    void SetDataCallback(DataCallback callback) override;
    void SetErrorCallback(ErrorCallback callback) override;

    // 连接ID
    uint64_t GetConnectionId() const noexcept override;
    void SetConnectionId(uint64_t id) override;

    // 管理器关联
    void SetConnectionManager(std::shared_ptr<IConnectionManager> manager) override;
    std::shared_ptr<IConnectionManager> GetConnectionManager() const override;

    // 自定义数据
    void SetUserData(const std::string& key, std::any data) override;
    std::any GetUserData(const std::string& key) const override;
    bool HasUserData(const std::string& key) const override;
    void RemoveUserData(const std::string& key) override;

    // 心跳
    void UpdateHeartbeat() override;
    bool IsHeartbeatExpired(std::chrono::milliseconds timeout) const override;

private:
    std::shared_ptr<IIOHandler> io_handler_;
    std::shared_ptr<IBufferManager> buffer_manager_;
    std::shared_ptr<ConnectionBufferManager> connection_buffer_;
    std::shared_ptr<async::IScheduler> scheduler_;
    std::shared_ptr<IEventDispatcher> dispatcher_;
    std::shared_ptr<ConnectionEventManager> event_manager_;
    std::shared_ptr<IConnectionManager> connection_manager_;

    std::atomic<ConnectionState> state_;
    ConnectionConfig config_;
    std::mutex state_mutex_;

    SocketFd socket_fd_;
    SocketAddress local_addr_;
    SocketAddress peer_addr_;

    std::atomic<size_t> bytes_sent_;
    std::atomic<size_t> bytes_received_;
    std::atomic<size_t> total_operations_;
    std::chrono::steady_clock::time_point connect_time_;
    std::chrono::steady_clock::time_point last_activity_time_;
    std::chrono::steady_clock::time_point last_heartbeat_time_;

    uint64_t connection_id_;

    std::unordered_map<std::string, std::any> user_data_;
    mutable std::mutex user_data_mutex_;

    bool setup_socket();
    std::error_code handle_socket_error();
    void update_activity_timestamp();
    
    // 使用 using 声明解决歧义
    using IConnection::shared_from_this;
    
    // 辅助方法：获取自身的 shared_ptr
    std::shared_ptr<TCPConnection> self() {
        return std::dynamic_pointer_cast<TCPConnection>(shared_from_this());
    }
};

} // namespace httpserver::core