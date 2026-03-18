// src/httpserver/core/connections/connection_manager.hpp
#pragma once

#include "connection_standard.hpp"  // IConnection, ConnectionState, SocketAddress
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <chrono>

namespace httpserver::core {

// 前向声明
class IConnectionManager;

// 连接管理器接口
// 负责管理所有TCP连接的生命周期，提供单个、全局、分组及监控能力
class IConnectionManager : public std::enable_shared_from_this<IConnectionManager> {
public:
    virtual ~IConnectionManager() = default;

    // ────────────────────────────────────────────
    // 1. 对单个连接的操作（一人）
    // ────────────────────────────────────────────

    // 获取指定连接ID的连接（返回shared_ptr，若不存在返回nullptr）
    virtual std::shared_ptr<IConnection> GetConnection(uint64_t connection_id) noexcept = 0;

    // 检查指定连接ID是否存在
    virtual bool HasConnection(uint64_t connection_id) const noexcept = 0;

    // 主动发起TCP连接（客户端模式），返回新连接的shared_ptr
    virtual std::shared_ptr<IConnection> CreateConnection(
        const std::string& host, uint16_t port) = 0;

    // 被动接受TCP请求（服务器模式），将已接受的客户端fd包装为连接
    virtual std::shared_ptr<IConnection> AcceptConnection(
        SocketFd client_fd, const SocketAddress& peer_addr) = 0;

    // 关闭指定连接（从管理器中移除并关闭）
    virtual bool CloseConnection(uint64_t connection_id) noexcept = 0;

    // ────────────────────────────────────────────
    // 2. 对所有连接统一管理（一群）
    // ────────────────────────────────────────────

    // 关闭所有连接
    virtual void CloseAllConnections() noexcept = 0;

    // 获取所有连接的ID列表
    virtual std::vector<uint64_t> GetAllConnectionIds() const = 0;

    // 获取当前连接总数
    virtual size_t GetConnectionCount() const noexcept = 0;

    // 将新连接注册到管理器（通常由工厂或接受连接时调用）
    virtual void RegisterConnection(std::shared_ptr<IConnection> connection) = 0;

    // 将指定连接从管理器移除（不关闭连接，仅移除跟踪）
    virtual void UnregisterConnection(uint64_t connection_id) noexcept = 0;

    // 获取指定IP地址的所有连接
    virtual std::vector<std::shared_ptr<IConnection>> GetConnectionsByIp(
        const std::string& ip) const = 0;

    // 获取指定端口号的所有连接
    virtual std::vector<std::shared_ptr<IConnection>> GetConnectionsByPort(
        uint16_t port) const = 0;

    // 获取指定地址（IP+端口）的所有连接
    virtual std::vector<std::shared_ptr<IConnection>> GetConnectionsByAddress(
        const std::string& ip, uint16_t port) const = 0;

    // 获取指定状态的所有连接
    virtual std::vector<std::shared_ptr<IConnection>> GetConnectionsByState(
        ConnectionState state) const = 0;

    // ────────────────────────────────────────────
    // 3. 连接池与生命周期管理
    // ────────────────────────────────────────────

    // 设置全局最大连接数（0表示无限制）
    virtual void SetMaxConnections(size_t max) = 0;

    // 获取当前最大连接数
    virtual size_t GetMaxConnections() const noexcept = 0;

    // 设置连接默认超时时间（毫秒，0表示无超时）
    virtual void SetDefaultTimeout(std::chrono::milliseconds timeout) = 0;

    // 获取连接默认超时时间
    virtual std::chrono::milliseconds GetDefaultTimeout() const noexcept = 0;

    // 设置单个连接的超时时间（覆盖默认）
    virtual void SetConnectionTimeout(uint64_t connection_id,
                                      std::chrono::milliseconds timeout) = 0;

    // 获取单个连接的剩余超时时间（若未设置则返回默认）
    virtual std::chrono::milliseconds GetConnectionTimeout(
        uint64_t connection_id) const = 0;

    // ────────────────────────────────────────────
    // 4. 心跳检测
    // ────────────────────────────────────────────

    // 启动心跳检测，每隔interval检查一次所有连接
    virtual void StartHeartbeat(std::chrono::milliseconds interval) = 0;

    // 停止心跳检测
    virtual void StopHeartbeat() noexcept = 0;

    // 手动执行一次心跳检测（检查超时、空闲等）
    virtual void PerformHeartbeatCheck() = 0;

    // 设置心跳超时回调（当连接超过超时时间未活动时调用）
    using HeartbeatTimeoutCallback = std::function<void(uint64_t connection_id)>;
    virtual void SetHeartbeatTimeoutCallback(HeartbeatTimeoutCallback callback) = 0;

    // 获取指定时间间隔内所有活跃连接（最后活动时间在interval内）
    virtual std::vector<std::shared_ptr<IConnection>> GetActiveConnections(
        std::chrono::milliseconds interval) const = 0;

    // 获取指定时间间隔内所有空闲连接（最后活动时间超过interval）
    virtual std::vector<std::shared_ptr<IConnection>> GetIdleConnections(
        std::chrono::milliseconds interval) const = 0;

    // ────────────────────────────────────────────
    // 5. 事件订阅与通知（可扩展）
    // ────────────────────────────────────────────

    // 连接事件类型
    enum class ConnectionEvent {
        Added,      // 新连接加入
        Removed,    // 连接被移除
        StateChanged, // 状态变化
        Timeout     // 超时
    };

    // 事件回调函数类型
    using EventCallback = std::function<void(ConnectionEvent event,
                                             uint64_t connection_id,
                                             std::shared_ptr<IConnection> conn)>;

    // 订阅连接事件
    virtual void Subscribe(EventCallback callback) = 0;

    // 取消订阅（如果支持，可以传递句柄，这里简化）
    // 实际项目中可能返回订阅ID，这里略

    // ────────────────────────────────────────────
    // 6. 统计信息
    // ────────────────────────────────────────────

    struct ManagerStats {
        size_t total_connections{0};        // 当前总连接数
        size_t max_connections{0};          // 最大连接数限制
        size_t active_connections{0};        // 活跃连接数（可根据最后活动时间定义）
        size_t idle_connections{0};           // 空闲连接数
        std::unordered_map<ConnectionState, size_t> state_counts; // 各状态计数
    };

    virtual ManagerStats GetStats() const = 0;

    // 重置统计信息（如清零计数）
    virtual void ResetStats() noexcept = 0;
};

// 默认连接管理器实现（具体实现在.cpp文件中）
class DefaultConnectionManager : public IConnectionManager {
public:
    DefaultConnectionManager();
    ~DefaultConnectionManager() override;

    // 禁用拷贝
    DefaultConnectionManager(const DefaultConnectionManager&) = delete;
    DefaultConnectionManager& operator=(const DefaultConnectionManager&) = delete;

    // IConnectionManager 接口实现
    std::shared_ptr<IConnection> GetConnection(uint64_t connection_id) noexcept override;
    bool HasConnection(uint64_t connection_id) const noexcept override;
    std::shared_ptr<IConnection> CreateConnection(const std::string& host, uint16_t port) override;
    std::shared_ptr<IConnection> AcceptConnection(SocketFd client_fd, const SocketAddress& peer_addr) override;
    bool CloseConnection(uint64_t connection_id) noexcept override;

    void CloseAllConnections() noexcept override;
    std::vector<uint64_t> GetAllConnectionIds() const override;
    size_t GetConnectionCount() const noexcept override;
    void RegisterConnection(std::shared_ptr<IConnection> connection) override;
    void UnregisterConnection(uint64_t connection_id) noexcept override;

    std::vector<std::shared_ptr<IConnection>> GetConnectionsByIp(const std::string& ip) const override;
    std::vector<std::shared_ptr<IConnection>> GetConnectionsByPort(uint16_t port) const override;
    std::vector<std::shared_ptr<IConnection>> GetConnectionsByAddress(const std::string& ip, uint16_t port) const override;
    std::vector<std::shared_ptr<IConnection>> GetConnectionsByState(ConnectionState state) const override;

    void SetMaxConnections(size_t max) override;
    size_t GetMaxConnections() const noexcept override;
    void SetDefaultTimeout(std::chrono::milliseconds timeout) override;
    std::chrono::milliseconds GetDefaultTimeout() const noexcept override;
    void SetConnectionTimeout(uint64_t connection_id, std::chrono::milliseconds timeout) override;
    std::chrono::milliseconds GetConnectionTimeout(uint64_t connection_id) const override;

    void StartHeartbeat(std::chrono::milliseconds interval) override;
    void StopHeartbeat() noexcept override;
    void PerformHeartbeatCheck() override;
    void SetHeartbeatTimeoutCallback(HeartbeatTimeoutCallback callback) override;
    std::vector<std::shared_ptr<IConnection>> GetActiveConnections(std::chrono::milliseconds interval) const override;
    std::vector<std::shared_ptr<IConnection>> GetIdleConnections(std::chrono::milliseconds interval) const override;

    void Subscribe(EventCallback callback) override;

    ManagerStats GetStats() const override;
    void ResetStats() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace httpserver::core