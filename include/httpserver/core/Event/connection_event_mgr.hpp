#pragma once
#include "event_dispatcher.hpp"

namespace httpserver::core {

// 连接事件管理器（每个连接的专属事件处理）
class ConnectionEventManager {
public:
    
    explicit ConnectionEventManager(std::shared_ptr<IEventDispatcher> dispatcher);
    
    void SetConnection(std::shared_ptr<IConnection> conn);
    
    // 事件触发
    void TriggerEvent(ConnectionEvent event);
    void TriggerData(std::string_view data);
    void TriggerError(std::error_code ec);
    
    // 回调设置
    void SetEventCallback(EventCallback callback);
    void SetDataCallback(DataCallback callback);
    void SetErrorCallback(ErrorCallback callback);
    
    // 批量事件处理
    void BatchEvents(std::function<void()> operation);
    
private:
    std::shared_ptr<IEventDispatcher> dispatcher_;
    std::weak_ptr<IConnection> self_connection_;
    
    EventCallback event_callback_;
    DataCallback data_callback_;
    ErrorCallback error_callback_;
};

} // namespace httpserver::core