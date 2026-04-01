// src/core/tcp_connections/Event/connection_event_mgr.cpp
// 职责：每个连接（如一个房间）的专属“客服专员”，负责该房间内所有消息的接收、私人处理、上报物业
#include "httpserver/core/tcp_connections/Event/connection_event_mgr.hpp"
#include <utility>

namespace httpserver::core {

// 构造函数：给这个房间配一个客服专员，专员知道物业总台（dispatcher）的联系方式
// 就像新开一间房，前台把对讲机（dispatcher）交给房间管家
ConnectionEventManager::ConnectionEventManager(std::shared_ptr<IEventDispatcher> dispatcher)
    : dispatcher_(std::move(dispatcher)) {
}

// 设置这个房间的编号（连接对象），让管家知道自己在为哪个房间服务
void ConnectionEventManager::SetConnection(std::shared_ptr<IConnection> conn) {
    self_connection_ = conn;
}

// 房间发生了状态变化（比如窗户打开、空调启动）
// 管家先按照房主私人要求处理（比如先发短信给房主），再上报给物业中心
void ConnectionEventManager::TriggerEvent(ConnectionEvent event) {
    auto conn = self_connection_.lock();
    if (!conn) return;

    // 先通知房主私人电话（私有回调）
    if (event_callback_) {
        event_callback_(event, conn);
    }

    // 再上报给物业总台（全局分发器）
    if (dispatcher_) {
        dispatcher_->PublishEvent(event, std::move(conn));
    }
}

// 房间收到了新货物（比如快递送到）
// 管家先喊房主“您的快递到了”，然后通知物业“XX房间已签收”
void ConnectionEventManager::TriggerData(std::string_view data) {
    auto conn = self_connection_.lock();
    if (!conn) return;

    if (data_callback_) {
        data_callback_(conn, data);
    }

    if (dispatcher_) {
        dispatcher_->PublishData(std::move(conn), data);
    }
}

// 房间出了问题（比如水管爆了）
// 管家先告诉房主（私有错误回调），再向物业报修
void ConnectionEventManager::TriggerError(std::error_code ec) {
    auto conn = self_connection_.lock();
    if (!conn) return;

    if (error_callback_) {
        error_callback_(conn, ec);
    }

    if (dispatcher_) {
        dispatcher_->PublishError(std::move(conn), ec);
    }
}

// 设置房主的私人电话（状态变化时打给谁）
void ConnectionEventManager::SetEventCallback(EventCallback callback) {
    event_callback_ = std::move(callback);
}

// 设置房主的收货电话（收到货时通知谁）
void ConnectionEventManager::SetDataCallback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

// 设置房主的报修电话（出问题时通知谁）
void ConnectionEventManager::SetErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

// 批量处理房间事务（比如同时处理多个快递、多个维修单）
// 管家可以把几件事打包一次上报，提高效率
void ConnectionEventManager::BatchEvents(std::function<void()> operation) {
    if (operation) {
        operation();
    }
}

} // namespace httpserver::core