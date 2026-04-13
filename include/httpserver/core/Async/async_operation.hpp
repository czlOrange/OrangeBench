// include/httpserver/core/async/async_operation.hpp
#pragma once

#include "async_common.hpp"

namespace httpserver::core::async {

// 基础异步操作接口
// 就像快递订单，可以查询状态、等待送达、设置通知
class IOperation {
public:
    virtual ~IOperation() = default;
    
    // 状态查询
    virtual State GetState() const = 0;         // 查询操作当前处于什么阶段（看快递到哪了）
    virtual bool IsCompleted() const = 0;       // 快速判断是否完成（快递到了没？）
    virtual bool IsCancelled() const = 0;       // 快速判断是否取消（订单取消了没？）
    
    // 等待与取消
    virtual bool WaitFor(std::chrono::milliseconds timeout) = 0;  // 等待一段时间看能否完成（等快递5分钟，不来就算了）
    virtual bool Cancel() = 0;                   // 尝试取消操作（取消订单）
    
    // 回调设置
    virtual void SetOnCompleted(Callback callback) = 0;   // 设置完成回调（到了给我打电话）
    virtual void SetOnCancelled(Callback callback) = 0;   // 设置取消回调（取消给我打电话）
};

// 带返回值的异步操作接口
// 就像能打开查看的快递包裹
template<typename T>
class ITypedOperation : public IOperation {
public:
    virtual T GetResult() = 0;                      // 获取操作结果（打开快递盒拿东西）
    virtual std::exception_ptr GetException() = 0;  // 获取操作失败的原因（快递损坏了，看看原因）
};

} // namespace httpserver::core::async