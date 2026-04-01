#pragma once
#include "../Connection/connection_state.hpp"
#include "event_subscriber.hpp"
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include <system_error>
namespace httpserver::core {

// ========== 商场广播台（全局事件分发器） ==========
// 负责整个商场的信息广播：火灾报警、寻人启事、促销活动
// 任何店铺或顾客都可以订阅自己关心的消息
class IEventDispatcher {
public:
    virtual ~IEventDispatcher() = default;
    
    // -------- 广播功能（发布事件）--------
    // 商场发生状况时，向全商场广播
    virtual void PublishEvent(ConnectionEvent event, std::shared_ptr<IConnection> conn) = 0;
    // 商场广播："3楼服装店火灾，请疏散！"
    // 这里 event = 火灾，conn = 3楼服装店
    
    virtual void PublishData(std::shared_ptr<IConnection> conn,std::string_view data) = 0;
    // 商场广播："2楼餐饮店新到新鲜食材，欢迎选购！"
    // 这里 conn = 2楼餐饮店，data = "新鲜食材"
    
    virtual void PublishError(std::shared_ptr<IConnection> conn,std::error_code ec) = 0;
    // 商场广播："4楼电梯故障，正在维修！"
    // 这里 conn = 4楼电梯，ec = 电梯故障码
    
    
    // -------- 订阅功能（谁想听广播）--------
    // 保安想听：火灾、盗窃报警
    // 保洁想听：漏水、垃圾满
    // 店铺想听：自己店的促销活动
    virtual void SubscribeEvent(ConnectionEvent event, EventCallback callback) = 0;
    // 保安订阅：event=火灾，callback=去救火
    
    virtual void SubscribeData(DataCallback callback) = 0;
    // 顾客订阅：收到"新到食材"就去看
    
    virtual void SubscribeError(ErrorCallback callback) = 0;
    // 维修工订阅：收到"电梯故障"就去修
    
    
    // -------- 收件人管理（记录谁想听什么）--------
    virtual void RegisterSubscriber(std::shared_ptr<IEventSubscriber> subscriber) = 0;
    // 保安到广播台登记："我是保安，火灾、盗窃都通知我"
    
    virtual void UnregisterSubscriber(const std::string& subscriber_id) = 0;
    // 保安下班了："我不听广播了"
    
    virtual void UnregisterAllSubscribers() = 0;
    // 商场关门，清空所有登记
    
    
    // -------- 过滤器（决定哪些消息可以播）--------
    using EventFilter = std::function<bool(ConnectionEvent, std::shared_ptr<IConnection>)>;
    virtual void AddEventFilter(EventFilter filter) = 0;
    // 添加过滤器：只播报重要事件，小事情不广播
    
    virtual void RemoveEventFilter(size_t index) = 0;
    // 移除某个过滤器
    
    
    // -------- 精准路由（点对点通知）--------
    virtual void RouteEvent(ConnectionEvent event, std::shared_ptr<IConnection> conn,const std::string& route_key) = 0;
    // 只通知特定的人："301房间客人，您的快递到了"
    // route_key = "301房间"
    
    
    // ----- 统计信息（广播台工作日志）-----
    struct DispatchStats {
        size_t total_events;        // 总共广播了多少次
        size_t total_data;          // 总共广播了多少条商品信息
        size_t total_errors;        // 总共广播了多少次故障
        size_t active_subscribers;  // 当前有多少人在听广播
        std::unordered_map<ConnectionEvent, size_t> event_counts;  // 各类事件数量
    };
    virtual DispatchStats GetStats() const = 0;
    // 店长查看："今天广播了10次，其中5次促销，3次寻人，2次故障"
    
    
    // ----- 广播台设置 -----
    virtual void SetMaxQueueSize(size_t size) = 0;
    // 设置广播室最多排队多少条消息，满了就丢弃
    
    virtual void SetAsyncDispatch(bool enable) = 0;
    // 设置是否启用备用广播员（异步模式）
    
    
    // ----- 广播台创建 -----
    static std::shared_ptr<IEventDispatcher> CreateDefault();
    // 商场开业，建立广播台
};

} // namespace httpserver::core

