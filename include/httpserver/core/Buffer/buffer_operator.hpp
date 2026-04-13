// ============================================================================
// 文件: include/httpserver/core/buffer_operator.hpp
// 描述: 缓冲区操作接口 - 对 Buffer 数据进行操作
// ============================================================================

#pragma once

#include "buffer.hpp"
#include "buffer_view.hpp"
#include "buffer_factory.hpp"
#include <vector>
#include <memory>

namespace httpserver::core {

/**
 * @brief 缓冲区操作接口，提供追加、合并、分割等功能
 */
class IBufferOperator {
public:
    virtual ~IBufferOperator() = default;

    // 追加到末尾（要求目标有足够剩余空间）
    virtual bool Append(Buffer& dest, const BufferView& src) = 0;

    // 插入到开头（要求目标有足够剩余空间）
    virtual bool Prepend(Buffer& dest, const BufferView& src) = 0;

    // 合并多个视图为一个新缓冲区
    virtual std::unique_ptr<Buffer> Merge(
        const std::vector<BufferView>& buffers,
        std::shared_ptr<IBufferFactory> factory) = 0;

    // 从指定位置分割，返回后半部分新缓冲区
    virtual std::unique_ptr<Buffer> Split(
        const Buffer& src, size_t pos,
        std::shared_ptr<IBufferFactory> factory) = 0;

    // 拷贝数据到目标缓冲区的指定偏移（不自动扩容）
    virtual bool Copy(Buffer& dest, const BufferView& src, size_t dest_offset = 0) = 0;

    // 用指定值填充整个缓冲区（包括未使用的容量）
    virtual bool Fill(Buffer& dest, uint8_t value) = 0;

    // 调整缓冲区大小（可能扩容）
    virtual bool Resize(Buffer& buffer, size_t new_size,
                        std::shared_ptr<IBufferFactory> factory) = 0;

    // 工厂方法
    static std::shared_ptr<IBufferOperator> CreateDefault();
};

} // namespace httpserver::core