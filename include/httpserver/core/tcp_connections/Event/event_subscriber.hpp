#pragma once
#include "event_common.hpp"

namespace httpserver::core {

// ========== 快递收件人（订阅者） ==========
// 每个人收到快递通知后的处理方式不同
// 有人会直接拆开，有人会放快递柜，有人会让家人代收
class IEventSubscriber {
public:
    virtual ~IEventSubscriber() = default;
    
    // 收到快递状态短信："您的包裹已揽收/运输中/已签收"
    // 你可能会：打开App看看物流详情、转发给家人、标记日历
    virtual void OnEvent(ConnectionEvent event, std::shared_ptr<IConnection> conn) = 0;
    
    // 收到取件通知："您的包裹到了，里面有 XXX"
    // 你可能会：下楼取件、让邻居帮忙、申请放快递柜
    virtual void OnData(std::shared_ptr<IConnection> conn, std::string_view data) = 0;
    
    // 收到异常短信："您的包裹丢了/破损了"
    // 你可能会：联系客服、申请理赔、投诉物流
    virtual void OnError(std::shared_ptr<IConnection> conn, std::error_code ec) = 0;
    
    // 收件人ID（手机号/门牌号/取件码）
    // 快递员要知道你是谁，才能准确投递
    virtual std::string GetSubscriberId() const = 0;
};

} // namespace httpserver::core