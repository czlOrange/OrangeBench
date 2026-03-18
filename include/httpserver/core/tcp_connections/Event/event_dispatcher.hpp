#pragma once
#include "event_common.hpp"
#include "event_subscriber.hpp"
#include <unordered_map>
#include <vector>

namespace httpserver::core {

// 事件分发器接口
class IEventDispatcher {
public:
    virtual ~IEventDispatcher() = default;
    
    // 事件发布
    virtual void PublishEvent(ConnectionEvent event, 
                             std::shared_ptr<IConnection> conn) = 0;
    virtual void PublishData(std::shared_ptr<IConnection> conn,
                            std::string_view data) = 0;
    virtual void PublishError(std::shared_ptr<IConnection> conn,
                             std::error_code ec) = 0;
    
    // 直接回调订阅
    virtual void SubscribeEvent(ConnectionEvent event, EventCallback callback) = 0;
    virtual void SubscribeData(DataCallback callback) = 0;
    virtual void SubscribeError(ErrorCallback callback) = 0;
    
    // 订阅者管理
    virtual void RegisterSubscriber(std::shared_ptr<IEventSubscriber> subscriber) = 0;
    virtual void UnregisterSubscriber(const std::string& subscriber_id) = 0;
    virtual void UnregisterAllSubscribers() = 0;
    
    // 事件过滤
    using EventFilter = std::function<bool(ConnectionEvent, std::shared_ptr<IConnection>)>;
    virtual void AddEventFilter(EventFilter filter) = 0;
    virtual void RemoveEventFilter(size_t index) = 0;
    
    // 事件路由
    virtual void RouteEvent(ConnectionEvent event, 
                          std::shared_ptr<IConnection> conn,
                          const std::string& route_key) = 0;
    
    // 统计信息
    struct DispatchStats {
        size_t total_events;
        size_t total_data;
        size_t total_errors;
        size_t active_subscribers;
        std::unordered_map<ConnectionEvent, size_t> event_counts;
    };
    
    virtual DispatchStats GetStats() const = 0;
    
    // 配置
    virtual void SetMaxQueueSize(size_t size) = 0;
    virtual void SetAsyncDispatch(bool enable) = 0;
    
    // 工厂方法
    static std::shared_ptr<IEventDispatcher> CreateDefault();
};

} // namespace httpserver::core