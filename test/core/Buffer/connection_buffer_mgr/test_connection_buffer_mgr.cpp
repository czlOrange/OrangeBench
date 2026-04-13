// test/core/connection_buffer_mgr_test.cpp
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/connection_buffer_mgr.hpp"
#include <gtest/gtest.h>
#include <cstring>

using namespace httpserver::core;

class ConnectionBufferManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_shared<ConnectionBufferManager>(
            IBufferFactory::CreateDefault(),
            IBufferOperator::CreateDefault());
    }

    std::shared_ptr<ConnectionBufferManager> manager_;
};

TEST_F(ConnectionBufferManagerTest, SendQueue) {
    EXPECT_TRUE(manager_->IsSendQueueEmpty());
    EXPECT_EQ(manager_->GetSendQueueSize(), 0);

    const char* data1 = "Hello";
    BufferView view1(data1, 5);
    EXPECT_TRUE(manager_->EnqueueSend(view1));
    EXPECT_FALSE(manager_->IsSendQueueEmpty());
    EXPECT_EQ(manager_->GetSendQueueSize(), 5);

    auto peek = manager_->PeekSend();
    ASSERT_EQ(peek.size, 5);
    EXPECT_STREQ(peek.data, "Hello");

    manager_->ConsumeSend(2);
    EXPECT_EQ(manager_->GetSendQueueSize(), 3);
    peek = manager_->PeekSend();
    ASSERT_EQ(peek.size, 3);
    EXPECT_EQ(std::string(peek.data, 3), "llo");

    manager_->ConsumeSend(3);
    EXPECT_TRUE(manager_->IsSendQueueEmpty());
    EXPECT_EQ(manager_->GetSendQueueSize(), 0);
}

TEST_F(ConnectionBufferManagerTest, SendQueueMultiple) {
    const char* data1 = "Hello";
    const char* data2 = " ";
    const char* data3 = "World";
    EXPECT_TRUE(manager_->EnqueueSend(BufferView(data1, 5)));
    EXPECT_TRUE(manager_->EnqueueSend(BufferView(data2, 1)));
    EXPECT_TRUE(manager_->EnqueueSend(BufferView(data3, 5)));
    EXPECT_EQ(manager_->GetSendQueueSize(), 11);

    auto peek = manager_->PeekSend();
    EXPECT_EQ(peek.size, 5);
    manager_->ConsumeSend(5);
    peek = manager_->PeekSend();
    EXPECT_EQ(peek.size, 1);
    manager_->ConsumeSend(1);
    peek = manager_->PeekSend();
    EXPECT_EQ(peek.size, 5);
    manager_->ConsumeSend(5);
    EXPECT_TRUE(manager_->IsSendQueueEmpty());
}

TEST_F(ConnectionBufferManagerTest, SendQueueLimit) {
    manager_->SetMaxSendQueueSize(10);
    const char* data = "HelloWorld";
    BufferView view(data, 10);
    EXPECT_TRUE(manager_->EnqueueSend(view));
    const char* extra = "Extra";
    EXPECT_FALSE(manager_->EnqueueSend(BufferView(extra, 5)));
    EXPECT_EQ(manager_->GetSendQueueSize(), 10);
    manager_->ConsumeSend(5);
    EXPECT_TRUE(manager_->EnqueueSend(BufferView(extra, 5)));
    EXPECT_EQ(manager_->GetSendQueueSize(), 10);
}

TEST_F(ConnectionBufferManagerTest, ReceiveBuffer) {
    // 准备接收
    EXPECT_TRUE(manager_->PrepareReceive(100));
    auto view = manager_->GetReceiveBuffer();
    EXPECT_GE(view.size, 100);
    // 模拟写入数据
    const char* data = "Hello";
    std::memcpy(view.data, data, 5);
    manager_->CommitReceive(5);
    EXPECT_EQ(manager_->GetReceiveBufferSize(), 5);
    // 现在 receive_buffer 内部应该已有数据
    // 读取数据（可以通过直接访问 buffer 的方式，但我们没有提供读取接口，可以通过工厂获取 buffer 的 data？）
    // 我们可以通过准备新的接收缓冲区来验证？或者通过 GetReceiveBuffer 的偏移
    // 这里简单验证 resize 后数据仍然存在
    auto recv_buf = manager_->GetReceiveBuffer(); // 这是可写视图，从已提交后开始
    EXPECT_EQ(recv_buf.size, view.size - 5);
    // 再次提交
    const char* more = " World";
    std::memcpy(recv_buf.data, more, 6);
    manager_->CommitReceive(6);
    EXPECT_EQ(manager_->GetReceiveBufferSize(), 11);
    // 为了验证数据，我们可以通过一个 Buffer 来获取整个接收缓冲区内容？目前没有接口。
    // 但我们可以通过提供的方法间接验证：再获取一次接收缓冲区，应该从 11 字节后开始
    auto next = manager_->GetReceiveBuffer();
    EXPECT_EQ(next.size, view.size - 11);
}

TEST_F(ConnectionBufferManagerTest, ReceiveBufferExpand) {
    // 创建一个小缓冲区
    EXPECT_TRUE(manager_->PrepareReceive(10));
    auto view = manager_->GetReceiveBuffer();
    EXPECT_GE(view.size, 10);
    // 写入数据，占用 9 字节
    std::memset(view.data, 'A', 9);
    manager_->CommitReceive(9);
    // 准备接收更多数据，超过剩余空间
    EXPECT_TRUE(manager_->PrepareReceive(10));
    auto new_view = manager_->GetReceiveBuffer();
    EXPECT_GE(new_view.size, 10);
    // 原数据应该已被保留
    EXPECT_EQ(manager_->GetReceiveBufferSize(), 9);
    // 写入新数据到新视图
    std::memset(new_view.data, 'B', 10);
    manager_->CommitReceive(10);
    EXPECT_EQ(manager_->GetReceiveBufferSize(), 19);
}

TEST_F(ConnectionBufferManagerTest, ClearAll) {
    manager_->EnqueueSend(BufferView("test", 4));
    manager_->PrepareReceive(100);
    manager_->CommitReceive(50);
    EXPECT_FALSE(manager_->IsSendQueueEmpty());
    EXPECT_GT(manager_->GetReceiveBufferSize(), 0);
    manager_->ClearAll();
    EXPECT_TRUE(manager_->IsSendQueueEmpty());
    EXPECT_EQ(manager_->GetReceiveBufferSize(), 0);
}

TEST_F(ConnectionBufferManagerTest, Swap) {
    auto mgr1 = std::make_shared<ConnectionBufferManager>(
        IBufferFactory::CreateDefault(),
        IBufferOperator::CreateDefault());
    auto mgr2 = std::make_shared<ConnectionBufferManager>(
        IBufferFactory::CreateDefault(),
        IBufferOperator::CreateDefault());
    mgr1->EnqueueSend(BufferView("one", 3));
    mgr2->EnqueueSend(BufferView("two", 3));
    mgr1->PrepareReceive(10);
    mgr1->CommitReceive(4);
    mgr2->PrepareReceive(20);
    mgr2->CommitReceive(5);

    mgr1->Swap(*mgr2);
    EXPECT_EQ(mgr1->GetSendQueueSize(), 3);
    EXPECT_EQ(mgr2->GetSendQueueSize(), 3);
    EXPECT_EQ(mgr1->GetReceiveBufferSize(), 5);
    EXPECT_EQ(mgr2->GetReceiveBufferSize(), 4);
}

TEST_F(ConnectionBufferManagerTest, ReceiveStrategy) {
    // 默认策略：翻倍
    manager_->SetReceiveBufferStrategy(nullptr); // 使用默认
    EXPECT_TRUE(manager_->PrepareReceive(100));
    size_t cap1 = manager_->GetReceiveBuffer().size; // 可用空间，即 capacity - committed
    // 这个 cap1 至少 100，但具体值取决于策略
    manager_->CommitReceive(80);
    EXPECT_TRUE(manager_->PrepareReceive(100)); // 需要更多空间
    size_t cap2 = manager_->GetReceiveBuffer().size;
    // 策略应产生更大的空间
    EXPECT_GE(cap2, 100);
    // 自定义策略：固定增量
    manager_->SetReceiveBufferStrategy([](size_t current) { return current + 100; });
    manager_->ClearAll();
    EXPECT_TRUE(manager_->PrepareReceive(50));
    size_t cap3 = manager_->GetReceiveBuffer().size;
    EXPECT_GE(cap3, 50);
    manager_->CommitReceive(50);
    EXPECT_TRUE(manager_->PrepareReceive(100));
    size_t cap4 = manager_->GetReceiveBuffer().size;
    EXPECT_GE(cap4, 100);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}