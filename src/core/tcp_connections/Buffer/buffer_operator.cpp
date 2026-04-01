// src/core/tcp_connections/Buffer/buffer_operator.cpp
#include "../../../../include/httpserver/core/tcp_connections/Buffer/buffer_operator.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace httpserver::core {

class DefaultBufferOperator : public IBufferOperator {
public:
    bool Append(Buffer& dest, const BufferView& src) override {
        if (src.size == 0) return true;
        if (dest.capacity() - dest.size() < src.size) return false;
        std::memcpy(dest.data() + dest.size(), src.data, src.size);
        dest.resize(dest.size() + src.size);
        return true;
    }

    bool Prepend(Buffer& dest, const BufferView& src) override {
        if (src.size == 0) return true;
        if (dest.capacity() - dest.size() < src.size) return false;
        // 移动现有数据向后
        std::memmove(dest.data() + src.size, dest.data(), dest.size());
        // 复制新数据到开头
        std::memcpy(dest.data(), src.data, src.size);
        dest.resize(dest.size() + src.size);
        return true;
    }

    std::unique_ptr<Buffer> Merge(
        const std::vector<BufferView>& buffers,
        std::shared_ptr<IBufferFactory> factory) override {
        if (!factory) factory = IBufferFactory::CreateDefault();
        size_t total = 0;
        for (const auto& view : buffers) total += view.size;
        auto buf = factory->CreateBuffer(total);
        if (!buf) return nullptr;
        char* ptr = buf->data();
        for (const auto& view : buffers) {
            std::memcpy(ptr, view.data, view.size);
            ptr += view.size;
        }
        buf->resize(total);
        return buf;
    }

    std::unique_ptr<Buffer> Split(
        const Buffer& src, size_t pos,
        std::shared_ptr<IBufferFactory> factory) override {
        if (pos > src.size()) return nullptr;
        if (!factory) factory = IBufferFactory::CreateDefault();
        size_t new_size = src.size() - pos;
        auto new_buf = factory->CreateBuffer(new_size);
        if (!new_buf) return nullptr;
        std::memcpy(new_buf->data(), src.data() + pos, new_size);
        new_buf->resize(new_size);
        // 原缓冲区大小需要修改，但 src 是 const，所以不能改。但 Split 定义中应该允许修改源缓冲区。
        // 根据接口定义，src 是 const Buffer&，无法修改其大小。但 Split 通常需要修改源缓冲区。
        // 这里按照原设计返回新缓冲区，不修改原缓冲区（因为 const）。
        // 如果希望修改，应该传非 const 引用，但接口已定，我们遵循当前设计。
        return new_buf;
    }

    bool Copy(Buffer& dest, const BufferView& src, size_t dest_offset) override {
        if (src.size == 0) return true;
        if (dest_offset + src.size > dest.capacity()) return false;
        std::memcpy(dest.data() + dest_offset, src.data, src.size);
        // 如果复制的区域超出了当前有效数据范围，需要更新 dest 的 size
        if (dest_offset + src.size > dest.size()) {
            dest.resize(dest_offset + src.size);
        }
        return true;
    }

    bool Fill(Buffer& dest, uint8_t value) override {
        if (dest.capacity() == 0) return true;
        std::memset(dest.data(), value, dest.capacity());
        // 注意：有效数据大小保持不变
        return true;
    }

    bool Resize(Buffer& buffer, size_t new_size,
                std::shared_ptr<IBufferFactory> factory) override {
        if (new_size <= buffer.capacity()) {
            buffer.resize(new_size);
            return true;
        }
        // 需要扩容
        if (!factory) factory = IBufferFactory::CreateDefault();
        auto new_buf = factory->CreateBuffer(new_size);
        if (!new_buf) return false;
        // 拷贝原有数据
        std::memcpy(new_buf->data(), buffer.data(), buffer.size());
        new_buf->resize(buffer.size());
        // 通过移动赋值交换资源
        buffer = std::move(*new_buf);
        return true;
    }
};

std::shared_ptr<IBufferOperator> IBufferOperator::CreateDefault() {
    return std::make_shared<DefaultBufferOperator>();
}

} // namespace httpserver::core