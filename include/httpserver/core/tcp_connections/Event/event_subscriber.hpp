#pragma once
#include "event_common.hpp"

namespace httpserver::core {

// 事件订阅者接口
class IEventSubscriber {
public:
    virtual ~IEventSubscriber() = default;
    
    virtual void OnEvent(ConnectionEvent event, std::shared_ptr<IConnection> conn) = 0;
    virtual void OnData(std::shared_ptr<IConnection> conn, std::string_view data) = 0;
    virtual void OnError(std::shared_ptr<IConnection> conn, std::error_code ec) = 0;
    
    virtual std::string GetSubscriberId() const = 0;
};

} // namespace httpserver::core