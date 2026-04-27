#include <functional>
#include <memory>
#include <mutex>
#include "core/Event/event_dispatcher.hpp"
#include "connection_interface.hpp"

namespace httpserver::core {

class EventManager {
public:
    using EventCallback = std::function<void(ConnectionEvent, std::shared_ptr<IConnection>, const std::string&)>;
    using DataCallback = std::function<void(std::shared_ptr<IConnection>, std::string_view)>;
    using ErrorCallback = std::function<void(std::shared_ptr<IConnection>, std::error_code)>;

    explicit EventManager(std::shared_ptr<IEventDispatcher> dispatcher) 
        : dispatcher_(std::move(dispatcher)) {}

    void SetEventCallback(EventCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        event_cb_ = std::move(cb);
    }

    void SetDataCallback(DataCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_cb_ = std::move(cb);
    }

    void SetErrorCallback(ErrorCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_cb_ = std::move(cb);
    }

    void NotifyEvent(ConnectionEvent ev, std::shared_ptr<IConnection> conn, const std::string& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (event_cb_) event_cb_(ev, conn, info);
    }

    void NotifyData(std::shared_ptr<IConnection> conn, std::string_view data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_cb_) data_cb_(conn, data);
    }

    void NotifyError(std::shared_ptr<IConnection> conn, std::error_code ec) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_cb_) error_cb_(conn, ec);
    }

    std::shared_ptr<IEventDispatcher> GetDispatcher() const { return dispatcher_; }

    void RegisterRead(int fd, std::function<void()> handler) {
        if (dispatcher_ && fd != -1) {
            dispatcher_->Register(fd, EVENT_READ, [handler](int, EventMask) { handler(); });
        }
    }

    void RegisterWrite(int fd, std::function<void()> handler) {
        if (dispatcher_ && fd != -1) {
            dispatcher_->Register(fd, EVENT_WRITE, [handler](int, EventMask) { handler(); });
        }
    }

    void Unregister(int fd) { 
        if (dispatcher_) dispatcher_->Unregister(fd); 
    }

    void ModifyToReadOnly(int fd) { 
        if (dispatcher_) dispatcher_->Modify(fd, EVENT_READ); 
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<IEventDispatcher> dispatcher_;
    EventCallback event_cb_;
    DataCallback data_cb_;
    ErrorCallback error_cb_;
};

} // namespace