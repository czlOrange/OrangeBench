// src/core/tcp_connections/Async/async_scheduler_impl.cpp
#include "httpserver/core/tcp_connections/Async/async_scheduler_impl.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <functional>

namespace httpserver::core::async {

// 定时任务结构（使用 shared_ptr 保存函数副本，支持周期任务）
struct TimerTask {
    std::shared_ptr<std::function<void()>> func;
    std::chrono::steady_clock::time_point expire;
    std::chrono::milliseconds interval;
    size_t id;

    bool operator<(const TimerTask& other) const {
        return expire > other.expire; // 最小堆
    }
};

struct ThreadPoolScheduler::Impl {
    // 工作线程
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable task_cv;

    // 定时器线程
    std::thread timer_thread;
    std::priority_queue<TimerTask> timer_queue;
    std::mutex timer_mutex;
    std::condition_variable timer_cv;
    std::atomic<bool> timer_running{false};

    // 状态标志
    std::atomic<bool> stop{false};
    std::atomic<bool> paused{false};

    // 统计
    std::atomic<size_t> active_tasks{0};
    std::atomic<size_t> completed_tasks{0};
    std::atomic<size_t> failed_tasks{0};
    std::atomic<size_t> next_timer_id{0};

    // 配置
    size_t max_concurrent{0};
    size_t worker_threads{0};
};

// 获取默认线程数
static size_t get_default_thread_count() {
    size_t count = std::thread::hardware_concurrency();
    return count > 0 ? count : 4;
}

ThreadPoolScheduler::ThreadPoolScheduler(size_t thread_count)
    : impl_(std::make_unique<Impl>()) {
    if (thread_count == 0) thread_count = get_default_thread_count();
    impl_->worker_threads = thread_count;
    impl_->max_concurrent = thread_count;
}

ThreadPoolScheduler::~ThreadPoolScheduler() {
    Stop();
}

void ThreadPoolScheduler::Start() {
    if (!impl_->workers.empty()) return;

    impl_->stop = false;
    impl_->paused = false;
    impl_->timer_running = true;

    // 启动工作线程
    for (size_t i = 0; i < impl_->worker_threads; ++i) {
        impl_->workers.emplace_back(&ThreadPoolScheduler::workerLoop, this);
    }

    // 启动定时器线程
    impl_->timer_thread = std::thread(&ThreadPoolScheduler::timerLoop, this);
}

void ThreadPoolScheduler::Stop() {
    // 停止工作线程
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->stop = true;
        impl_->paused = false;
        impl_->task_cv.notify_all();
    }

    for (auto& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
    impl_->workers.clear();

    // 停止定时器线程
    {
        std::lock_guard<std::mutex> lock(impl_->timer_mutex);
        impl_->timer_running = false;
        impl_->timer_cv.notify_one();
    }
    if (impl_->timer_thread.joinable()) impl_->timer_thread.join();

    // 清空队列
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        while (!impl_->tasks.empty()) impl_->tasks.pop();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->timer_mutex);
        while (!impl_->timer_queue.empty()) impl_->timer_queue.pop();
    }

    // 重置统计
    impl_->active_tasks = 0;
    impl_->completed_tasks = 0;
    impl_->failed_tasks = 0;
}

void ThreadPoolScheduler::Pause() {
    impl_->paused = true;
}

void ThreadPoolScheduler::Resume() {
    impl_->paused = false;
    impl_->task_cv.notify_all();
}

void ThreadPoolScheduler::ScheduleBatch(std::vector<std::function<void()>> tasks) {
    for (auto& task : tasks) {
        ScheduleTask(std::move(task));
    }
}

void ThreadPoolScheduler::ScheduleAfter(std::function<void()> task,
                                        std::chrono::milliseconds delay) {
    if (delay.count() <= 0) {
        ScheduleTask(std::move(task));
        return;
    }

    auto now = std::chrono::steady_clock::now();
    TimerTask timer_task;
    timer_task.func = std::make_shared<std::function<void()>>(std::move(task));
    timer_task.expire = now + delay;
    timer_task.interval = std::chrono::milliseconds(0);
    timer_task.id = ++impl_->next_timer_id;

    {
        std::lock_guard<std::mutex> lock(impl_->timer_mutex);
        impl_->timer_queue.push(std::move(timer_task));
    }
    impl_->timer_cv.notify_one();
}

void ThreadPoolScheduler::ScheduleEvery(std::function<void()> task,
                                        std::chrono::milliseconds interval) {
    if (interval.count() <= 0) {
        ScheduleTask(std::move(task));
        return;
    }

    auto now = std::chrono::steady_clock::now();
    TimerTask timer_task;
    timer_task.func = std::make_shared<std::function<void()>>(std::move(task));
    timer_task.expire = now + interval;
    timer_task.interval = interval;
    timer_task.id = ++impl_->next_timer_id;

    {
        std::lock_guard<std::mutex> lock(impl_->timer_mutex);
        impl_->timer_queue.push(std::move(timer_task));
    }
    impl_->timer_cv.notify_one();
}

SchedulerStats ThreadPoolScheduler::GetStats() const {
    SchedulerStats stats;
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        stats.pending_tasks = impl_->tasks.size();
    }
    stats.running_tasks = impl_->active_tasks.load();
    stats.completed_tasks = impl_->completed_tasks.load();
    stats.failed_tasks = impl_->failed_tasks.load();
    stats.worker_threads = impl_->workers.size();
    return stats;
}

void ThreadPoolScheduler::SetMaxConcurrent(size_t max) {
    impl_->max_concurrent = max;
    impl_->task_cv.notify_all();
}

void ThreadPoolScheduler::SetThreadPoolSize(size_t size) {
    if (size == impl_->worker_threads) return;
    if (size > impl_->worker_threads) {
        size_t to_add = size - impl_->worker_threads;
        for (size_t i = 0; i < to_add; ++i) {
            impl_->workers.emplace_back(&ThreadPoolScheduler::workerLoop, this);
        }
        impl_->worker_threads = size;
    } else {
        impl_->worker_threads = size;
    }
}

void ThreadPoolScheduler::ScheduleTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->tasks.push(std::move(task));
    }
    impl_->task_cv.notify_one();
}

void ThreadPoolScheduler::workerLoop() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(impl_->queue_mutex);
            impl_->task_cv.wait(lock, [this] {
                return impl_->stop ||
                       (!impl_->tasks.empty() && !impl_->paused &&
                        (impl_->max_concurrent == 0 || impl_->active_tasks < impl_->max_concurrent));
            });

            if (impl_->stop && impl_->tasks.empty()) break;
            if (impl_->paused || impl_->tasks.empty()) continue;

            if (impl_->max_concurrent > 0 && impl_->active_tasks >= impl_->max_concurrent) {
                continue;
            }

            task = std::move(impl_->tasks.front());
            impl_->tasks.pop();
            ++impl_->active_tasks;
        }

        try {
            task();
            ++impl_->completed_tasks;
        } catch (...) {
            ++impl_->failed_tasks;
        }

        --impl_->active_tasks;
        impl_->task_cv.notify_one();
    }
}

void ThreadPoolScheduler::timerLoop() {
    while (impl_->timer_running) {
        std::unique_lock<std::mutex> lock(impl_->timer_mutex);

        if (impl_->timer_queue.empty()) {
            impl_->timer_cv.wait(lock, [this] {
                return !impl_->timer_running || !impl_->timer_queue.empty();
            });
            if (!impl_->timer_running) break;
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        auto& top = const_cast<TimerTask&>(impl_->timer_queue.top());

        if (top.expire <= now) {
            // 任务到期
            auto func = top.func;
            auto interval = top.interval;
            impl_->timer_queue.pop();

            lock.unlock();

            // 提交到线程池执行
            if (func && *func) {
                ScheduleTask(*func);
            }

            // 周期性任务：重新调度
            if (interval.count() > 0 && func && *func) {
                lock.lock();
                TimerTask new_task;
                new_task.func = func;
                new_task.expire = now + interval;
                new_task.interval = interval;
                new_task.id = ++impl_->next_timer_id;
                impl_->timer_queue.push(std::move(new_task));
                lock.unlock();
                impl_->timer_cv.notify_one();
            }
        } else {
            auto wait_time = top.expire - now;
            impl_->timer_cv.wait_for(lock, wait_time, [this] {
                return !impl_->timer_running || !impl_->timer_queue.empty();
            });
        }
    }
}

} // namespace httpserver::core::async