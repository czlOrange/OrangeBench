// ============================================================================
// 文件: include/httpserver/core/memory_pool.hpp
// 描述: 内存池接口 - 只负责原始内存管理
// ============================================================================

#pragma once

#include <cstddef>
#include <memory>

namespace httpserver::core {

/**
 * @brief 内存池抽象接口
 */
class IMemoryPool {
public:
    virtual ~IMemoryPool() = default;

    // 配置
    virtual void SetMaxPoolSize(size_t max_size) = 0;
    virtual void SetPreAllocation(bool enable) = 0;

    // 管理
    virtual void ClearPool() noexcept = 0;
    virtual size_t GetPoolSize() const noexcept = 0;
    virtual size_t GetTotalAllocated() const noexcept = 0;

    // 原始内存分配/释放
    virtual void* AllocateBytes(size_t size) = 0;
    virtual void* AllocateBytesAligned(size_t size, size_t alignment) = 0;
    virtual void DeallocateBytes(void* ptr) noexcept = 0;

    // 统计
    struct MemoryStats {
        size_t allocated_bytes;     // 当前已分配总字节
        size_t used_bytes;          // 实际使用字节
        size_t pool_bytes;          // 池大小
        size_t allocation_count;    // 分配次数
        size_t deallocation_count;  // 释放次数
    };
    virtual MemoryStats GetStats() const = 0;

    // 工厂方法
    static std::shared_ptr<IMemoryPool> CreateDefault();
};

} // namespace httpserver::core