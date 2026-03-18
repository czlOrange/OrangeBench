// src/httpserver/core/connections/connection.hpp
#pragma once

#include "connection_state.hpp"
#include "io_handler.hpp"
#include "buffer_manager.hpp"
#include "async_operation.hpp"
#include "event_dispatcher.hpp"
#include <memory>
#include <string>
#include <system_error>
#include <expected>

namespace httpserver::core {

// 前向声明
class IConnectionManager;

// 连接接口
class IConnection : public std::enable_shared_from_this<IConnection> {
public:
    virtual ~IConnection() = default;
    
    // 连接生命周期
    virtual std::error_code Connect(const std::string& host, uint16_t port) = 0;
    virtual std::error_code Connect(const SocketAddress& addr) = 0;
    virtual std::error_code Disconnect() noexcept = 0;
    virtual void Close() noexcept = 0;
    
    // 同步数据操作
    virtual std::expected<size_t, std::error_code> 
        Send(std::string_view data) = 0;
    virtual std::expected<size_t, std::error_code> 
        Send(const void* data, size_t len) = 0;
    virtual std::expected<std::string, std::error_code> 
        Receive(size_t max_len = 4096) = 0;
    virtual std::expected<size_t, std::error_code> 
        Receive(void* buffer, size_t len) = 0;
    
    // 异步数据操作（使用调度器）
    virtual AsyncResult<size_t> SendAsync(std::string_view data) = 0;
    virtual AsyncResult<size_t> SendAsync(const void* data, size_t len) = 0;
    virtual AsyncResult<std::string> ReceiveAsync(size_t max_len = 4096) = 0;
    virtual AsyncResult<size_t> ReceiveAsync(void* buffer, size_t len) = 0;
    
    // 缓冲区管理
    virtual std::error_code Flush() = 0;
    virtual void ClearBuffers() noexcept = 0;
    virtual size_t GetPendingSendBytes() const noexcept = 0;
    virtual size_t GetPendingReceiveBytes() const noexcept = 0;
    
    // 配置管理
    virtual void Configure(const ConnectionConfig& config) = 0;
    virtual ConnectionConfig GetConfig() const = 0;
    virtual void UpdateConfig(std::function<void(ConnectionConfig&)> updater) = 0;
    
    // 状态查询
    virtual ConnectionState GetState() const = 0;
    virtual bool IsConnected() const = 0;
    virtual bool IsReadable() const = 0;
    virtual bool IsWritable() const = 0;
    virtual bool HasError() const = 0;
    
    // 地址信息
    virtual std::string GetLocalAddress() const = 0;
    virtual std::string GetPeerAddress() const = 0;
    virtual uint16_t GetLocalPort() const noexcept = 0;
    virtual uint16_t GetPeerPort() const noexcept = 0;
    virtual SocketAddress GetLocalSocketAddress() const = 0;
    virtual SocketAddress GetPeerSocketAddress() const = 0;
    
    // 统计信息
    virtual size_t GetBytesSent() const noexcept = 0;
    virtual size_t GetBytesReceived() const noexcept = 0;
    virtual size_t GetTotalOperations() const noexcept = 0;
    virtual std::chrono::steady_clock::time_point GetConnectTime() const = 0;
    virtual std::chrono::steady_clock::time_point GetLastActivityTime() const = 0;
    
    // 超时等待
    virtual bool WaitForData(std::chrono::milliseconds timeout) = 0;
    virtual bool WaitForWritable(std::chrono::milliseconds timeout) = 0;
    
    // 文件描述符
    virtual int GetFd() const = 0;
    
    // 事件回调（通过事件管理器）
    virtual void SetEventCallback(EventCallback callback) = 0;
    virtual void SetDataCallback(DataCallback callback) = 0;
    virtual void SetErrorCallback(ErrorCallback callback) = 0;
    
    // 连接ID
    virtual uint64_t GetConnectionId() const noexcept = 0;
    virtual void SetConnectionId(uint64_t id) = 0;
    
    // 管理器关联
    virtual void SetConnectionManager(std::shared_ptr<IConnectionManager> manager) = 0;
    virtual std::shared_ptr<IConnectionManager> GetConnectionManager() const = 0;
    
    // 自定义数据
    virtual void SetUserData(const std::string& key, std::any data) = 0;
    virtual std::any GetUserData(const std::string& key) const = 0;
    virtual bool HasUserData(const std::string& key) const = 0;
    virtual void RemoveUserData(const std::string& key) = 0;
    
    // 心跳
    virtual void UpdateHeartbeat() = 0;
    virtual bool IsHeartbeatExpired(std::chrono::milliseconds timeout) const = 0;
    
    // 工厂方法
    static std::shared_ptr<IConnection> Create(
        std::shared_ptr<IIOHandler> io_handler,
        std::shared_ptr<IBufferManager> buffer_manager,
        std::shared_ptr<IAsyncScheduler> scheduler,
        std::shared_ptr<IEventDispatcher> dispatcher);
};

} // namespace httpserver::core