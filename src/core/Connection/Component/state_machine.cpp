#include "connection_state.hpp"
#include <mutex>

namespace httpserver::core {

class StateMachine {
public:
    StateMachine() : state_(ConnectionState::DISCONNECTED) {}

    ConnectionState GetState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    bool SetConnecting() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ConnectionState::DISCONNECTED) {
            state_ = ConnectionState::CONNECTING;
            return true;
        }
        return false;
    }

    bool SetConnected() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ConnectionState::CONNECTING || state_ == ConnectionState::CONNECTED) {
            state_ = ConnectionState::CONNECTED;
            return true;
        }
        return false;
    }

    void SetDisconnected() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ConnectionState::DISCONNECTED;
    }

    void SetError() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ConnectionState::ERROR;
    }

    bool IsConnected() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == ConnectionState::CONNECTED;
    }

private:
    mutable std::mutex mutex_;
    ConnectionState state_;
};

} // namespace