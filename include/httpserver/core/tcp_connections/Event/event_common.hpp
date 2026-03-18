#pragma once
#include "connection_state.hpp"
#include <memory>
#include <functional>

namespace httpserver::core {

// 前向声明
class IConnection;

// 事件回调类型
using EventCallback = std::function<void(ConnectionEvent, std::shared_ptr<IConnection>)>;
using DataCallback = std::function<void(std::shared_ptr<IConnection>, std::string_view)>;
using ErrorCallback = std::function<void(std::shared_ptr<IConnection>, std::error_code)>;

} // namespace httpserver::core