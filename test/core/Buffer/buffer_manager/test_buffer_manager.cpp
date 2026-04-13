// test/core/buffer_manager_test.cpp
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer_manager.hpp"
#include <gtest/gtest.h>
#include <cstring>

using namespace httpserver::core;

class BufferManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = IBufferManager::CreateDefault();
        ASSERT_NE(manager_, nullptr);
    }

    std::shared_ptr<IBufferManager> manager_;
};

TEST_F(BufferManagerTest, GetComponents) {
    auto pool = manager_->GetMemoryPool();
    auto factory = manager_->GetBufferFactory();
    auto op = manager_->GetBufferOperator();
    EXPECT_NE(pool, nullptr);
    EXPECT_NE(factory, nullptr);
    EXPECT_NE(op, nullptr);
}

TEST_F(BufferManagerTest, AllocateAndReturn) {
    auto buf = manager_->Allocate(100);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(buf->capacity(), 100);
    EXPECT_EQ(buf->size(), 0);
    manager_->ReturnBuffer(std::move(buf));
    // 重新分配可能成功
    auto buf2 = manager_->Allocate(100);
    EXPECT_NE(buf2, nullptr);
}

TEST_F(BufferManagerTest, AllocateAligned) {
    auto buf = manager_->AllocateAligned(100, 64);
    ASSERT_NE(buf, nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(buf->data());
    EXPECT_EQ(addr % 64, 0);
}

TEST_F(BufferManagerTest, CloneBuffer) {
    const char* text = "Hello, World!";
    BufferView view(text, strlen(text));
    auto cloned = manager_->CloneBuffer(view);
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->size(), view.size);
    EXPECT_STREQ(cloned->data(), text);
}

TEST_F(BufferManagerTest, AppendBuffer) {
    auto buf = manager_->Allocate(100);
    ASSERT_NE(buf, nullptr);
    const char* data = "Hello";
    BufferView view(data, 5);
    EXPECT_TRUE(manager_->AppendBuffer(*buf, view));
    EXPECT_EQ(buf->size(), 5);
    EXPECT_STREQ(buf->data(), "Hello");
}

TEST_F(BufferManagerTest, PrependBuffer) {
    auto buf = manager_->Allocate(100);
    ASSERT_NE(buf, nullptr);
    const char* data = "World";
    BufferView view(data, 5);
    EXPECT_TRUE(manager_->PrependBuffer(*buf, view));
    EXPECT_EQ(buf->size(), 5);
    EXPECT_STREQ(buf->data(), "World");
    const char* more = "Hello ";
    BufferView view2(more, 6);
    EXPECT_TRUE(manager_->PrependBuffer(*buf, view2));
    EXPECT_EQ(buf->size(), 11);
    EXPECT_STREQ(buf->data(), "Hello World");
}

TEST_F(BufferManagerTest, MergeBuffers) {
    std::vector<BufferView> views;
    const char* a = "Hello";
    const char* b = " ";
    const char* c = "World";
    views.emplace_back(a, 5);
    views.emplace_back(b, 1);
    views.emplace_back(c, 5);
    auto merged = manager_->MergeBuffers(views);
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->size(), 11);
    EXPECT_STREQ(merged->data(), "Hello World");
}

TEST_F(BufferManagerTest, SplitBuffer) {
    auto buf = manager_->Allocate(100);
    ASSERT_NE(buf, nullptr);
    const char* data = "HelloWorld";
    BufferView view(data, 10);
    manager_->AppendBuffer(*buf, view);
    auto split = manager_->SplitBuffer(*buf, 5);
    ASSERT_NE(split, nullptr);
    EXPECT_EQ(split->size(), 5);
    EXPECT_STREQ(split->data(), "World");
    // 原缓冲区不变（因为 const）
    EXPECT_EQ(buf->size(), 10);
    EXPECT_STREQ(buf->data(), "HelloWorld");
}

TEST_F(BufferManagerTest, PoolManagement) {
    manager_->SetMaxPoolSize(1024);
    manager_->SetPreAllocation(true);
    EXPECT_GT(manager_->GetPoolSize(), 0);
    manager_->SetPreAllocation(false);
    // 池大小可能变为0
    manager_->ClearPool();
    EXPECT_EQ(manager_->GetPoolSize(), 0);
}

TEST_F(BufferManagerTest, Stats) {
    auto stats = manager_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 0);
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.pool_bytes, 0);
    EXPECT_EQ(stats.allocation_count, 0);
    EXPECT_EQ(stats.deallocation_count, 0);
    auto buf = manager_->Allocate(50);
    stats = manager_->GetStats();
    EXPECT_EQ(stats.allocated_bytes, 50);
    EXPECT_EQ(stats.used_bytes, 50);
    EXPECT_GT(stats.pool_bytes, 0);
    EXPECT_EQ(stats.allocation_count, 1);
    manager_->ReturnBuffer(std::move(buf));
    stats = manager_->GetStats();
    EXPECT_EQ(stats.used_bytes, 0);
    EXPECT_EQ(stats.deallocation_count, 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}