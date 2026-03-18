// ============================================================================
// 文件: src/httpserver/core/memory_pool.cpp
// 描述: 内存池实现 - 基于块分配的内存池
// ============================================================================

#include "httpserver/core/tcp_connections/Buffer/memory_pool.hpp"
#include <vector>
#include <mutex>
#include <unordered_map>
#include <cassert>

namespace httpserver::core {

// 内存块头信息
struct MemoryBlockHeader {
    size_t size;           // 实际分配大小
    bool in_use;           // 是否在使用中
    MemoryBlockHeader* next; // 链表下一节点
};

// 内存池实现类
class DefaultMemoryPool : public IMemoryPool {
private:
    // 内存池配置
    size_t max_pool_size_{1024 * 1024 * 100}; // 默认100MB
    bool pre_allocation_{false};
    
    // 内存块管理
    std::mutex mutex_;
    std::unordered_map<void*, MemoryBlockHeader*> allocated_blocks_; // 已分配块
    std::vector<MemoryBlockHeader*> free_lists_; // 按大小分级的空闲链表
    
    // 统计信息
    size_t total_allocated_bytes_{0};
    size_t current_used_bytes_{0};
    size_t current_pool_bytes_{0};
    size_t allocation_count_{0};
    size_t deallocation_count_{0};
    
    // 预分配内存块
    std::vector<char*> pre_allocated_memory_;
    
    // 辅助函数：根据大小选择合适的空闲链表索引
    size_t GetFreeListIndex(size_t size) {
        if (size <= 64) return 0;
        if (size <= 256) return 1;
        if (size <= 1024) return 2;
        if (size <= 4096) return 3;
        if (size <= 16384) return 4;
        return 5; // 大块内存
    }
    
    // 辅助函数：对齐大小
    size_t AlignSize(size_t size, size_t alignment = alignof(std::max_align_t)) {
        return (size + alignment - 1) & ~(alignment - 1);
    }

public:
    DefaultMemoryPool() {
        // 初始化空闲链表
        free_lists_.resize(6); // 6个大小级别
    }
    
    ~DefaultMemoryPool() override {
        ClearPool();
        // 释放预分配内存
        for (auto* mem : pre_allocated_memory_) {
            ::operator delete(mem);
        }
    }
    
    // 配置
    void SetMaxPoolSize(size_t max_size) override {
        std::lock_guard<std::mutex> lock(mutex_);
        max_pool_size_ = max_size;
    }
    
    void SetPreAllocation(bool enable) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enable == pre_allocation_) return;
        
        pre_allocation_ = enable;
        if (enable) {
            // 预分配一些内存
            PreAllocateMemory();
        } else {
            // 释放预分配内存
            for (auto* mem : pre_allocated_memory_) {
                ::operator delete(mem);
            }
            pre_allocated_memory_.clear();
        }
    }
    
    // 预分配内存
    void PreAllocateMemory() {
        // 预分配几个常用大小的内存块
        const size_t sizes[] = {64, 256, 1024, 4096};
        for (size_t size : sizes) {
            for (int i = 0; i < 10; ++i) { // 每种大小预分配10个
                void* ptr = ::operator new(size + sizeof(MemoryBlockHeader));
                auto* header = static_cast<MemoryBlockHeader*>(ptr);
                header->size = size;
                header->in_use = false;
                header->next = nullptr;
                
                // 加入空闲链表
                size_t index = GetFreeListIndex(size);
                header->next = free_lists_[index];
                free_lists_[index] = header;
                
                pre_allocated_memory_.push_back(static_cast<char*>(ptr));
                current_pool_bytes_ += (size + sizeof(MemoryBlockHeader));
            }
        }
    }
    
    // 管理
    void ClearPool() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 释放所有已分配块
        for (auto& pair : allocated_blocks_) {
            if (pair.second) {
                // 标记为未使用但不释放内存
                pair.second->in_use = false;
            }
        }
        allocated_blocks_.clear();
        
        // 重置统计
        current_used_bytes_ = 0;
        allocation_count_ = 0;
        deallocation_count_ = 0;
        
        // 重新组织空闲链表
        for (auto& list : free_lists_) {
            auto* current = list;
            while (current) {
                current->in_use = false;
                current = current->next;
            }
        }
    }
    
    size_t GetPoolSize() const noexcept override {
        return current_pool_bytes_;
    }
    
    size_t GetTotalAllocated() const noexcept override {
        return total_allocated_bytes_;
    }
    
    // 原始内存分配
    void* AllocateBytes(size_t size) override {
        return AllocateBytesAligned(size, alignof(std::max_align_t));
    }
    
    void* AllocateBytesAligned(size_t size, size_t alignment) override {
        if (size == 0) return nullptr;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查池大小限制
        if (current_pool_bytes_ + size + sizeof(MemoryBlockHeader) > max_pool_size_) {
            return nullptr; // 超过最大限制
        }
        
        // 对齐大小
        size_t aligned_size = AlignSize(size, alignment);
        size_t total_size = aligned_size + sizeof(MemoryBlockHeader);
        
        MemoryBlockHeader* header = nullptr;
        
        // 先从空闲链表中查找
        size_t index = GetFreeListIndex(aligned_size);
        MemoryBlockHeader** prev_next = &free_lists_[index];
        MemoryBlockHeader* current = free_lists_[index];
        
        while (current) {
            if (current->size >= aligned_size && !current->in_use) {
                // 找到合适的空闲块
                header = current;
                // 从空闲链表中移除
                *prev_next = current->next;
                break;
            }
            prev_next = &current->next;
            current = current->next;
        }
        
        if (!header) {
            // 没有合适的空闲块，分配新内存
            header = static_cast<MemoryBlockHeader*>(::operator new(total_size));
            header->size = aligned_size;
            current_pool_bytes_ += total_size;
        }
        
        // 初始化头信息
        header->in_use = true;
        header->next = nullptr;
        
        // 记录分配
        void* user_ptr = reinterpret_cast<char*>(header) + sizeof(MemoryBlockHeader);
        allocated_blocks_[user_ptr] = header;
        
        // 更新统计
        total_allocated_bytes_ += aligned_size;
        current_used_bytes_ += aligned_size;
        allocation_count_++;
        
        return user_ptr;
    }
    
    void DeallocateBytes(void* ptr) noexcept override {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = allocated_blocks_.find(ptr);
        if (it == allocated_blocks_.end()) {
            // 指针不属于这个池，可能是非法指针
            assert(false && "Deallocating pointer not from this pool");
            return;
        }
        
        auto* header = it->second;
        header->in_use = false;
        
        // 更新统计
        current_used_bytes_ -= header->size;
        deallocation_count_++;
        
        // 从分配映射中移除
        allocated_blocks_.erase(it);
        
        // 如果池大小超过阈值，可以释放内存
        if (current_pool_bytes_ > max_pool_size_ * 1.2) {
            // 可以选择释放空闲内存
            ::operator delete(header);
            current_pool_bytes_ -= (header->size + sizeof(MemoryBlockHeader));
        } else {
            // 否则放回空闲链表
            size_t index = GetFreeListIndex(header->size);
            header->next = free_lists_[index];
            free_lists_[index] = header;
        }
    }
    
    // 统计
    MemoryStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return MemoryStats{
            .allocated_bytes = total_allocated_bytes_,
            .used_bytes = current_used_bytes_,
            .pool_bytes = current_pool_bytes_,
            .allocation_count = allocation_count_,
            .deallocation_count = deallocation_count_
        };
    }
};

// 工厂方法实现
std::shared_ptr<IMemoryPool> IMemoryPool::CreateDefault() {
    return std::make_shared<DefaultMemoryPool>();
}

} // namespace httpserver::core