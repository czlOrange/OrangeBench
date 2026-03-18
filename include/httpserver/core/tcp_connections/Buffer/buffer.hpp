// ============================================================================
// 文件: include/httpserver/core/buffer.hpp
// 描述: Buffer 类定义 - 拥有内存的RAII缓冲区，只能移动
// ============================================================================

#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace httpserver::core {

/**
 * @brief 缓冲区类，持有从内存池分配的内存块，析构时自动归还
 * 
 * 特点：
 * - 只能移动，不能拷贝，保证内存唯一所有权
 * - 构造时传入删除器，析构时自动调用归还内存
 * - 提供 data(), size(), capacity() 等访问接口
 */
class Buffer {
public:
    /**
     * @brief 构造函数
     * @param data 内存地址（来自内存池）
     * @param size 内存块大小（容量）
     * @param deleter 删除器，用于释放内存
     */
    Buffer(void* data, size_t size, std::function<void(void*)> deleter);

    ~Buffer();

    // 禁止拷贝
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // 允许移动
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    // 访问接口
    char* data();
    const char* data() const;
    size_t size() const;      // 当前有效数据大小
    size_t capacity() const;   // 缓冲区总容量

    // 调整有效数据大小（不能超过容量）
    void resize(size_t new_size);

private:
    void* data_;                       // 内存块地址
    size_t size_;                       // 当前已用大小
    size_t capacity_;                    // 总容量
    std::function<void(void*)> deleter_; // 删除器
};

} // namespace httpserver::core