// ============================================================================
// 文件: include/httpserver/core/buffer_factory.hpp
// 描述: 缓冲区工厂接口 - 负责创建 Buffer 对象
// ============================================================================

#pragma once

#include "buffer.hpp"
#include "buffer_view.hpp"
#include "memory_pool.hpp"
#include <memory>

namespace httpserver::core {

/**
 * @brief 缓冲区工厂接口，使用内存池创建 Buffer 对象
 */
class IBufferFactory {
public:
    virtual ~IBufferFactory() = default;

    // 创建缓冲区
    virtual std::unique_ptr<Buffer> CreateBuffer(size_t size) = 0;
    virtual std::unique_ptr<Buffer> CreateBufferAligned(size_t size, size_t alignment) = 0;

    // 克隆视图数据到新缓冲区
    virtual std::unique_ptr<Buffer> CloneBuffer(const BufferView& view) = 0;

    // 回收缓冲区（实际上 unique_ptr 析构会自动调用删除器，此方法可用于统计或缓存）
    virtual void RecycleBuffer(std::unique_ptr<Buffer> buffer) = 0;

    // 工厂方法
    static std::shared_ptr<IBufferFactory> CreateDefault();
};

} // namespace httpserver::core