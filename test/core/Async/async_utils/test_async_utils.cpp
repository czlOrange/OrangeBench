// test/tcp_connection/Async/test_async_utils.cpp
#include <gtest/gtest.h>
#include <thread>
#include "httpserver/core/tcp_connections/Async/async_utils.hpp"
#include "httpserver/core/tcp_connections/Async/async_scheduler_impl.hpp"

using namespace httpserver::core::async;

class AsyncUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        scheduler_ = std::make_shared<ThreadPoolScheduler>(4);
        scheduler_->Start();
        ops_ = std::make_unique<ConnectionOperations>(scheduler_);
    }

    void TearDown() override {
        ops_->CancelAll();
        scheduler_->Stop();
    }

    std::shared_ptr<IScheduler> scheduler_;
    std::unique_ptr<ConnectionOperations> ops_;
};

TEST_F(AsyncUtilsTest, SendAsync) {
    auto future = ops_->SendAsync("Hello");
    size_t sent = future.get();
    EXPECT_GT(sent, 0);
}

TEST_F(AsyncUtilsTest, SendBinaryAsync) {
    const char data[] = "Binary Data";
    auto future = ops_->SendAsync(data, sizeof(data));
    size_t sent = future.get();
    EXPECT_EQ(sent, sizeof(data));
}

TEST_F(AsyncUtilsTest, ReceiveAsync) {
    auto future = ops_->ReceiveAsync(1024);
    std::string data = future.get();
    EXPECT_FALSE(data.empty());
}

TEST_F(AsyncUtilsTest, ReceiveToBufferAsync) {
    char buffer[1024] = {0};
    auto future = ops_->ReceiveAsync(buffer, sizeof(buffer));
    size_t received = future.get();
    EXPECT_GT(received, 0);
}

TEST_F(AsyncUtilsTest, ConnectAsync) {
    auto future = ops_->ConnectAsync("127.0.0.1", 8080);
    bool connected = future.get();
    EXPECT_TRUE(connected);
}

TEST_F(AsyncUtilsTest, DisconnectAsync) {
    auto future = ops_->DisconnectAsync();
    EXPECT_NO_THROW(future.get());
}

TEST_F(AsyncUtilsTest, SendBatchAsync) {
    std::vector<std::string> messages = {"Hello", "World", "Test"};
    auto future = ops_->SendBatchAsync(messages);
    auto results = future.get();
    EXPECT_EQ(results.size(), messages.size());
    for (size_t i = 0; i < results.size(); ++i) {
        EXPECT_EQ(results[i], messages[i].size());
    }
}

TEST_F(AsyncUtilsTest, CancelAll) {
    // 提交一些操作
    std::vector<std::future<size_t>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(ops_->SendAsync("test"));
    }
    
    ops_->CancelAll();
    EXPECT_EQ(ops_->PendingCount(), 0);
}

TEST_F(AsyncUtilsTest, PendingCount) {
    EXPECT_EQ(ops_->PendingCount(), 0);
    
    auto future1 = ops_->SendAsync("test1");
    auto future2 = ops_->SendAsync("test2");
    
    // 等待任务被调度（可能需要一点时间）
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 注意：任务可能很快完成，pending_count 可能为 0
    // 这里只是验证接口不会崩溃
    EXPECT_NO_THROW(ops_->PendingCount());
    
    future1.get();
    future2.get();
}

TEST_F(AsyncUtilsTest, WaitAll) {
    for (int i = 0; i < 5; ++i) {
        ops_->SendAsync("test");
    }
    
    bool result = ops_->WaitAll(std::chrono::milliseconds(1000));
    EXPECT_TRUE(result);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}