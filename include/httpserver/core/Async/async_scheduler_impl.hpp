// include/httpserver/core/async/async_scheduler_impl.hpp
#pragma once

#include "async_scheduler.hpp"
#include <memory>

namespace httpserver::core::async {

class ThreadPoolScheduler : public IScheduler {
public:
    explicit ThreadPoolScheduler(size_t thread_count = 0);
    ~ThreadPoolScheduler() override;

    void Start() override;
    void Stop() override;
    void Pause() override;
    void Resume() override;

    void ScheduleBatch(std::vector<std::function<void()>> tasks) override;
    void ScheduleAfter(std::function<void()> task, std::chrono::milliseconds delay) override;
    void ScheduleEvery(std::function<void()> task, std::chrono::milliseconds interval) override;

    SchedulerStats GetStats() const override;
    void SetMaxConcurrent(size_t max) override;
    void SetThreadPoolSize(size_t size) override;

protected:
    void ScheduleTask(std::function<void()> task) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // 线程循环函数
    void workerLoop();
    void timerLoop();
};

} // namespace httpserver::core::async