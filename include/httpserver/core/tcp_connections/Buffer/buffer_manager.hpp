// ============================================================================
// 文件: include/httpserver/core/buffer_manager.hpp
// 描述: 缓冲区管理器外观接口 - 组合内存池、工厂、操作器
// ============================================================================

#pragma once

#include "memory_pool.hpp"
#include "buffer_factory.hpp"
#include "buffer_operator.hpp"
#include <memory>
#include <vector>

namespace httpserver::core {

/**
 * @brief 外观接口，提供统一入口，向后兼容原有 IBufferManager 接口
 */
class IBufferManager {
public:
    virtual ~IBufferManager() = default;

    // 获取子组件
    virtual std::shared_ptr<IMemoryPool> GetMemoryPool() = 0;
    virtual std::shared_ptr<IBufferFactory> GetBufferFactory() = 0;
    virtual std::shared_ptr<IBufferOperator> GetBufferOperator() = 0;

    // ----- 内存池管理（委托） -----
    void SetMaxPoolSize(size_t max_size);
    void SetPreAllocation(bool enable);
    void ClearPool() noexcept;
    size_t GetPoolSize() const noexcept;
    size_t GetTotalAllocated() const noexcept;
    IMemoryPool::MemoryStats GetStats() const;

    // ----- 缓冲区创建（委托） -----
    std::unique_ptr<Buffer> Allocate(size_t size);
    std::unique_ptr<Buffer> AllocateAligned(size_t size, size_t alignment);
    std::unique_ptr<Buffer> CloneBuffer(const BufferView& view);
    void ReturnBuffer(std::unique_ptr<Buffer> buffer);

    // ----- 缓冲区操作（委托） -----
    bool AppendBuffer(Buffer& dest, const BufferView& src);
    bool PrependBuffer(Buffer& dest, const BufferView& src);
    std::unique_ptr<Buffer> MergeBuffers(const std::vector<BufferView>& buffers);
    std::unique_ptr<Buffer> SplitBuffer(const Buffer& src, size_t pos);

    // 工厂方法
    static std::shared_ptr<IBufferManager> CreateDefault();
};

} // namespace httpserver::core