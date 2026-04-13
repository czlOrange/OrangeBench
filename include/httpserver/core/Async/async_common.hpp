// include/httpserver/core/async/async_common.hpp
#pragma once

#include <future>
#include <chrono>

namespace httpserver::core::async {

// 异步操作结果类型别名("快递单号")
template<typename T>
using Result = std::future<T>;

// 异步操作状态枚举
enum class State {
    PENDING,    // 等待执行
    RUNNING,    // 执行中
    COMPLETED,  // 已完成
    CANCELLED,  // 已取消
    FAILED      // 执行失败
};

// 可以装下任何"无参数、无返回值"的可调用对象，让回调处理更加灵活统一。("收货电话")
using Callback = std::function<void()>;

} // namespace httpserver::core::async