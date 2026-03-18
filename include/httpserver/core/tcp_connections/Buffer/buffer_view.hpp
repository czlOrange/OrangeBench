// ============================================================================
// 文件: include/httpserver/core/buffer_view.hpp
// 描述: 缓冲区视图定义（只读视图和可写视图）
// ============================================================================

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace httpserver::core {

// 前向声明
class Buffer;

/**`
 * @brief 只读缓冲区视图，不拥有内存，轻量级
 */
struct BufferView {
    const char* data;   // 数据指针
    size_t size;        // 数据长度

    BufferView();
    BufferView(const char* d, size_t s);
    explicit BufferView(const std::string& str);
    explicit BufferView(const Buffer& buf);  // 从 Buffer 构造只读视图

    std::string_view ToStringView() const;
};

/**
 * @brief 可写缓冲区视图，用于需要直接写入数据的场景
 */
struct MutableBufferView {
    char* data;         // 数据指针（可写）
    size_t size;        // 缓冲区可用大小（通常为 capacity）

    MutableBufferView();
    MutableBufferView(char* d, size_t s);
    explicit MutableBufferView(Buffer& buf);  // 从 Buffer 构造可写视图

    std::string_view ToStringView() const;    // 转换为只读视图
};

} // namespace httpserver::core