// src/core/async/factory.cpp
#include "httpserver/core/Async/async_factory.hpp"
#include "httpserver/core/Async/async_scheduler_impl.hpp"  // ThreadPoolScheduler
#include <thread>  // std::thread::hardware_concurrency

namespace httpserver::core::async {

static std::shared_ptr<IScheduler> CreateDefaultScheduler(size_t thread_count) {
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 1;
    }
    auto scheduler = std::make_shared<ThreadPoolScheduler>(thread_count);
    scheduler->Start();  // ✅ 关键：自动启动调度器
    return scheduler;
}

static std::function<std::shared_ptr<IScheduler>(size_t)> scheduler_creator = CreateDefaultScheduler;

std::shared_ptr<IScheduler> Factory::CreateScheduler(size_t thread_count) {
    auto scheduler = scheduler_creator(thread_count);
    scheduler->Start();  // ✅ 确保启动
    return scheduler;
}

std::unique_ptr<ConnectionOperations> Factory::CreateConnectionOps(
    std::shared_ptr<IScheduler> scheduler) {
    if (!scheduler) {
        scheduler = CreateDefaultScheduler(0);  // 内部已启动
    }
    return std::make_unique<ConnectionOperations>(std::move(scheduler));
}

std::unique_ptr<ConnectionOperations> Factory::CreateDefaultConnectionOps() {
    auto scheduler = CreateScheduler(0);
    return CreateConnectionOps(std::move(scheduler));
}

// 可选：提供注册函数，允许外部替换调度器创建逻辑
// void Factory::SetSchedulerCreator(
//     std::function<std::shared_ptr<IScheduler>(size_t)> creator) {
//     if (creator) {
//         scheduler_creator = std::move(creator);
//     }
// }

} // namespace httpserver::core::async

/*
======================================
使用示例：
======================================
// 方式1：只创建调度器
auto scheduler = Factory::CreateScheduler(4);  // 4个线程
scheduler->Start();

// 方式2：用现有调度器创建连接工具
auto ops = Factory::CreateConnectionOps(scheduler);
ops->SendAsync("Hello");

// 方式3：一键创建完整工具（自动创建调度器）
auto default_ops = Factory::CreateDefaultConnectionOps();
default_ops->ConnectAsync("127.0.0.1", 8080);
*/