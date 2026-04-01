// src/core/buffer_manager.cpp
#include "../../../../include/httpserver/core/tcp_connections/Buffer/buffer_manager.hpp"

#include <memory>

namespace httpserver::core {

class DefaultBufferManager : public IBufferManager {
public:
    DefaultBufferManager()
        : pool_(IMemoryPool::CreateDefault()),
          factory_(IBufferFactory::CreateDefault()),
          operator_(IBufferOperator::CreateDefault()) {
        // 注意：工厂和操作器使用各自独立的内存池，为了统一管理可以后续优化
    }

    std::shared_ptr<IMemoryPool> GetMemoryPool() override {
        return pool_;
    }

    std::shared_ptr<IBufferFactory> GetBufferFactory() override {
        return factory_;
    }

    std::shared_ptr<IBufferOperator> GetBufferOperator() override {
        return operator_;
    }

    void SetMaxPoolSize(size_t max_size) override {
        pool_->SetMaxPoolSize(max_size);
    }

    void SetPreAllocation(bool enable) override {
        pool_->SetPreAllocation(enable);
    }

    void ClearPool() noexcept override {
        pool_->ClearPool();
    }

    size_t GetPoolSize() const noexcept override {
        return pool_->GetPoolSize();
    }

    size_t GetTotalAllocated() const noexcept override {
        return pool_->GetTotalAllocated();
    }

    IMemoryPool::MemoryStats GetStats() const override {
        return pool_->GetStats();
    }

    std::unique_ptr<Buffer> Allocate(size_t size) override {
        return factory_->CreateBuffer(size);
    }

    std::unique_ptr<Buffer> AllocateAligned(size_t size, size_t alignment) override {
        return factory_->CreateBufferAligned(size, alignment);
    }

    std::unique_ptr<Buffer> CloneBuffer(const BufferView& view) override {
        return factory_->CloneBuffer(view);
    }

    void ReturnBuffer(std::unique_ptr<Buffer> buffer) override {
        factory_->RecycleBuffer(std::move(buffer));
    }

    bool AppendBuffer(Buffer& dest, const BufferView& src) override {
        return operator_->Append(dest, src);
    }

    bool PrependBuffer(Buffer& dest, const BufferView& src) override {
        return operator_->Prepend(dest, src);
    }

    std::unique_ptr<Buffer> MergeBuffers(const std::vector<BufferView>& buffers) override {
        return operator_->Merge(buffers, factory_);
    }

    std::unique_ptr<Buffer> SplitBuffer(const Buffer& src, size_t pos) override {
        return operator_->Split(src, pos, factory_);
    }

private:
    std::shared_ptr<IMemoryPool> pool_;
    std::shared_ptr<IBufferFactory> factory_;
    std::shared_ptr<IBufferOperator> operator_;
};

std::shared_ptr<IBufferManager> IBufferManager::CreateDefault() {
    return std::make_shared<DefaultBufferManager>();
}

} // namespace httpserver::core