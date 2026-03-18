#include "scheduler.hpp"
#include <atomic>
#include <queue>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <iostream>
#include <sstream>

namespace httpserver::coroutine {

// ============================================================================
// TimerAwaiter 实现
// ============================================================================

// 1.构造函数实现（补全初始化列表）
TimerAwaiter::TimerAwaiter(std::chrono::milliseconds duration)
    : duration_(duration),                              // 初始化延时时长
      start_time_(std::chrono::steady_clock::now()) {}  // 初始化启动时间
// 2.实现 await_ready：判断是否已经等待够时长
bool TimerAwaiter::await_ready() {
    auto now = std::chrono::steady_clock::now();
    return (now - start_time_) >= duration_;
}
// 3.实现 await_suspend：挂起协程，启动定时器
void TimerAwaiter::await_suspend(std::coroutine_handle<> handle) {
    // 1. 定义std::function，匹配lambda的签名：无参数、无返回值
    std::function<void()> timer_task = [handle, duration = duration_]() {
        std::this_thread::sleep_for(duration);
        if (!handle.done()) {
            handle.resume();
        }
    };

    // 2. 用包装后的function创建线程
    std::thread(timer_task).detach();
}
// 4.实现 await_resume：恢复协程执行
void TimerAwaiter::await_resume() {
    // 恢复执行，不需要特殊处理
}

// ============================================================================
// NetworkAwaiter 实现
// ============================================================================


template<typename Result>//用外部的异步操作,初始化成员变量async_operation_
NetworkAwaiter<Result>::NetworkAwaiter(std::function<void(Callback)> async_operation)
    : async_operation_(std::move(async_operation)) {}



template<typename Result>
bool NetworkAwaiter<Result>::await_ready() const noexcept {
    // 异步操作总是未就绪，需要挂起
    return false;
}


template<typename Result>//协程挂起后,本线程马上要做的事情，启动异步操作。
void NetworkAwaiter<Result>::await_suspend(std::coroutine_handle<> handle) {
    // 存储协程句柄
    handle_ = handle;
    
    // 启动异步操作
    async_operation_([this](Result result) {
        // 异步操作完成，存储结果并恢复协程
        result_ = std::move(result);// 保存结果
        
        if (!handle_.done()) {      // 检查协程是否已完成
            handle_.resume();       // 恢复协程执行
        }
    });
}


template<typename Result>//恢复协程执行后,本线程马上要做的事情,获取异步操作结果。
Result NetworkAwaiter<Result>::await_resume() {
    return std::move(result_.value());
}8

// ============================================================================
// Task 的实现
// ============================================================================

// Task<T> 的实现
template<typename T>//移动构造函数
Task<T>::Task(Task&& other) noexcept 
    : handle_(std::exchange(other.handle_, nullptr)) {}


template<typename T>//移动赋值运算符
Task<T>& Task<T>::operator=(Task&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            handle_.destroy();
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

template<typename T>
Task<T>::~Task() {
    if (handle_) {
        handle_.destroy();
    }
}

template<typename T>
bool Task<T>::IsReady() const {
    return !handle_ || handle_.done();
}

template<typename T>
T Task<T>::Wait() {
    if (!handle_) {
        throw std::runtime_error("Task is empty");
    }
    
    // 简单实现：轮询等待
    while (!handle_.done()) {
        std::this_thread::yield();
    }
    
    // 获取结果
    if (handle_.promise().exception) {
        std::rethrow_exception(handle_.promise().exception);
    }
    
    return *handle_.promise().value;
}

template<typename T>
std::future<T> Task<T>::AsFuture() {
    return std::async(std::launch::async, [this]() {
        return Wait();
    });
}

template<typename T>
bool Task<T>::Cancel() {
    if (!handle_ || handle_.done()) {
        return false;
    }
    
    handle_.destroy();
    handle_ = nullptr;
    return true;
}

template<typename T>
std::expected<T, std::error_code> Task<T>::TryGet() {
    if (!handle_) {
        return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    }
    
    if (!handle_.done()) {
        return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
    }
    
    if (handle_.promise().exception) {
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }
    
    return *handle_.promise().value;
}

// Task<T> promise_type 方法实现
template<typename T>
Task<T> Task<T>::promise_type::get_return_object() {
    return Task<T>{std::coroutine_handle<promise_type>::from_promise(*this)};
}

template<typename T>
std::suspend_always Task<T>::promise_type::initial_suspend() {
    return {};
}

template<typename T>
std::suspend_always Task<T>::promise_type::final_suspend() noexcept {
    return {};
}

template<typename T>
void Task<T>::promise_type::return_value(T value) {
    this->value = std::make_shared<T>(std::move(value));
}

template<typename T>
void Task<T>::promise_type::unhandled_exception() {
    exception = std::current_exception();
}

// ============================================================================
// Task<void> 的特化实现
// ============================================================================

Task<void> Task<void>::promise_type::get_return_object() {
    return Task<void>{std::coroutine_handle<promise_type>::from_promise(*this)};
}

std::suspend_always Task<void>::promise_type::initial_suspend() {
    return {};
}

std::suspend_always Task<void>::promise_type::final_suspend() noexcept {
    return {};
}

void Task<void>::promise_type::return_void() {
    // void 类型没有返回值
}

void Task<void>::promise_type::unhandled_exception() {
    exception = std::current_exception();
}

// Task<void> 方法实现
Task<void>::Task(Task&& other) noexcept 
    : handle_(std::exchange(other.handle_, nullptr)) {}

Task<void>& Task<void>::operator=(Task&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            handle_.destroy();
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

Task<void>::~Task() {
    if (handle_) {
        handle_.destroy();
    }
}

bool Task<void>::IsReady() const {
    return !handle_ || handle_.done();
}

void Task<void>::Wait() {
    if (!handle_) {
        throw std::runtime_error("Task is empty");
    }
    
    while (!handle_.done()) {
        std::this_thread::yield();
    }
    
    if (handle_.promise().exception) {
        std::rethrow_exception(handle_.promise().exception);
    }
}

// ============================================================================
// Scheduler 实现类
// ============================================================================

class SchedulerImpl : public IScheduler {
private:
    struct ScheduledTask {
        std::coroutine_handle<> handle;
        int priority;
        std::chrono::steady_clock::time_point scheduled_time;
        
        bool operator<(const ScheduledTask& other) const {
            if (priority != other.priority) {
                return priority < other.priority; // 优先级数字越小，优先级越高
            }
            return scheduled_time > other.scheduled_time; // 时间早的优先
        }
    };
    
    struct WorkerThread {
        std::thread thread;
        std::deque<ScheduledTask> local_queue;
        std::mutex queue_mutex;
        std::condition_variable cv;
        bool running = true;
        std::atomic<uint64_t> processed_count{0};
    };

public:
    SchedulerImpl() : next_coroutine_id_(0) {}
    
    ~SchedulerImpl() override {
        Shutdown();
    }
    
    bool Initialize(const SchedulerConfig& config) override {
        config_ = config;
        stats_ = SchedulerStats{};
        
        try {
            // 创建工作线程
            workers_.resize(config.worker_threads);
            for (size_t i = 0; i < config.worker_threads; ++i) {
                workers_[i] = std::make_unique<WorkerThread>();
                workers_[i]->thread = std::thread([this, i]() {
                    WorkerLoop(i);
                });
            }
            
            // 启动监控线程
            if (config.enable_stats) {
                monitor_thread_ = std::thread([this]() {
                    MonitorLoop();
                });
            }
            
            initialized_ = true;
            return true;
        } catch (...) {
            Shutdown();
            return false;
        }
    }
    
    void Shutdown() noexcept override {
        if (!initialized_) return;
        
        initialized_ = false;
        
        // 停止工作线程
        for (auto& worker : workers_) {
            if (worker) {
                {
                    std::lock_guard<std::mutex> lock(worker->queue_mutex);
                    worker->running = false;
                }
                worker->cv.notify_all();
            }
        }
        
        // 等待线程结束
        for (auto& worker : workers_) {
            if (worker && worker->thread.joinable()) {
                worker->thread.join();
            }
        }
        
        // 停止监控线程
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        
        // 销毁所有未完成的协程
        {
            std::lock_guard<std::mutex> lock(active_coroutines_mutex_);
            for (auto& [id, handle] : active_coroutines_) {
                handle.destroy();
            }
            active_coroutines_.clear();
        }
        
        workers_.clear();
    }
    
    // 协程调度
    template<typename Func>
    Task<> Schedule(Func func) {
        return ScheduleInternal([func]() mutable {
            func();
        });
    }
    
    template<typename Func, typename... Args>
    Task<> Schedule(Func func, Args&&... args) {
        return ScheduleInternal([func, args...]() mutable {
            std::invoke(func, args...);
        });
    }
    
    // 批量调度
    template<typename Func>
    std::vector<Task<>> ScheduleBatch(size_t count, Func func) {
        std::vector<Task<>> tasks;
        tasks.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            tasks.push_back(Schedule(func));
        }
        
        return tasks;
    }
    
    // 定时调度
    template<typename Func>
    Task<> ScheduleAfter(std::chrono::milliseconds delay, Func func) {
        auto task = Schedule(func);
        
        // 将协程延迟执行
        std::thread([this, delay, handle = task.handle_]() mutable {
            std::this_thread::sleep_for(delay);
            EnqueueCoroutine(handle);
        }).detach();
        
        return task;
    }
    
    template<typename Func>
    Task<> ScheduleAt(std::chrono::steady_clock::time_point time, Func func) {
        auto now = std::chrono::steady_clock::now();
        if (time <= now) {
            return Schedule(func);
        }
        
        return ScheduleAfter(std::chrono::duration_cast<std::chrono::milliseconds>(time - now), func);
    }
    
    // 协程同步原语
    Task<> Yield() override {
        co_await std::suspend_always{};
    }
    
    Task<> Sleep(std::chrono::milliseconds duration) override {
        TimerAwaiter awaiter(duration);
        co_await awaiter;
    }
    
    // 等待多个任务完成
    Task<> WhenAll(std::vector<Task<>> tasks) override {
        struct AllAwaiter {
            std::vector<Task<>> tasks;
            
            bool await_ready() const noexcept {
                return tasks.empty();
            }
            
            void await_suspend(std::coroutine_handle<> handle) {
                auto shared_state = std::make_shared<AllState>();
                shared_state->remaining = tasks.size();
                shared_state->resume_handle = handle;
                
                for (auto& task : tasks) {
                    std::thread([task = std::move(task), shared_state]() mutable {
                        task.Wait();
                        if (--shared_state->remaining == 0) {
                            shared_state->resume_handle.resume();
                        }
                    }).detach();
                }
            }
            
            void await_resume() {}
            
        private:
            struct AllState {
                std::atomic<size_t> remaining;
                std::coroutine_handle<> resume_handle;
            };
        };
        
        co_await AllAwaiter{std::move(tasks)};
    }
    
    template<typename T>
    Task<std::vector<T>> WhenAll(std::vector<Task<T>> tasks) {
        struct AllAwaiter {
            std::vector<Task<T>> tasks;
            
            bool await_ready() const noexcept {
                return tasks.empty();
            }
            
            void await_suspend(std::coroutine_handle<> handle) {
                auto shared_state = std::make_shared<AllState<T>>();
                shared_state->results.resize(tasks.size());
                shared_state->remaining = tasks.size();
                shared_state->resume_handle = handle;
                
                for (size_t i = 0; i < tasks.size(); ++i) {
                    std::thread([i, task = std::move(tasks[i]), shared_state]() mutable {
                        try {
                            shared_state->results[i] = task.Wait();
                        } catch (...) {
                            // 处理异常
                        }
                        if (--shared_state->remaining == 0) {
                            shared_state->resume_handle.resume();
                        }
                    }).detach();
                }
            }
            
            std::vector<T> await_resume() {
                return std::move(shared_state_->results);
            }
            
        private:
            struct AllState {
                std::vector<T> results;
                std::atomic<size_t> remaining;
                std::coroutine_handle<> resume_handle;
            };
            std::shared_ptr<AllState> shared_state_;
        };
        
        co_return co_await AllAwaiter{std::move(tasks)};
    }
    
    Task<> WhenAny(std::vector<Task<>> tasks) override {
        struct AnyAwaiter {
            std::vector<Task<>> tasks;
            
            bool await_ready() const noexcept {
                return tasks.empty();
            }
            
            void await_suspend(std::coroutine_handle<> handle) {
                auto shared_state = std::make_shared<AnyState>();
                shared_state->completed = false;
                shared_state->resume_handle = handle;
                
                for (auto& task : tasks) {
                    std::thread([task = std::move(task), shared_state]() mutable {
                        task.Wait();
                        if (!shared_state->completed.exchange(true)) {
                            shared_state->resume_handle.resume();
                        }
                    }).detach();
                }
            }
            
            void await_resume() {}
            
        private:
            struct AnyState {
                std::atomic<bool> completed;
                std::coroutine_handle<> resume_handle;
            };
        };
        
        co_await AnyAwaiter{std::move(tasks)};
    }
    
    // IO操作集成
    template<typename T>
    Task<T> AsyncIO(std::function<void(std::function<void(std::expected<T, std::error_code>)>)> io_operation) {
        struct IOAwaiter {
            std::function<void(std::function<void(std::expected<T, std::error_code>)>)> io_operation;
            std::optional<std::expected<T, std::error_code>> result;
            
            bool await_ready() const noexcept { return false; }
            
            void await_suspend(std::coroutine_handle<> handle) {
                io_operation([this, handle](std::expected<T, std::error_code> res) mutable {
                    result = std::move(res);
                    if (!handle.done()) {
                        handle.resume();
                    }
                });
            }
            
            T await_resume() {
                if (result->has_value()) {
                    return std::move(**result);
                } else {
                    throw std::system_error(result->error());
                }
            }
        };
        
        co_return co_await IOAwaiter{std::move(io_operation)};
    }
    
    // 连接协程适配器
    template<typename ConnectionType>
    class ConnectionAdapter {
    public:
        explicit ConnectionAdapter(IScheduler& scheduler) : scheduler_(scheduler) {}
        
        Task<ConnectionType> Connect(std::function<void(std::function<void(std::expected<ConnectionType, std::error_code>)>)> connect_operation) {
            return scheduler_.AsyncIO<ConnectionType>(std::move(connect_operation));
        }
        
        template<typename T>
        Task<T> Execute(ConnectionType& conn, std::function<void(ConnectionType&, std::function<void(std::expected<T, std::error_code>)>)> operation) {
            return scheduler_.AsyncIO<T>([&conn, operation = std::move(operation)](auto callback) {
                operation(conn, std::move(callback));
            });
        }
        
    private:
        IScheduler& scheduler_;
    };
    
    // 监控和管理
    SchedulerStats GetStats() const override {
        return stats_.load();
    }
    
    size_t GetPendingTasks() const override {
        size_t total = 0;
        for (const auto& worker : workers_) {
            std::lock_guard<std::mutex> lock(worker->queue_mutex);
            total += worker->local_queue.size();
        }
        return total;
    }
    
    size_t GetRunningTasks() const override {
        std::lock_guard<std::mutex> lock(active_coroutines_mutex_);
        return active_coroutines_.size();
    }
    
    void SetMaxConcurrency(size_t max) override {
        max_concurrency_ = max;
    }
    
    size_t GetMaxConcurrency() const override {
        return max_concurrency_;
    }
    
    // 错误处理
    void SetUnhandledExceptionHandler(
        std::function<void(std::exception_ptr)> handler) override {
        exception_handler_ = std::move(handler);
    }
    
    // 调试支持
    void EnableDebug(bool enable) override {
        debug_enabled_ = enable;
    }
    
    std::vector<size_t> GetActiveCoroutineIds() const override {
        std::lock_guard<std::mutex> lock(active_coroutines_mutex_);
        std::vector<size_t> ids;
        ids.reserve(active_coroutines_.size());
        for (const auto& [id, _] : active_coroutines_) {
            ids.push_back(id);
        }
        return ids;
    }

protected:
    void EnqueueCoroutine(std::coroutine_handle<> handle, int priority = 0) override {
        ScheduledTask task{
            .handle = handle,
            .priority = priority,
            .scheduled_time = std::chrono::steady_clock::now()
        };
        
        // 选择工作线程（简单的轮询）
        static size_t next_worker = 0;
        size_t worker_idx = next_worker++ % workers_.size();
        
        auto& worker = workers_[worker_idx];
        {
            std::lock_guard<std::mutex> lock(worker->queue_mutex);
            worker->local_queue.push_back(std::move(task));
        }
        worker->cv.notify_one();
    }
    
    void DequeueCoroutine(std::coroutine_handle<> handle) override {
        // 在实际实现中，可能需要从队列中移除特定的协程
        // 这里简化处理，只是标记协程完成
        handle.destroy();
    }

private:
    template<typename Func>
    Task<> ScheduleInternal(Func func) {
        // 创建协程
        auto task = [func = std::move(func)]() -> Task<> {
            try {
                func();
            } catch (...) {
                // 异常处理
            }
            co_return;
        }();
        
        // 注册协程
        size_t id = ++next_coroutine_id_;
        {
            std::lock_guard<std::mutex> lock(active_coroutines_mutex_);
            active_coroutines_[id] = task.handle_;
        }
        
        stats_.total_coroutines.fetch_add(1, std::memory_order_relaxed);
        stats_.active_coroutines.fetch_add(1, std::memory_order_relaxed);
        
        // 入队调度
        EnqueueCoroutine(task.handle_);
        
        return task;
    }
    
    void WorkerLoop(size_t worker_id) {
        auto& worker = *workers_[worker_id];
        
        while (worker.running) {
            ScheduledTask task;
            bool has_task = false;
            
            // 从本地队列获取任务
            {
                std::unique_lock<std::mutex> lock(worker.queue_mutex);
                if (worker.cv.wait_for(lock, config_.idle_timeout, 
                    [&worker]() { return !worker.running || !worker.local_queue.empty(); })) {
                    if (!worker.local_queue.empty()) {
                        task = std::move(worker.local_queue.front());
                        worker.local_queue.pop_front();
                        has_task = true;
                    }
                }
            }
            
            // 如果本地队列为空，尝试工作窃取
            if (!has_task && config_.enable_work_stealing) {
                has_task = TryStealWork(worker_id, task);
            }
            
            if (has_task) {
                ExecuteTask(task, worker_id);
            }
        }
    }
    
    bool TryStealWork(size_t thief_id, ScheduledTask& task) {
        // 简单的随机窃取策略
        for (size_t i = 0; i < workers_.size(); ++i) {
            if (i == thief_id) continue;
            
            auto& victim = *workers_[i];
            std::lock_guard<std::mutex> lock(victim.queue_mutex);
            if (!victim.local_queue.empty()) {
                task = std::move(victim.local_queue.back());
                victim.local_queue.pop_back();
                return true;
            }
        }
        return false;
    }
    
    void ExecuteTask(const ScheduledTask& task, size_t worker_id) {
        ++stats_.context_switches;
        
        try {
            if (!task.handle.done()) {
                auto start = std::chrono::steady_clock::now();
                task.handle.resume();
                auto end = std::chrono::steady_clock::now();
                
                auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
                UpdateWaitTimeStats(duration.count());
            }
            
            if (task.handle.done()) {
                // 协程完成
                size_t id = 0;
                {
                    std::lock_guard<std::mutex> lock(active_coroutines_mutex_);
                    // 查找并移除协程
                    for (auto it = active_coroutines_.begin(); it != active_coroutines_.end(); ++it) {
                        if (it->second == task.handle) {
                            id = it->first;
                            active_coroutines_.erase(it);
                            break;
                        }
                    }
                }
                
                stats_.active_coroutines.fetch_sub(1, std::memory_order_relaxed);
                stats_.completed_coroutines.fetch_add(1, std::memory_order_relaxed);
                
                workers_[worker_id]->processed_count.fetch_add(1, std::memory_order_relaxed);
            }
        } catch (...) {
            stats_.failed_coroutines.fetch_add(1, std::memory_order_relaxed);
            if (exception_handler_) {
                exception_handler_(std::current_exception());
            }
        }
    }
    
    void UpdateWaitTimeStats(uint64_t wait_time_ns) {
        auto old_avg = stats_.avg_wait_time_ns.load(std::memory_order_relaxed);
        uint64_t new_avg = old_avg + (wait_time_ns - old_avg) / stats_.context_swights.load(std::memory_order_relaxed);
        stats_.avg_wait_time_ns.store(new_avg, std::memory_order_relaxed);
        
        uint64_t old_max = stats_.max_wait_time_ns.load(std::memory_order_relaxed);
        while (wait_time_ns > old_max) {
            if (stats_.max_wait_time_ns.compare_exchange_weak(old_max, wait_time_ns)) {
                break;
            }
        }
    }
    
    void MonitorLoop() {
        while (initialized_) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.stats_interval_ms));
            
            if (!initialized_) break;
            
            // 计算吞吐量
            uint64_t total_processed = 0;
            for (const auto& worker : workers_) {
                total_processed += worker->processed_count.load(std::memory_order_relaxed);
            }
            
            static uint64_t last_total = 0;
            uint64_t delta = total_processed - last_total;
            last_total = total_processed;
            
            stats_.throughput_per_sec.store(
                delta * 1000 / config_.stats_interval_ms,
                std::memory_order_relaxed
            );
            
            if (debug_enabled_) {
                PrintDebugInfo();
            }
        }
    }
    
    void PrintDebugInfo() {
        std::stringstream ss;
        ss << "Scheduler Stats:\n";
        ss << "  Active coroutines: " << stats_.active_coroutines.load() << "\n";
        ss << "  Total coroutines: " << stats_.total_coroutines.load() << "\n";
        ss << "  Completed: " << stats_.completed_coroutines.load() << "\n";
        ss << "  Failed: " << stats_.failed_coroutines.load() << "\n";
        ss << "  Context switches: " << stats_.context_switches.load() << "\n";
        ss << "  Throughput: " << stats_.throughput_per_sec.load() << "/s\n";
        
        for (size_t i = 0; i < workers_.size(); ++i) {
            std::lock_guard<std::mutex> lock(workers_[i]->queue_mutex);
            ss << "  Worker " << i << ": queue=" << workers_[i]->local_queue.size()
               << ", processed=" << workers_[i]->processed_count.load() << "\n";
        }
        
        std::cout << ss.str() << std::endl;
    }

private:
    SchedulerConfig config_;
    std::atomic<SchedulerStats> stats_;
    std::atomic<bool> initialized_{false};
    
    std::vector<std::unique_ptr<WorkerThread>> workers_;
    std::thread monitor_thread_;
    
    std::atomic<size_t> next_coroutine_id_;
    std::unordered_map<size_t, std::coroutine_handle<>> active_coroutines_;
    mutable std::mutex active_coroutines_mutex_;
    
    std::atomic<size_t> max_concurrency_{0};
    std::function<void(std::exception_ptr)> exception_handler_;
    std::atomic<bool> debug_enabled_{false};
};

// 工厂函数创建调度器实例
std::unique_ptr<IScheduler> CreateScheduler() {
    return std::make_unique<SchedulerImpl>();
}

} // namespace httpserver::coroutine