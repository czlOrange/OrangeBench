// src/core/tcp_connections/Async/async_utils.cpp
#include "httpserver/core/tcp_connections/Async/async_utils.hpp"
#include <algorithm>
#include <cstring>      
#include <thread>       
namespace httpserver::core::async {

ConnectionOperations::ConnectionOperations(std::shared_ptr<IScheduler> scheduler)
    : scheduler_(std::move(scheduler)) {
}

Result<size_t> ConnectionOperations::SendAsync(const std::string& data) {
    return SendAsync(data.data(), data.size());
}

Result<size_t> ConnectionOperations::SendAsync(const void* data, size_t size) {
    // 创建异步任务
    return scheduler_->Schedule([data, size]() -> size_t {
        // 这里是模拟发送逻辑，实际应该调用网络发送函数
        // 假设成功发送所有数据
        // 实际实现中应该调用 IIOHandler::Send 等
        return size;
    });
}

Result<std::string> ConnectionOperations::ReceiveAsync(size_t max_size) {
    // 创建异步任务
    return scheduler_->Schedule([max_size]() -> std::string {
        // 这里是模拟接收逻辑，实际应该调用网络接收函数
        // 假设收到数据
        std::string result(max_size, 'A'); // 模拟数据
        return result;
    });
}

Result<size_t> ConnectionOperations::ReceiveAsync(void* buffer, size_t size) {
    // 创建异步任务
    return scheduler_->Schedule([buffer, size]() -> size_t {
        // 模拟接收数据
        // 实际实现中应该调用 IIOHandler::Recv
        std::memset(buffer, 'A', size);
        return size;
    });
}

Result<bool> ConnectionOperations::ConnectAsync(const std::string& host, uint16_t port) {
    // 创建异步任务
    return scheduler_->Schedule([host, port]() -> bool {
        // 模拟连接逻辑
        // 实际实现中应该调用 IIOHandler::Connect
        return true;
    });
}

Result<void> ConnectionOperations::DisconnectAsync() {
    // 创建异步任务
    return scheduler_->Schedule([]() -> void {
        // 模拟断开连接
    });
}

Result<std::vector<size_t>> ConnectionOperations::SendBatchAsync(
    const std::vector<std::string>& data_list) {
    
    std::vector<std::future<size_t>> futures;
    for (const auto& data : data_list) {
        futures.push_back(scheduler_->Schedule([data]() -> size_t {
            // 模拟发送
            return data.size();
        }));
    }
    
    // ✅ 关键修复：将 futures 移动到 lambda，并标记 mutable
    return scheduler_->Schedule([futures = std::move(futures)]() mutable -> std::vector<size_t> {
        std::vector<size_t> results;
        results.reserve(futures.size());
        for (auto& f : futures) {
            results.push_back(f.get());  // 现在可以调用了
        }
        return results;
    });
}

void ConnectionOperations::CancelAll() {
    std::lock_guard<std::mutex> lock(ops_mutex_);
    for (auto& op : pending_ops_) {
        op->Cancel();
    }
    pending_ops_.clear();
}

size_t ConnectionOperations::PendingCount() const {
    std::lock_guard<std::mutex> lock(ops_mutex_);
    return pending_ops_.size();
}

bool ConnectionOperations::WaitAll(std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> lock(ops_mutex_);
            if (pending_ops_.empty()) {
                return true;
            }
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed >= timeout) {
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ConnectionOperations::Track(std::shared_ptr<IOperation> op) {
    std::lock_guard<std::mutex> lock(ops_mutex_);
    pending_ops_.push_back(std::move(op));
}

void ConnectionOperations::Untrack(std::shared_ptr<IOperation> op) {
    std::lock_guard<std::mutex> lock(ops_mutex_);
    auto it = std::find(pending_ops_.begin(), pending_ops_.end(), op);
    if (it != pending_ops_.end()) {
        pending_ops_.erase(it);
    }
}

} // namespace httpserver::core::async