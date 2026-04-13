// test/core/async/test_async_operation.cpp
#include <gtest/gtest.h>
#include <thread>
#include "httpserver/core/tcp_connections/Async/async_operation_impl.hpp"

using namespace httpserver::core::async;

// 测试 PromiseOperation 的基本功能
TEST(AsyncOperationTest, InitialState) {
    PromiseOperation<int> op;
    EXPECT_EQ(op.GetState(), State::PENDING);
    EXPECT_FALSE(op.IsCompleted());
    EXPECT_FALSE(op.IsCancelled());
}

TEST(AsyncOperationTest, SetResultAndWait) {
    PromiseOperation<int> op;
    std::thread setter([&op]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        op.SetResult(42);
    });
    bool ready = op.WaitFor(std::chrono::milliseconds(100));
    EXPECT_TRUE(ready);
    EXPECT_EQ(op.GetResult(), 42);
    EXPECT_TRUE(op.IsCompleted());
    setter.join();
}

TEST(AsyncOperationTest, Cancel) {
    PromiseOperation<int> op;
    EXPECT_TRUE(op.Cancel());
    EXPECT_TRUE(op.IsCancelled());
    EXPECT_THROW(op.GetResult(), std::runtime_error);
}

TEST(AsyncOperationTest, Timeout) {
    PromiseOperation<int> op;
    bool ready = op.WaitFor(std::chrono::milliseconds(10));
    EXPECT_FALSE(ready);
    EXPECT_EQ(op.GetState(), State::PENDING);
}

TEST(AsyncOperationTest, Callbacks) {
    PromiseOperation<int> op;
    std::atomic<bool> completed{false};
    std::atomic<bool> cancelled{false};
    op.SetOnCompleted([&]() { completed = true; });
    op.SetOnCancelled([&]() { cancelled = true; });
    op.SetResult(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(completed);
    EXPECT_FALSE(cancelled);
}

TEST(AsyncOperationTest, Exception) {
    PromiseOperation<int> op;
    op.SetException(std::make_exception_ptr(std::runtime_error("test")));
    EXPECT_THROW(op.GetResult(), std::runtime_error);
    auto e = op.GetException();
    EXPECT_NE(e, nullptr);
    try {
        std::rethrow_exception(e);
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "test");
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}