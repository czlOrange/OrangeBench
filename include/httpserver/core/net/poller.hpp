#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <system_error>

namespace httpserver::net {

// 使用类型别名提高可读性
using EventMask = uint32_t;

// 事件常量（C++17 inline constexpr）
inline constexpr EventMask EVENT_NONE   = 0x00;
inline constexpr EventMask EVENT_READ   = 0x01;
inline constexpr EventMask EVENT_WRITE  = 0x02;
inline constexpr EventMask EVENT_ERROR  = 0x04;
inline constexpr EventMask EVENT_HUP    = 0x08;
inline constexpr EventMask EVENT_RDHUP  = 0x10;

// 回调：fd, 就绪事件, 用户上下文
using EventCallback = std::function<void(int fd, EventMask events, void* ctx)>;

/**
 * @brief 事件轮询器抽象
 * 
 * 封装 select/poll/epoll/kqueue 等系统调用
 * 生命周期：构造 -> add_fd(循环) -> poll(循环) -> 析构
 */
class Poller {
public:
    struct Event {
        int fd;
        EventMask events;      // 就绪的事件
        void* context;         // 用户自定义上下文
    };
    
    virtual ~Poller() = default;
    
    // 核心接口（失败抛出 std::system_error）
    virtual void add_fd(int fd, EventMask events, void* context = nullptr) = 0;
    virtual void mod_fd(int fd, EventMask events, void* context = nullptr) = 0;
    virtual void del_fd(int fd) = 0;
    
    // 轮询事件
    virtual int poll(std::vector<Event>& active_events, int timeout_ms = -1) = 0;
    
    // 查询能力
    virtual size_t max_events() const = 0;
    virtual const char* name() const = 0;  // "epoll"/"kqueue"/"poll"/"select"
    
    // 工厂
    static std::unique_ptr<Poller> create_default();
};

}  // namespace httpserver::net