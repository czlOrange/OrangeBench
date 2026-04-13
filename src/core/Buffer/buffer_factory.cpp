// src/core/buffer_factory.cpp
#include "httpserver/core/Buffer/buffer_factory.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>

namespace httpserver::core {

class DefaultBufferFactory : public IBufferFactory {
public:
    explicit DefaultBufferFactory(std::shared_ptr<IMemoryPool> pool = IMemoryPool::CreateDefault())
        : pool_(std::move(pool)) {
        if (!pool_) {
            throw std::runtime_error("Failed to create memory pool");
        }
    }

    std::unique_ptr<Buffer> CreateBuffer(size_t size) override {
        return CreateBufferAligned(size, alignof(std::max_align_t));
    }

    std::unique_ptr<Buffer> CreateBufferAligned(size_t size, size_t alignment) override {
        if (size == 0) {
            return nullptr;
        }
        void* mem = pool_->AllocateBytesAligned(size, alignment);
        if (!mem) {
            return nullptr;
        }
        auto deleter = [this](void* p) {
            if (p) pool_->DeallocateBytes(p);
        };
        return std::make_unique<Buffer>(mem, size, deleter);
    }

    std::unique_ptr<Buffer> CloneBuffer(const BufferView& view) override {
        if (view.size == 0) {
            return nullptr;
        }
        auto buf = CreateBuffer(view.size);
        if (!buf) {
            return nullptr;
        }
        std::memcpy(buf->data(), view.data, view.size);
        buf->resize(view.size);
        return buf;
    }

    void RecycleBuffer(std::unique_ptr<Buffer> buffer) override {
        // 直接释放 unique_ptr，Buffer 析构时会通过删除器归还内存
        buffer.reset();
    }

private:
    std::shared_ptr<IMemoryPool> pool_;
};

std::shared_ptr<IBufferFactory> IBufferFactory::CreateDefault() {
    return std::make_shared<DefaultBufferFactory>();
}

} // namespace httpserver::core