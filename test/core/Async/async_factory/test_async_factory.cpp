// test/core/async/factory_test.cpp
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "httpserver/core/Async/async_factory.hpp"

using namespace httpserver::core::async;

class FactoryTest : public ::testing::Test {
protected:
    void TearDown() override {
        // 清理：确保所有调度器都已停止
        // 由于是静态方法，无法统一清理，各测试自行管理
    }
};

// ============================================================================
// CreateScheduler 测试
// ============================================================================

TEST_F(FactoryTest, CreateSchedulerDefault) {
    auto scheduler = Factory::CreateScheduler();
    ASSERT_NE(scheduler, nullptr);
    
    scheduler->Start();
    auto stats = scheduler->GetStats();
    EXPECT_GT(stats.worker_threads, 0);
    scheduler->Stop();
}

TEST_F(FactoryTest, CreateSchedulerWithThreadCount) {
    size_t thread_count = 4;
    auto scheduler = Factory::CreateScheduler(thread_count);
    ASSERT_NE(scheduler, nullptr);
    
    scheduler->Start();
    auto stats = scheduler->GetStats();
    EXPECT_EQ(stats.worker_threads, thread_count);
    scheduler->Stop();
}

TEST_F(FactoryTest, CreateSchedulerZeroThreads) {
    // 0 表示使用 CPU 核心数
    auto scheduler = Factory::CreateScheduler(0);
    ASSERT_NE(scheduler, nullptr);
    
    scheduler->Start();
    auto stats = scheduler->GetStats();
    EXPECT_GT(stats.worker_threads, 0);
    scheduler->Stop();
}

// ============================================================================
// CreateConnectionOps 测试
// ============================================================================

TEST_F(FactoryTest, CreateConnectionOpsWithScheduler) {
    auto scheduler = Factory::CreateScheduler(2);
    scheduler->Start();
    
    auto ops = Factory::CreateConnectionOps(scheduler);
    ASSERT_NE(ops, nullptr);
    
    // 验证可以使用
    auto future = ops->SendAsync("test");
    size_t sent = future.get();
    EXPECT_GT(sent, 0);
    
    scheduler->Stop();
}

TEST_F(FactoryTest, CreateConnectionOpsWithNullScheduler) {
    std::cout << "Test started" << std::endl;
    
    auto ops = Factory::CreateConnectionOps(nullptr);
    std::cout << "After CreateConnectionOps" << std::endl;
    
    ASSERT_NE(ops, nullptr);
    std::cout << "After assert" << std::endl;
    
    auto future = ops->SendAsync("test");
    std::cout << "After SendAsync" << std::endl;
    
    size_t sent = future.get();
    std::cout << "After get: " << sent << std::endl;
    
    EXPECT_GT(sent, 0);
    std::cout << "Test finished" << std::endl;
}

// ============================================================================
// CreateDefaultConnectionOps 测试
// ============================================================================

TEST_F(FactoryTest, CreateDefaultConnectionOps) {
    auto ops = Factory::CreateDefaultConnectionOps();
    ASSERT_NE(ops, nullptr);
    
    // 验证可以使用
    auto future = ops->SendAsync("hello");
    size_t sent = future.get();
    EXPECT_GT(sent, 0);
}

TEST_F(FactoryTest, DefaultConnectionOpsMultipleOperations) {
    auto ops = Factory::CreateDefaultConnectionOps();
    ASSERT_NE(ops, nullptr);
    
    // 执行多个操作
    auto future1 = ops->SendAsync("message1");
    auto future2 = ops->SendAsync("message2");
    auto future3 = ops->SendAsync("message3");
    
    size_t sent1 = future1.get();
    size_t sent2 = future2.get();
    size_t sent3 = future3.get();
    
    EXPECT_GT(sent1, 0);
    EXPECT_GT(sent2, 0);
    EXPECT_GT(sent3, 0);
}

// ============================================================================
// 集成测试
// ============================================================================

TEST_F(FactoryTest, MultipleSchedulers) {
    auto scheduler1 = Factory::CreateScheduler(2);
    auto scheduler2 = Factory::CreateScheduler(4);
    
    scheduler1->Start();
    scheduler2->Start();
    
    auto ops1 = Factory::CreateConnectionOps(scheduler1);
    auto ops2 = Factory::CreateConnectionOps(scheduler2);
    
    auto future1 = ops1->SendAsync("test1");
    auto future2 = ops2->SendAsync("test2");
    
    EXPECT_EQ(future1.get(), 5);  // "test1" 长度 = 5
    EXPECT_EQ(future2.get(), 5);  // "test2" 长度 = 5
    
    scheduler1->Stop();
    scheduler2->Stop();
}

TEST_F(FactoryTest, FactoryReusability) {
    // 多次调用工厂方法应该返回独立的对象
    auto ops1 = Factory::CreateDefaultConnectionOps();
    auto ops2 = Factory::CreateDefaultConnectionOps();
    
    ASSERT_NE(ops1, nullptr);
    ASSERT_NE(ops2, nullptr);
    ASSERT_NE(ops1.get(), ops2.get());  // 不同的对象
}

// ============================================================================
// 压力测试
// ============================================================================

TEST_F(FactoryTest, ManySchedulerCreations) {
    const int count = 10;
    std::vector<std::shared_ptr<IScheduler>> schedulers;
    
    for (int i = 0; i < count; ++i) {
        auto scheduler = Factory::CreateScheduler(2);
        scheduler->Start();
        schedulers.push_back(std::move(scheduler));
    }
    
    // 所有调度器都应该正常工作
    for (auto& scheduler : schedulers) {
        auto ops = Factory::CreateConnectionOps(scheduler);
        auto future = ops->SendAsync("test");
        EXPECT_EQ(future.get(), 4);
    }
    
    // 清理
    for (auto& scheduler : schedulers) {
        scheduler->Stop();
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}