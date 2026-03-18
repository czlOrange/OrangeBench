// include/httpserver/core/async/async_scheduler.hpp
#pragma once

#include "async_common.hpp"
#include <memory>
#include <functional>
#include <vector>
#include <chrono>

namespace httpserver::core::async {

// 调度器统计信息 - 就像快递分拣中心的实时监控大屏
struct SchedulerStats {
    size_t pending_tasks{0};    // 待处理任务数（传送带上等待分拣的包裹数量）
    size_t running_tasks{0};    // 正在执行的任务数（分拣员手中正在处理的包裹数量）
    size_t completed_tasks{0};  // 已完成任务数（今天成功分拣的包裹总数）
    size_t failed_tasks{0};     // 失败任务数（破损或地址不清的失败包裹数）
    size_t worker_threads{0};   // 工作线程数（分拣中心雇佣的分拣员总人数）
};

// 异步任务调度器接口 - 就像快递分拣中心
class IScheduler {
public:
    virtual ~IScheduler() = default;  // 虚析构函数
    
    // 核心调度接口（模板方法不能在虚函数中，将在实现类中提供）
    // 这里只声明概念，实际使用时通过子类调用
    
    // 批量调度
    virtual void ScheduleBatch(std::vector<std::function<void()>> tasks) = 0;
    
    // 定时任务
    virtual void ScheduleAfter(std::function<void()> task, 
                              std::chrono::milliseconds delay) = 0;
    virtual void ScheduleEvery(std::function<void()> task,
                              std::chrono::milliseconds interval) = 0;
    
    // 生命周期控制
    virtual void Start() = 0;   // 分拣中心开门营业
    virtual void Stop() = 0;    // 分拣中心关门歇业
    virtual void Pause() = 0;   // 临时暂停（午休时间）
    virtual void Resume() = 0;  // 恢复工作
    
    // 监控与配置
    virtual SchedulerStats GetStats() const = 0;     // 查看今天处理了多少包裹
    virtual void SetMaxConcurrent(size_t max) = 0;   // 最多同时几个分拣员工作
    virtual void SetThreadPoolSize(size_t size) = 0; // 雇佣多少个分拣员
};

} // namespace httpserver::core::async