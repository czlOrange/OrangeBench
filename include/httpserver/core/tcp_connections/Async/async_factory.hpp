// include/httpserver/core/async/factory.hpp
#pragma once

#include "async_scheduler.hpp"
#include "async_utils.hpp"
#include <memory>

namespace httpserver::core::async {

// 异步组件工厂 - 就像快递设备制造公司，负责生产各种快递中心所需的设备
class Factory {
public:
    // 创建调度器 - 就像建造一个快递分拣中心（只建分拣中心，不配置前台）
    // thread_count: 分拣中心雇佣的分拣员人数，0表示使用默认配置（CPU核心数）
    static std::shared_ptr<IScheduler> CreateScheduler(
        size_t thread_count = 0);
    
    // 创建连接操作工具 - 就像给已有的分拣中心配置前台控制台
    // 需要传入一个已有的分拣中心（调度器），配置成完整的快递服务系统
    static std::unique_ptr<ConnectionOperations> CreateConnectionOps(
        std::shared_ptr<IScheduler> scheduler);
    
    // 创建带默认调度的连接操作工具 - 就像一键开通完整的快递网点
    // 工厂自动建好分拣中心（默认调度器），并配好前台，直接给你一个完整的快递网点
    static std::unique_ptr<ConnectionOperations> CreateDefaultConnectionOps();
};

} // namespace httpserver::core::async