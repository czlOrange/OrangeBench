// src/core/connection_buffer_mgr.cpp
#include "../../../../include/httpserver/core/tcp_connections/Buffer/connection_buffer_mgr.hpp"
#include <algorithm>
#include <cstring>
namespace httpserver::core {

// 默认接收缓冲区扩容策略：翻倍
static size_t default_receive_strategy(size_t current) {
    return current == 0 ? 4096 : current * 2;
}

ConnectionBufferManager::ConnectionBufferManager(
    std::shared_ptr<IBufferFactory> factory,
    std::shared_ptr<IBufferOperator> op)
    : factory_(std::move(factory))
    , operator_(std::move(op))
    , receive_strategy_(default_receive_strategy) {
    if (!operator_) {
        operator_ = IBufferOperator::CreateDefault();
    }
    if (!factory_) {
        factory_ = IBufferFactory::CreateDefault();
    }
}

bool ConnectionBufferManager::EnqueueSend(const BufferView& data) {
    if (data.size == 0) return true;
    // 检查发送队列大小限制
    size_t new_total = GetSendQueueSize() + data.size;
    if (new_total > max_send_queue_size_) {
        return false;
    }
    auto buf = factory_->CloneBuffer(data);
    if (!buf) return false;
    send_queue_.push_back(std::move(buf));
    return true;
}

BufferView ConnectionBufferManager::PeekSend() const noexcept {
    if (send_queue_.empty()) {
        return BufferView();
    }
    const auto& front = send_queue_.front();
    if (send_consumed_ >= front->size()) {
        // 不应该发生，调用者应已调用 ConsumeSend 移除已完成的部分
        return BufferView();
    }
    return BufferView(front->data() + send_consumed_,
                      front->size() - send_consumed_);
}

void ConnectionBufferManager::ConsumeSend(size_t bytes) {
    if (bytes == 0) return;
    while (bytes > 0 && !send_queue_.empty()) {
        auto& front = send_queue_.front();
        size_t remaining = front->size() - send_consumed_;
        if (bytes < remaining) {
            send_consumed_ += bytes;
            break;
        }
        bytes -= remaining;
        send_queue_.pop_front();
        send_consumed_ = 0;
    }
}

size_t ConnectionBufferManager::GetSendQueueSize() const noexcept {
    size_t total = 0;
    if (!send_queue_.empty()) {
        // 计算队首未消耗部分
        total += send_queue_.front()->size() - send_consumed_;
        // 其余完整 buffer
        for (size_t i = 1; i < send_queue_.size(); ++i) {
            total += send_queue_[i]->size();
        }
    }
    return total;
}

bool ConnectionBufferManager::IsSendQueueEmpty() const noexcept {
    return send_queue_.empty() && send_consumed_ == 0;
}

bool ConnectionBufferManager::PrepareReceive(size_t expected_size) {
    if (expected_size == 0) return true;
    if (!receive_buffer_) {
        receive_buffer_ = factory_->CreateBuffer(expected_size);
        if (!receive_buffer_) return false;
        receive_committed_ = 0;
        return true;
    }
    size_t free_space = receive_buffer_->capacity() - receive_committed_;
    if (free_space >= expected_size) {
        return true;
    }
    // 需要扩容
    size_t new_capacity = receive_strategy_(receive_buffer_->capacity());
    if (new_capacity < receive_committed_ + expected_size) {
        new_capacity = receive_committed_ + expected_size;
    }
    auto new_buf = factory_->CreateBuffer(new_capacity);
    if (!new_buf) return false;
    // 拷贝现有数据
    std::memcpy(new_buf->data(), receive_buffer_->data(), receive_committed_);
    new_buf->resize(receive_committed_);
    receive_buffer_ = std::move(new_buf);
    return true;
}

MutableBufferView ConnectionBufferManager::GetReceiveBuffer() noexcept {
    if (!receive_buffer_) {
        // 如果没有接收缓冲区，创建一个默认大小的（例如4096）
        if (!PrepareReceive(4096)) {
            return MutableBufferView();
        }
    }
    char* write_ptr = receive_buffer_->data() + receive_committed_;
    size_t free_space = receive_buffer_->capacity() - receive_committed_;
    return MutableBufferView(write_ptr, free_space);
}

void ConnectionBufferManager::CommitReceive(size_t bytes) {
    if (bytes == 0 || !receive_buffer_) return;
    receive_committed_ += bytes;
    receive_buffer_->resize(receive_committed_);
}

size_t ConnectionBufferManager::GetReceiveBufferSize() const noexcept {
    return receive_committed_;
}

void ConnectionBufferManager::ClearAll() noexcept {
    send_queue_.clear();
    receive_buffer_.reset();
    send_consumed_ = 0;
    receive_committed_ = 0;
}

void ConnectionBufferManager::Swap(ConnectionBufferManager& other) noexcept {
    std::swap(factory_, other.factory_);
    std::swap(operator_, other.operator_);
    std::swap(send_queue_, other.send_queue_);
    std::swap(receive_buffer_, other.receive_buffer_);
    std::swap(send_consumed_, other.send_consumed_);
    std::swap(receive_committed_, other.receive_committed_);
    std::swap(max_send_queue_size_, other.max_send_queue_size_);
    std::swap(receive_strategy_, other.receive_strategy_);
}

void ConnectionBufferManager::SetMaxSendQueueSize(size_t size) {
    max_send_queue_size_ = size;
}

void ConnectionBufferManager::SetReceiveBufferStrategy(
    std::function<size_t(size_t)> strategy) {
    receive_strategy_ = strategy ? std::move(strategy) : default_receive_strategy;
}

} // namespace httpserver::core