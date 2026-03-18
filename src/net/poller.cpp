// src/httpserver/net/epoll_poller.cpp
#include "httpserver/net/poller.hpp"

#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <cerrno>
#include <algorithm>

namespace httpserver::net {

//匿名命名空间，内部的辅助函数只在本文件使用
namespace {
// 1面向的对象：单个事件FD
// 2实现的功能：将自定义事件类型转换为epoll事件
// 3执行的场景：仅用在给内核注册 / 修改 fd 监听事件的时机
inline uint32_t to_epoll_events(EventType events) {
    uint32_t epoll_events = 0;
    
    if (static_cast<int>(events & EventType::READ)) {
        epoll_events |= EPOLLIN;
    }
    if (static_cast<int>(events & EventType::WRITE)) {
        epoll_events |= EPOLLOUT;
    }
    if (static_cast<int>(events & EventType::HUP)) {
        epoll_events |= EPOLLHUP;
    }
    if (static_cast<int>(events & EventType::RDHUP)) {
        epoll_events |= EPOLLRDHUP;
    }
    if (static_cast<int>(events & EventType::ERROR)) {
        epoll_events |= EPOLLERR;
    }
    
    // 边缘触发模式
    epoll_events |= EPOLLET;
    
    return epoll_events;
}
// epoll_wait 拿到就绪事件后，将内核 epoll 事件转回自定义 EventType，给业务层用。
inline EventType from_epoll_events(uint32_t epoll_events) {
    EventType events = EventType::NONE;
    
    if (epoll_events & EPOLLIN) {
        events = static_cast<EventType>(static_cast<int>(events) | 
                                        static_cast<int>(EventType::READ));
    }
    if (epoll_events & EPOLLOUT) {
        events = static_cast<EventType>(static_cast<int>(events) | 
                                        static_cast<int>(EventType::WRITE));
    }
    if (epoll_events & EPOLLHUP) {
        events = static_cast<EventType>(static_cast<int>(events) | 
                                        static_cast<int>(EventType::HUP));
    }
    if (epoll_events & EPOLLRDHUP) {
        events = static_cast<EventType>(static_cast<int>(events) | 
                                        static_cast<int>(EventType::RDHUP));
    }
    if (epoll_events & EPOLLERR) {
        events = static_cast<EventType>(static_cast<int>(events) | 
                                        static_cast<int>(EventType::ERROR));
    }
    
    return events;
}
}  // namespace

class EpollPoller final : public Poller {
public:
    explicit EpollPoller(size_t max_events = 1024) 
        : epoll_fd_(-1), max_events_(max_events) {
        
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            throw std::system_error(errno, std::system_category(), 
                                   "epoll_create1 failed");
        }
        
        events_.resize(max_events_);
    }
    
    ~EpollPoller() override {
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }
    }
    
    // 禁止拷贝和移动
    EpollPoller(const EpollPoller&) = delete;
    EpollPoller& operator=(const EpollPoller&) = delete;
    EpollPoller(EpollPoller&&) = delete;
    EpollPoller& operator=(EpollPoller&&) = delete;
    
    bool add_fd(int fd, EventType events, void* user_data = nullptr) override {
        return update_fd(EPOLL_CTL_ADD, fd, events, user_data);
    }
    
    bool mod_fd(int fd, EventType events, void* user_data = nullptr) override {
        return update_fd(EPOLL_CTL_MOD, fd, events, user_data);
    }
    
    bool del_fd(int fd) override {
        if (epoll_fd_ < 0 || fd < 0) {
            return false;
        }
        
        struct epoll_event event;
        std::memset(&event, 0, sizeof(event));
        
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &event) < 0) {
            if (errno != ENOENT) {  // 忽略文件描述符不存在的错误
                return false;
            }
        }
        
        return true;
    }

    int poll(std::vector<Event>& ready_events, int timeout_ms = -1) override {
        if (epoll_fd_ < 0) {
            return -1;
        }
        
        //阻塞等待内核返回就绪的文件描述符 (fd) 事件
        int num_events = ::epoll_wait(epoll_fd_,                        // epoll 文件描述符
                                     events_.data(),                    // 存放就绪事件的数组指针
                                     static_cast<int>(max_events_),     // 当前 epoll 实例允许监听的最大事件数 / 最大 fd 数；
                                     timeout_ms);                       // 等待事件发生的超时时间，单位毫秒，-1表示无限等待
        
        if (num_events < 0) {
            if (errno == EINTR) {
                return 0;  // 被信号中断，返回0个事件
            }
            return -1;
        }
        
        ready_events.clear();
        ready_events.reserve(num_events);
        
        // 解析内核返回的就绪事件
        for (int i = 0; i < num_events; ++i) {
            Event event;
            event.fd = events_[i].data.fd;
            event.events = from_epoll_events(events_[i].events);
            event.user_data = events_[i].data.ptr;
            
            // 存入就绪事件列表
            ready_events.push_back(event);
        }
        
        return num_events;
    }
    
    void set_max_events(size_t max) override {
        if (max > 0 && max != max_events_) {
            max_events_ = max;
            events_.resize(max_events_);
        }
    }
    
    size_t max_events() const override {
        return max_events_;
    }
    
private:
    bool update_fd(int operation, int fd, EventType events, void* user_data) {
        if (epoll_fd_ < 0 || fd < 0) {
            return false;
        }
        
        struct epoll_event event;
        std::memset(&event, 0, sizeof(event));
        
        event.events = to_epoll_events(events);
        event.data.fd = fd;
        event.data.ptr = user_data;
        
        if (::epoll_ctl(epoll_fd_, operation, fd, &event) < 0) {
            return false;
        }
        
        return true;
    }
    
private:
    int epoll_fd_;
    size_t max_events_;
    std::vector<struct epoll_event> events_;
};

// 工厂方法实现
std::unique_ptr<Poller> Poller::create_default() {
    return std::make_unique<EpollPoller>();
}

}  // namespace httpserver::net
// 在poller.cpp末尾添加
int main() {
    auto poller = httpserver::net::Poller::create_default();
    return 0;
}