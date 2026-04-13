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
    virtual void SetMaxPoolSize(size_t max_size) = 0;
    virtual void SetPreAllocation(bool enable) = 0;
    virtual void ClearPool() noexcept = 0;
    virtual size_t GetPoolSize() const noexcept = 0;
    virtual size_t GetTotalAllocated() const noexcept = 0;
    virtual IMemoryPool::MemoryStats GetStats() const = 0;

    // ----- 缓冲区创建（委托） -----
    virtual std::unique_ptr<Buffer> Allocate(size_t size) = 0;
    virtual std::unique_ptr<Buffer> AllocateAligned(size_t size, size_t alignment) = 0;
    virtual std::unique_ptr<Buffer> CloneBuffer(const BufferView& view) = 0;
    virtual void ReturnBuffer(std::unique_ptr<Buffer> buffer) = 0;

    // ----- 缓冲区操作（委托） -----
    virtual bool AppendBuffer(Buffer& dest, const BufferView& src) = 0;
    virtual bool PrependBuffer(Buffer& dest, const BufferView& src) = 0;
    virtual std::unique_ptr<Buffer> MergeBuffers(const std::vector<BufferView>& buffers) = 0;
    virtual std::unique_ptr<Buffer> SplitBuffer(const Buffer& src, size_t pos) = 0;

    // 工厂方法
    static std::shared_ptr<IBufferManager> CreateDefault();
};

} // namespace httpserver::core