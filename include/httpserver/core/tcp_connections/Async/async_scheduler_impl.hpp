// include/httpserver/core/async/async_scheduler_impl.hpp
#pragma once

#include "async_scheduler.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <chrono>
#include <memory>
#include <future>

namespace httpserver::core::async {

// 基于线程池的调度器实现
// 就像一个快递分拣中心，有多个分拣员（线程）同时处理包裹（任务）
class ThreadPoolScheduler : public IScheduler {
public:
    // 构造函数：创建分拣中心，雇佣指定数量的分拣员（thread_count）
    // 默认雇佣CPU核心数那么多的分拣员
    explicit ThreadPoolScheduler(size_t thread_count = std::thread::hardware_concurrency());
    
    // 析构函数：关闭分拣中心，遣散所有分拣员
    ~ThreadPoolScheduler() override;
    
    // 核心调度接口（模板方法）
    template<typename Func, typename... Args>
    auto Schedule(Func&& func, Args&&... args) 
        -> std::future<std::invoke_result_t<Func, Args...>> {
        
        using ResultType = std::invoke_result_t<Func, Args...>;
        auto task = std::make_shared<std::packaged_task<ResultType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        
        auto future = task->get_future();
        ScheduleTask([task]() { (*task)(); });
        return future;
    }
    
    // 批量接收包裹（一次接收多个任务）
    void ScheduleBatch(std::vector<std::function<void()>> tasks) override;
    
    // 延迟任务（"这个包裹明天再处理"）
    void ScheduleAfter(std::function<void()> task, std::chrono::milliseconds delay) override;
    
    // 周期任务（"每小时清点一次库存"）
    void ScheduleEvery(std::function<void()> task, std::chrono::milliseconds interval) override;
    
    // 生命周期控制
    void Start() override;   // 分拣中心开门营业
    void Stop() override;    // 分拣中心关门歇业
    void Pause() override;   // 临时暂停（午休时间）
    void Resume() override;  // 恢复工作
    
    // 查看监控大屏（今天处理了多少包裹）
    SchedulerStats GetStats() const override;
    
    // 设置最多同时处理多少包裹（限制并发数）
    void SetMaxConcurrent(size_t max) override;
    
    // 调整分拣员人数（动态扩招或裁员）
    void SetThreadPoolSize(size_t size) override;
    
private:
    // 内部任务调度方法（供模板方法调用）
    void ScheduleTask(std::function<void()> task);
    
    // 分拣员的工作流程（不断从传送带拿包裹处理）
    void WorkerLoop();
    // 处理延迟包裹的专用流程
    void ProcessDelayedTasks();
    
    // 分拣中心的内部布局图（实现细节）
    struct Impl;
    // 指向实际分拣中心的指针
    std::unique_ptr<Impl> impl_;
};

} // namespace httpserver::core::async