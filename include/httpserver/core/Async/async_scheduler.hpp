// include/httpserver/core/async/async_scheduler.hpp
#pragma once

#include "async_common.hpp"
#include <memory>
#include <functional>
#include <vector>
#include <future>
#include <chrono>

namespace httpserver::core::async {

// 调度器统计信息
struct SchedulerStats {
    size_t pending_tasks{0};
    size_t running_tasks{0};
    size_t completed_tasks{0};
    size_t failed_tasks{0};
    size_t worker_threads{0};
};

// 异步任务调度器接口
class IScheduler {
public:
    virtual ~IScheduler() = default;

    // 核心调度接口（模板方法）
    template<typename Func, typename... Args>
    auto Schedule(Func&& func, Args&&... args)
        -> Result<std::invoke_result_t<Func, Args...>> {
        using ResultType = std::invoke_result_t<Func, Args...>;
        auto task = std::make_shared<std::packaged_task<ResultType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        auto future = task->get_future();
        ScheduleTask([task]() { (*task)(); });
        return future;
    }

    // 批量调度
    virtual void ScheduleBatch(std::vector<std::function<void()>> tasks) = 0;

    // 定时任务
    virtual void ScheduleAfter(std::function<void()> task,
                              std::chrono::milliseconds delay) = 0;
    virtual void ScheduleEvery(std::function<void()> task,
                              std::chrono::milliseconds interval) = 0;

    // 生命周期控制
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;

    // 监控与配置
    virtual SchedulerStats GetStats() const = 0;
    virtual void SetMaxConcurrent(size_t max) = 0;
    virtual void SetThreadPoolSize(size_t size) = 0;

    // 工厂方法
    static std::shared_ptr<IScheduler> CreateDefault();

protected:
    // 子类必须实现此方法，将任务加入调度队列
    virtual void ScheduleTask(std::function<void()> task) = 0;
};

} // namespace httpserver::core::async