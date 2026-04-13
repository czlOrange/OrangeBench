// src/core/Connection/connection_manager.cpp
#include "httpserver/core/Connection/connection_manager.hpp"
#include "httpserver/core/Tcp/tcp_connection.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

namespace httpserver::core {

// ✅ 提供 Impl 的完整定义
struct DefaultConnectionManager::Impl {
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> connections;
    std::shared_mutex mutex;
    std::atomic<uint64_t> next_id{1};
};

DefaultConnectionManager::DefaultConnectionManager()
    : impl_(std::make_unique<Impl>()) {}

DefaultConnectionManager::~DefaultConnectionManager() = default;

std::shared_ptr<IConnection> DefaultConnectionManager::GetConnection(uint64_t id) noexcept {
    std::shared_lock lock(impl_->mutex);
    auto it = impl_->connections.find(id);
    return it != impl_->connections.end() ? it->second : nullptr;
}

bool DefaultConnectionManager::HasConnection(uint64_t id) const noexcept {
    std::shared_lock lock(impl_->mutex);
    return impl_->connections.find(id) != impl_->connections.end();
}

std::shared_ptr<IConnection> DefaultConnectionManager::CreateConnection(const std::string& host, uint16_t port) {
    // 实际项目中需要创建 TCPConnection 并调用 Connect
    // 这里返回 nullptr 作为占位
    return nullptr;
}

std::shared_ptr<IConnection> DefaultConnectionManager::AcceptConnection(SocketFd fd, const SocketAddress& addr) {
    // 实际项目中需要创建 TCPConnection
    // 这里返回 nullptr 作为占位
    return nullptr;
}

bool DefaultConnectionManager::CloseConnection(uint64_t id) noexcept {
    std::unique_lock lock(impl_->mutex);
    auto it = impl_->connections.find(id);
    if (it == impl_->connections.end()) return false;
    it->second->Close();
    impl_->connections.erase(it);
    return true;
}

void DefaultConnectionManager::CloseAllConnections() noexcept {
    std::unique_lock lock(impl_->mutex);
    for (auto& [id, conn] : impl_->connections) {
        conn->Close();
    }
    impl_->connections.clear();
}

std::vector<uint64_t> DefaultConnectionManager::GetAllConnectionIds() const {
    std::shared_lock lock(impl_->mutex);
    std::vector<uint64_t> ids;
    ids.reserve(impl_->connections.size());
    for (const auto& [id, _] : impl_->connections) {
        ids.push_back(id);
    }
    return ids;
}

size_t DefaultConnectionManager::GetConnectionCount() const noexcept {
    std::shared_lock lock(impl_->mutex);
    return impl_->connections.size();
}

void DefaultConnectionManager::RegisterConnection(std::shared_ptr<IConnection> conn) {
    if (!conn) return;
    uint64_t id = conn->GetConnectionId();
    if (id == 0) id = impl_->next_id++;
    conn->SetConnectionId(id);
    std::unique_lock lock(impl_->mutex);
    impl_->connections[id] = std::move(conn);
}

void DefaultConnectionManager::UnregisterConnection(uint64_t id) noexcept {
    std::unique_lock lock(impl_->mutex);
    impl_->connections.erase(id);
}

// ============================================================================
// 以下方法为占位实现（可按需完善）
// ============================================================================

std::vector<std::shared_ptr<IConnection>> DefaultConnectionManager::GetConnectionsByIp(const std::string&) const {
    return {};
}

std::vector<std::shared_ptr<IConnection>> DefaultConnectionManager::GetConnectionsByPort(uint16_t) const {
    return {};
}

std::vector<std::shared_ptr<IConnection>> DefaultConnectionManager::GetConnectionsByAddress(const std::string&, uint16_t) const {
    return {};
}

std::vector<std::shared_ptr<IConnection>> DefaultConnectionManager::GetConnectionsByState(ConnectionState) const {
    return {};
}

void DefaultConnectionManager::SetMaxConnections(size_t) {}

size_t DefaultConnectionManager::GetMaxConnections() const noexcept {
    return 0;
}

void DefaultConnectionManager::SetDefaultTimeout(std::chrono::milliseconds) {}

std::chrono::milliseconds DefaultConnectionManager::GetDefaultTimeout() const noexcept {
    return {};
}

void DefaultConnectionManager::SetConnectionTimeout(uint64_t, std::chrono::milliseconds) {}

std::chrono::milliseconds DefaultConnectionManager::GetConnectionTimeout(uint64_t) const {
    return {};
}

void DefaultConnectionManager::StartHeartbeat(std::chrono::milliseconds) {}

void DefaultConnectionManager::StopHeartbeat() noexcept {}

void DefaultConnectionManager::PerformHeartbeatCheck() {}

void DefaultConnectionManager::SetHeartbeatTimeoutCallback(HeartbeatTimeoutCallback) {}

std::vector<std::shared_ptr<IConnection>> DefaultConnectionManager::GetActiveConnections(std::chrono::milliseconds) const {
    return {};
}

std::vector<std::shared_ptr<IConnection>> DefaultConnectionManager::GetIdleConnections(std::chrono::milliseconds) const {
    return {};
}

void DefaultConnectionManager::Subscribe(EventCallback) {}

IConnectionManager::ManagerStats DefaultConnectionManager::GetStats() const {
    ManagerStats stats;
    stats.total_connections = GetConnectionCount();
    return stats;
}

void DefaultConnectionManager::ResetStats() noexcept {}

} // namespace httpserver::core