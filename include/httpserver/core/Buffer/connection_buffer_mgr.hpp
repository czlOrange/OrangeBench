// ============================================================================
// 文件: include/httpserver/core/connection_buffer_mgr.hpp
// 描述: 连接专用缓冲区管理器 - 管理单个连接的发送队列和接收缓冲区
// ============================================================================

#pragma once

#include "buffer_factory.hpp"      // 用于创建Buffer对象（需要工厂来分配内存）
#include "buffer_operator.hpp"     // 用于操作Buffer数据（预留，将来可能需要）
#include "buffer_manager.hpp"      // 外观接口（用于向后兼容的构造函数）
#include <deque>                   // 双端队列，用作发送队列（从两端操作）
#include <functional>              // std::function，用于存储接收策略函数
#include <memory>                  // 智能指针相关

namespace httpserver::core {

/**
 * @brief 每个网络连接专用的缓冲区管理器
 * 
 * 负责管理一个网络连接的发送数据队列和接收缓冲区，
 * 协调数据的入队、出队和内存复用。
 */
class ConnectionBufferManager {
public:

    //======================================================================
    // ----- 构造与析构 -----
    //======================================================================
    
    // 构造函数：注入工厂和操作器（操作器可选，不传则使用默认）
    explicit ConnectionBufferManager(std::shared_ptr<IBufferFactory> factory,std::shared_ptr<IBufferOperator> op = nullptr);

    // 析构函数默认实现（所有unique_ptr会自动释放，Buffer自动归还内存）
    ~ConnectionBufferManager() = default;
    
    
    //======================================================================
    // ----- 发送缓冲区管理（管理待发送的数据） -----
    //======================================================================

    // 将数据加入发送队列（内部会克隆一份数据，存入新Buffer）
    bool EnqueueSend(const BufferView& data);

    // 查看队首待发送的数据（返回只读视图，不移动指针）
    BufferView PeekSend() const noexcept;

    // 确认已发送了bytes字节，从队列中移除已发送部分
    void ConsumeSend(size_t bytes);

    // 获取发送队列中待发送的总字节数（所有未发送数据的总和）
    size_t GetSendQueueSize() const noexcept;

    // 判断发送队列是否为空（没有待发送数据）
    bool IsSendQueueEmpty() const noexcept;
    
    
    //======================================================================
    // ----- 接收缓冲区管理（管理接收到的数据） -----
    //====================================================================== 
    
    // 准备接收数据：确保接收缓冲区至少有expected_size的可用空间
    bool PrepareReceive(size_t expected_size);

    // 获取接收缓冲区的可写视图（用于网络层直接写入数据）
    MutableBufferView GetReceiveBuffer() noexcept;

    // 确认已接收了bytes字节，更新接收缓冲区的有效数据大小
    void CommitReceive(size_t bytes);

    // 获取接收缓冲区中已接收的数据大小（有效数据字节数）
    size_t GetReceiveBufferSize() const noexcept;

    // ----- 状态管理（连接关闭或重置时使用） -----

    // 清空所有缓冲区（发送队列和接收缓冲区全部释放）
    void ClearAll() noexcept;

    // 交换两个连接管理器的内部状态（用于移动语义或特殊场景）
    void Swap(ConnectionBufferManager& other) noexcept;


    //======================================================================
    // ----- 配置（调整连接级策略） -----
    //======================================================================

    // 设置发送队列的最大字节数限制（防止内存无限增长）
    void SetMaxSendQueueSize(size_t size);

    // 设置接收缓冲区的扩容策略函数（如：翻倍分配、固定增量等）
    void SetReceiveBufferStrategy(std::function<size_t(size_t)> strategy);

private:
    std::shared_ptr<IBufferFactory> factory_;      // 缓冲区工厂：用于创建新的Buffer
    std::shared_ptr<IBufferOperator> operator_;    // 缓冲区操作器：保留以备将来扩展
    std::deque<std::unique_ptr<Buffer>> send_queue_; // 发送队列：存放待发送的Buffer
    std::unique_ptr<Buffer> receive_buffer_;       // 接收缓冲区：当前用于接收数据的Buffer
    size_t send_consumed_ = 0;                      // 当前队首Buffer已消耗的字节数（部分发送）
    size_t receive_committed_ = 0;                   // 接收缓冲区已写入的有效字节数
    size_t max_send_queue_size_ = 1024 * 1024;      // 发送队列最大字节数限制（默认1MB）
    std::function<size_t(size_t)> receive_strategy_; // 接收缓冲区扩容策略函数
};

} // namespace httpserver::core