#pragma once
#include "../Connection/connection_state.hpp"
#include <memory>
#include <functional>
#include <system_error>
namespace httpserver::core {

// 前向声明
class IConnection;

// "发生了什么事情" == 事件回调：事件类型 + 连接对象
using EventCallback = std::function<void(ConnectionEvent, std::shared_ptr<IConnection>)>;

// "收到了什么数据" == 数据回调：连接对象 + 数据
using DataCallback = std::function<void(std::shared_ptr<IConnection>, std::string_view)>;

// "发生了什么错误" == 错误回调：连接对象 + 错误码
using ErrorCallback = std::function<void(std::shared_ptr<IConnection>, std::error_code)>;

} // namespace httpserver::core