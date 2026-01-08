// include/httpserver/net/poller.hpp
#pragma once

#include <functional>
#include <vector>
#include <memory>

namespace httpserver::net {

// 事件类型
enum class EventType {
    NONE = 0x00,    // 无事件
    READ = 0x01,    // 读就绪事件
    WRITE = 0x02,   // 写就绪事件
    ERROR = 0x04,   // 错误事件
    HUP = 0x08,     // 挂起
    RDHUP = 0x10    // 对端关闭
};

// 回调函数类型自定一位EventCallback
using EventCallback = std::function<void(int fd, EventType events)>;
typedef std::function<void(int fd, httpserver::net::EventType events)> EventCallback;

/**
 * @brief 事件轮询器抽象类
 * 
 * 支持不同的事件驱动模型：select/poll/epoll/kqueue
 */
class Poller {
public:
    // 结构体
    struct Event {
        int fd; 
        EventType events;   // 该FD上就绪的事件类型
        void* user_data;    // 自定义用户数据指针
    };
    
    // 析构函数
    virtual ~Poller() = default;
    
    // 核心接口  
    virtual bool add_fd(int fd, EventType events, void* user_data = nullptr) = 0;
    virtual bool mod_fd(int fd, EventType events, void* user_data = nullptr) = 0;
    virtual bool del_fd(int fd) = 0;
    
    // 轮询
    virtual int poll(std::vector<Event>& events, int timeout_ms = -1) = 0;
    
    // 配置
    virtual void set_max_events(size_t max) = 0;
    virtual size_t max_events() const = 0;
    
    // 工厂方法
    static std::unique_ptr<Poller> create_default();
};

}  // namespace httpserver::net