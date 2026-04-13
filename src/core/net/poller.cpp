// src/net/epoll_poller.cpp
#include "httpserver/core/net/poller.hpp"

#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <system_error>
#include <cerrno>
#include <unordered_map>

namespace httpserver::net {

namespace {

// 将用户事件掩码转换为 epoll 事件标志
uint32_t to_epoll_events(EventMask events) {
    uint32_t epoll_events = 0;
    if (events & EVENT_READ)   epoll_events |= EPOLLIN;
    if (events & EVENT_WRITE)  epoll_events |= EPOLLOUT;
    if (events & EVENT_HUP)    epoll_events |= EPOLLHUP;
    if (events & EVENT_RDHUP)  epoll_events |= EPOLLRDHUP;
    if (events & EVENT_ERROR)  epoll_events |= EPOLLERR;
    epoll_events |= EPOLLET;
    return epoll_events;
}

// 将 epoll 事件标志转换为用户事件掩码
EventMask from_epoll_events(uint32_t epoll_events) {
    EventMask events = 0;
    if (epoll_events & EPOLLIN)      events |= EVENT_READ;
    if (epoll_events & EPOLLOUT)     events |= EVENT_WRITE;
    if (epoll_events & EPOLLHUP)     events |= EVENT_HUP;
    if (epoll_events & EPOLLRDHUP)   events |= EVENT_RDHUP;
    if (epoll_events & EPOLLERR)     events |= EVENT_ERROR;
    return events;
}

} // namespace

class EpollPoller final : public Poller {
public:
    explicit EpollPoller(size_t max_events = 1024)
        : max_events_(max_events), epoll_fd_(-1) {
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
        }
    }

    EpollPoller(const EpollPoller&) = delete;
    EpollPoller& operator=(const EpollPoller&) = delete;
    EpollPoller(EpollPoller&&) = delete;
    EpollPoller& operator=(EpollPoller&&) = delete;

    void add_fd(int fd, EventMask events, void* context = nullptr) override {
        update_fd(EPOLL_CTL_ADD, fd, events, context);
    }

    void mod_fd(int fd, EventMask events, void* context = nullptr) override {
        update_fd(EPOLL_CTL_MOD, fd, events, context);
    }

    void del_fd(int fd) override {
        if (epoll_fd_ < 0 || fd < 0) {
            throw std::system_error(EINVAL, std::system_category(),
                                    "invalid fd or epoll not initialized");
        }
        
        // 删除映射
        fd_to_context_.erase(fd);
        
        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev) < 0) {
            if (errno != ENOENT) {
                throw std::system_error(errno, std::system_category(),
                                        "epoll_ctl DEL failed");
            }
        }
    }

    int poll(std::vector<Event>& active_events, int timeout_ms = -1) override {
        if (epoll_fd_ < 0) {
            return -1;
        }

        int num = ::epoll_wait(epoll_fd_, events_.data(),
                               static_cast<int>(max_events_),
                               timeout_ms);
        if (num < 0) {
            if (errno == EINTR) {
                return 0;
            }
            return -1;
        }

        active_events.clear();
        active_events.reserve(static_cast<size_t>(num));
        for (int i = 0; i < num; ++i) {
            Event ev;
            ev.fd = events_[i].data.fd;  // 从 data.fd 获取
            ev.events = from_epoll_events(events_[i].events);
            
            // 从映射表中获取 context
            auto it = fd_to_context_.find(ev.fd);
            if (it != fd_to_context_.end()) {
                ev.context = it->second;
            } else {
                ev.context = nullptr;
            }
            
            active_events.push_back(ev);
        }
        return num;
    }

    size_t max_events() const override {
        return max_events_;
    }

    const char* name() const override {
        return "epoll";
    }

private:
    void update_fd(int op, int fd, EventMask events, void* context) {
        if (epoll_fd_ < 0 || fd < 0) {
            throw std::system_error(EINVAL, std::system_category(),
                                    "invalid fd or epoll not initialized");
        }
        
        // 保存 fd 到 context 的映射
        if (op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD) {
            if (context != nullptr) {
                fd_to_context_[fd] = context;
            } else {
                fd_to_context_.erase(fd);
            }
        }
        
        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = to_epoll_events(events);
        ev.data.fd = fd;  // 只使用 data.fd，不使用 data.ptr
        
        if (::epoll_ctl(epoll_fd_, op, fd, &ev) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "epoll_ctl failed");
        }
    }

private:
    int epoll_fd_;
    size_t max_events_;
    std::vector<struct epoll_event> events_;
    std::unordered_map<int, void*> fd_to_context_;  // fd -> context 映射
};

// 工厂方法实现
std::unique_ptr<Poller> Poller::create_default() {
    return std::make_unique<EpollPoller>();
}

} // namespace httpserver::net