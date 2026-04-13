// include/httpserver/core/event_loop.hpp
#pragma once

#include "connection.hpp"
#include "../net/poller.hpp"
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>

namespace httpserver::core {

/**
 * @brief 事件循环类
 * 
 * Reactor模式实现，单线程处理所有I/O事件
 */
class EventLoop {
public:
    using Task = std::function<void()>;
    
    EventLoop();
    ~EventLoop();
    
    // 生命周期
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // 连接管理
    bool add_connection(Connection::Ptr conn);
    bool remove_connection(int fd);
    Connection::Ptr get_connection(int fd);
    
    // 定时器
    using TimerId = uint64_t;
    TimerId run_after(int delay_ms, Task task);
    TimerId run_every(int interval_ms, Task task);
    void cancel_timer(TimerId id);
    
    // 任务队列（线程安全）
    void run_in_loop(Task task);
    void queue_in_loop(Task task);
    
    // 统计
    size_t connection_count() const;
    size_t pending_tasks() const;
    
private:
    void loop();                    // 主循环
    void handle_events();           // 处理网络事件
    void handle_tasks();            // 处理任务队列
    void handle_timers();           // 处理定时器
    void wakeup();                  // 唤醒事件循环
    
private:
    std::unique_ptr<net::Poller> poller_;
    std::unordered_map<int, Connection::Ptr> connections_;
    
    std::atomic<bool> running_{false};
    std::thread loop_thread_;
    
    // 任务队列
    std::mutex task_mutex_;
    std::vector<Task> tasks_;
    
    // 定时器
    struct Timer {
        TimerId id;
        std::chrono::steady_clock::time_point expire_time;
        std::chrono::milliseconds interval{0};
        Task task;
    };
    std::vector<Timer> timers_;
    TimerId next_timer_id_{1};
    
    // 唤醒机制
    int wakeup_fd_{-1};
};

} // namespace httpserver::core