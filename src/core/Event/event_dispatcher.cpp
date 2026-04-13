// src/core/Event/event_dispatcher.cpp
#include "core/Event/event_dispatcher.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <unordered_map>
#include <functional>
#include <string>
#include <memory>
#include <condition_variable>

namespace httpserver::core {

// 带事件类型的回调结构
struct TypedEventCallback {
    ConnectionEvent event;
    EventCallback callback;
};

class DefaultEventDispatcher : public IEventDispatcher {
public:
    DefaultEventDispatcher(size_t worker_threads = 4)
        : stop_(false)
        , max_queue_size_(1000)
        , async_dispatch_(false)
        , total_events_(0)
        , total_data_(0)
        , total_errors_(0)
        , active_subscribers_(0)
        , stats_{0, 0, 0, 0, {}} {  // ✅ 初始化 stats_
        startWorkerThreads(worker_threads);
    }

    ~DefaultEventDispatcher() override {
        stop_ = true;
        queue_cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    void PublishEvent(ConnectionEvent event, std::shared_ptr<IConnection> conn) override {
        if (!applyFilters(event, conn)) return;

        total_events_++;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.event_counts[event]++;
            stats_.total_events = total_events_;
            stats_.total_data = total_data_;
            stats_.total_errors = total_errors_;
            stats_.active_subscribers = active_subscribers_;
        }

        auto task = [this, event, conn]() {
            notifySubscribers([&](auto& subscriber) {
                subscriber->OnEvent(event, conn);
            });
            notifyTypedCallbacks(event, conn);
        };
        dispatchTask(std::move(task));
    }

    void PublishData(std::shared_ptr<IConnection> conn, std::string_view data) override {
        total_data_++;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.total_events = total_events_;
            stats_.total_data = total_data_;
            stats_.total_errors = total_errors_;
            stats_.active_subscribers = active_subscribers_;
        }

        auto task = [this, conn, data = std::string(data)]() {
            notifySubscribers([&](auto& subscriber) {
                subscriber->OnData(conn, data);
            });
            notifyCallbacks(data_callbacks_, [conn, &data](auto& cb) {
                cb(conn, data);
            });
        };
        dispatchTask(std::move(task));
    }

    void PublishError(std::shared_ptr<IConnection> conn, std::error_code ec) override {
        total_errors_++;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.total_events = total_events_;
            stats_.total_data = total_data_;
            stats_.total_errors = total_errors_;
            stats_.active_subscribers = active_subscribers_;
        }

        auto task = [this, conn, ec]() {
            notifySubscribers([&](auto& subscriber) {
                subscriber->OnError(conn, ec);
            });
            notifyCallbacks(error_callbacks_, [conn, ec](auto& cb) {
                cb(conn, ec);
            });
        };
        dispatchTask(std::move(task));
    }

    void SubscribeEvent(ConnectionEvent event, EventCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        typed_callbacks_.push_back({event, std::move(callback)});
    }

    void SubscribeData(DataCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        data_callbacks_.push_back(std::move(callback));
    }

    void SubscribeError(ErrorCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        error_callbacks_.push_back(std::move(callback));
    }

    void RegisterSubscriber(std::shared_ptr<IEventSubscriber> subscriber) override {
        if (!subscriber) return;
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_[subscriber->GetSubscriberId()] = std::move(subscriber);
        active_subscribers_ = subscribers_.size();
        stats_.active_subscribers = active_subscribers_;
    }

    void UnregisterSubscriber(const std::string& subscriber_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(subscriber_id);
        active_subscribers_ = subscribers_.size();
        stats_.active_subscribers = active_subscribers_;
    }

    void UnregisterAllSubscribers() override {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.clear();
        active_subscribers_ = 0;
        stats_.active_subscribers = 0;
    }

    void AddEventFilter(EventFilter filter) override {
        std::lock_guard<std::mutex> lock(mutex_);
        filters_.push_back(std::move(filter));
    }

    void RemoveEventFilter(size_t index) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < filters_.size()) {
            filters_.erase(filters_.begin() + index);
        }
    }

    void RouteEvent(ConnectionEvent event, std::shared_ptr<IConnection> conn,
                    const std::string& route_key) override {
        if (!applyFilters(event, conn)) return;

        std::shared_ptr<IEventSubscriber> target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscribers_.find(route_key);
            if (it != subscribers_.end()) target = it->second;
        }

        auto task = [this, event, conn, target]() {
            if (target) target->OnEvent(event, conn);
            notifyTypedCallbacks(event, conn);
        };
        dispatchTask(std::move(task));
    }

    DispatchStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void SetMaxQueueSize(size_t size) override {
        max_queue_size_ = size;
    }

    void SetAsyncDispatch(bool enable) override {
        async_dispatch_ = enable;
    }

private:
    void startWorkerThreads(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    void workerLoop() {
        while (!stop_) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] {
                    return !task_queue_.empty() || stop_;
                });
                if (stop_ && task_queue_.empty()) break;
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
            if (task) task();
        }
    }

    void dispatchTask(std::function<void()> task) {
        if (async_dispatch_) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (max_queue_size_ > 0 && task_queue_.size() >= max_queue_size_) {
                    return; // 队列满，丢弃任务
                }
                task_queue_.push(std::move(task));
            }
            queue_cv_.notify_one();
        } else {
            task();
        }
    }

    template<typename Func>
    void notifySubscribers(Func&& func) {
        std::vector<std::shared_ptr<IEventSubscriber>> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.reserve(subscribers_.size());
            for (auto& [id, sub] : subscribers_) {
                if (sub) snapshot.push_back(sub);
            }
        }
        for (auto& sub : snapshot) {
            if (sub) func(sub);
        }
    }

    template<typename CallbackList, typename Func>
    void notifyCallbacks(const CallbackList& list, Func&& func) {
        CallbackList snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = list;
        }
        for (auto& cb : snapshot) {
            if (cb) func(cb);
        }
    }

    void notifyTypedCallbacks(ConnectionEvent event, std::shared_ptr<IConnection> conn) {
        std::vector<EventCallback> matching;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& tc : typed_callbacks_) {
                if (tc.event == event && tc.callback) {
                    matching.push_back(tc.callback);
                }
            }
        }
        for (auto& cb : matching) {
            if (cb) cb(event, conn);
        }
    }

    bool applyFilters(ConnectionEvent event, std::shared_ptr<IConnection> conn) {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 直接加锁，不拷贝
        for (const auto& filter : filters_) {
            if (filter && !filter(event, conn)) return false;
        }
        return true;
    }

    // 线程池相关
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> stop_;
    size_t max_queue_size_;
    bool async_dispatch_;

    // 统计信息
    std::atomic<size_t> total_events_;
    std::atomic<size_t> total_data_;
    std::atomic<size_t> total_errors_;
    std::atomic<size_t> active_subscribers_;
    mutable std::mutex mutex_;
    DispatchStats stats_;  // ✅ 在构造函数中已初始化

    // 订阅者管理
    std::unordered_map<std::string, std::shared_ptr<IEventSubscriber>> subscribers_;
    std::vector<TypedEventCallback> typed_callbacks_;
    std::vector<DataCallback> data_callbacks_;
    std::vector<ErrorCallback> error_callbacks_;
    std::vector<EventFilter> filters_;
};

std::shared_ptr<IEventDispatcher> IEventDispatcher::CreateDefault() {
    return std::make_shared<DefaultEventDispatcher>();
}

} // namespace httpserver::core