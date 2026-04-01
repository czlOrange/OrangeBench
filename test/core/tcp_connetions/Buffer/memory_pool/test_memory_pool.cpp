// test/core/memory_pool_test.cpp
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/memory_pool.hpp"

#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>

using namespace httpserver::core;

class MemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = IMemoryPool::CreateDefault();
        ASSERT_NE(pool_, nullptr);
    }

    void TearDown() override {
        // 确保所有分配都已释放（避免影响后续测试）
        // 池析构时会清理空闲块，但使用中的块不会自动释放，因此测试中必须自行释放。
        // 这里不做自动清理，由各测试用例负责释放。
    }

    std::shared_ptr<IMemoryPool> pool_;
};

// 基本分配和释放
TEST_F(MemoryPoolTest, AllocateAndDeallocate) {
    void* p = pool_->AllocateBytes(100);
    ASSERT_NE(p, nullptr);
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 100);
    EXPECT_EQ(stats.used_bytes, 100);
    EXPECT_EQ(stats.allocation_count, 1);
    EXPECT_EQ(stats.deallocation_count, 0);

    pool_->DeallocateBytes(p);
    stats = pool_->GetStats();
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.deallocation_count, 1);
    // 分配的字节总数仍为100，因为池记录了历史分配
    EXPECT_EQ(stats.allocated_bytes, 100);
}

// 分配零字节应返回nullptr
TEST_F(MemoryPoolTest, AllocateZero) {
    void* p = pool_->AllocateBytes(0);
    EXPECT_EQ(p, nullptr);
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 0);
    EXPECT_EQ(stats.allocation_count, 0);
}

// 释放空指针应无影响
TEST_F(MemoryPoolTest, DeallocateNull) {
    pool_->DeallocateBytes(nullptr);
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 0);
    EXPECT_EQ(stats.deallocation_count, 0);
}

// 多次分配释放
TEST_F(MemoryPoolTest, MultipleAllocations) {
    std::vector<void*> ptrs;
    for (int i = 0; i < 10; ++i) {
        void* p = pool_->AllocateBytes(64);
        ASSERT_NE(p, nullptr);
        ptrs.push_back(p);
    }
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 10 * 64);
    EXPECT_EQ(stats.used_bytes, 10 * 64);
    EXPECT_EQ(stats.allocation_count, 10);

    for (void* p : ptrs) {
        pool_->DeallocateBytes(p);
    }
    stats = pool_->GetStats();
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.deallocation_count, 10);
}

// 对齐分配测试
TEST_F(MemoryPoolTest, AlignedAllocation) {
    size_t alignments[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    for (size_t align : alignments) {
        void* p = pool_->AllocateBytesAligned(100, align);
        ASSERT_NE(p, nullptr);
        uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        EXPECT_EQ(addr % align, 0);
        pool_->DeallocateBytes(p);
    }
}

// 超出池大小限制应返回nullptr
TEST_F(MemoryPoolTest, ExceedPoolSize) {
    pool_->SetMaxPoolSize(1024); // 限制总池大小（包括头开销）
    // 分配 2000 字节（实际需要更多），应失败
    void* p = pool_->AllocateBytes(2000);
    EXPECT_EQ(p, nullptr);
    // 分配较小内存应成功
    p = pool_->AllocateBytes(512);
    EXPECT_NE(p, nullptr);
    pool_->DeallocateBytes(p);
    // 恢复默认限制（避免影响其他测试）
    pool_->SetMaxPoolSize(1024 * 1024 * 100);
}

// 预分配功能测试
TEST_F(MemoryPoolTest, PreAllocation) {
    // 获取初始池大小
    size_t initial = pool_->GetPoolSize();
    // 启用预分配
    pool_->SetPreAllocation(true);
    size_t after_pre = pool_->GetPoolSize();
    EXPECT_GT(after_pre, initial); // 池大小应增加

    // 分配内存，应使用预分配块（但实现中当前并未重用空闲块，只是简单分配新块）
    // 因此此处只验证预分配后池大小变化，不深入验证重用。
    // 禁用预分配，池大小应恢复到初始（但注意：空闲块被释放）
    pool_->SetPreAllocation(false);
    size_t after_disable = pool_->GetPoolSize();
    EXPECT_EQ(after_disable, initial);
}

// 清空池（只清空间闲块，使用中的块不受影响）
TEST_F(MemoryPoolTest, ClearPool) {
    // 分配一些内存
    void* p1 = pool_->AllocateBytes(100);
    void* p2 = pool_->AllocateBytes(200);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    size_t before_clear = pool_->GetPoolSize();

    // 清空池（应只释放空闲块，使用中的块不会释放）
    pool_->ClearPool();
    size_t after_clear = pool_->GetPoolSize();
    // 因为分配的两个块仍在用，所以池大小不变（它们占用了内存，但不在空闲链表中）
    EXPECT_EQ(after_clear, before_clear);

    // 释放使用中的块，它们会被放回空闲链表
    pool_->DeallocateBytes(p1);
    pool_->DeallocateBytes(p2);
    // 再次清空池，此时空闲块应被释放，池大小应变为0（因为所有内存都已空闲）
    pool_->ClearPool();
    size_t final = pool_->GetPoolSize();
    EXPECT_EQ(final, 0);
}

// 统计信息正确性
TEST_F(MemoryPoolTest, Statistics) {
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 0);
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.pool_bytes, 0);
    EXPECT_EQ(stats.allocation_count, 0);
    EXPECT_EQ(stats.deallocation_count, 0);

    void* p1 = pool_->AllocateBytes(50);
    void* p2 = pool_->AllocateBytes(150);
    stats = pool_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 200);
    EXPECT_EQ(stats.used_bytes, 200);
    EXPECT_GT(stats.pool_bytes, 0); // 池大小至少包含用户数据+头
    EXPECT_EQ(stats.allocation_count, 2);
    EXPECT_EQ(stats.deallocation_count, 0);

    pool_->DeallocateBytes(p1);
    stats = pool_->GetStats();
    EXPECT_EQ(stats.used_bytes, 150);
    EXPECT_EQ(stats.deallocation_count, 1);

    pool_->DeallocateBytes(p2);
    stats = pool_->GetStats();
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.deallocation_count, 2);
}

// 并发分配释放测试（简单验证无竞争）
TEST_F(MemoryPoolTest, ConcurrentAllocations) {
    const int thread_count = 4;
    const int allocs_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<bool> failed{false};

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            std::vector<void*> ptrs;
            for (int j = 0; j < allocs_per_thread; ++j) {
                void* p = pool_->AllocateBytes(64);
                if (!p) {
                    failed = true;
                    return;
                }
                ptrs.push_back(p);
            }
            for (void* p : ptrs) {
                pool_->DeallocateBytes(p);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_FALSE(failed);
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.allocation_count, thread_count * allocs_per_thread);
    EXPECT_EQ(stats.deallocation_count, thread_count * allocs_per_thread);
}

// 重复分配相同大小，验证内存是否被重用（简单验证）
TEST_F(MemoryPoolTest, ReuseMemory) {
    void* p1 = pool_->AllocateBytes(64);
    ASSERT_NE(p1, nullptr);
    pool_->DeallocateBytes(p1);

    void* p2 = pool_->AllocateBytes(64);
    ASSERT_NE(p2, nullptr);
    // 由于空闲块可能被重用，p2 可能等于 p1，也可能不等（取决于实现）
    // 这里只验证分配成功，不要求相等。
    pool_->DeallocateBytes(p2);
}

// 测试不同大小的混合分配释放
TEST_F(MemoryPoolTest, MixedSizes) {
    std::vector<void*> ptrs;
    for (int i = 1; i <= 10; ++i) {
        void* p = pool_->AllocateBytes(i * 10);
        ASSERT_NE(p, nullptr);
        ptrs.push_back(p);
    }
    for (void* p : ptrs) {
        pool_->DeallocateBytes(p);
    }
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.allocation_count, 10);
    EXPECT_EQ(stats.deallocation_count, 10);
}

// 主函数
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}