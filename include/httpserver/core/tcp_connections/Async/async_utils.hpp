// include/httpserver/core/async/async_utils.hpp
#pragma once

#include "async_common.hpp"
#include "async_scheduler.hpp"
#include "async_operation.hpp"
#include <string>
#include <vector>
#include <mutex>

namespace httpserver::core::async {

// 连接操作的工具包装类
class ConnectionOperations {
public:
    explicit ConnectionOperations(std::shared_ptr<IScheduler> scheduler);
    ~ConnectionOperations() = default;
    
    // 禁用拷贝
    ConnectionOperations(const ConnectionOperations&) = delete;
    ConnectionOperations& operator=(const ConnectionOperations&) = delete;
    
    // 发送操作
    Result<size_t> SendAsync(const std::string& data);
    Result<size_t> SendAsync(const void* data, size_t size);
    
    // 接收操作
    Result<std::string> ReceiveAsync(size_t max_size);
    Result<size_t> ReceiveAsync(void* buffer, size_t size);
    
    // 连接操作
    Result<bool> ConnectAsync(const std::string& host, uint16_t port);
    Result<void> DisconnectAsync();
    
    // 批量操作
    Result<std::vector<size_t>> SendBatchAsync(
        const std::vector<std::string>& data_list);
    
    // 操作管理
    void CancelAll();
    size_t PendingCount() const;
    bool WaitAll(std::chrono::milliseconds timeout);
    
private:
    std::shared_ptr<IScheduler> scheduler_;
    std::vector<std::shared_ptr<IOperation>> pending_ops_;
    mutable std::mutex ops_mutex_;
    
    void Track(std::shared_ptr<IOperation> op);
    void Untrack(std::shared_ptr<IOperation> op);
};

} // namespace httpserver::core::async