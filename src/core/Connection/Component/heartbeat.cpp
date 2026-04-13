#include <chrono>
#include <atomic>

namespace httpserver::core {

class Heartbeat {
public:
    Heartbeat() : last_beat_(std::chrono::steady_clock::now()) {}

    void Update() {
        last_beat_ = std::chrono::steady_clock::now();
    }

    bool IsExpired(std::chrono::milliseconds timeout) const {
        auto now = std::chrono::steady_clock::now();
        return (now - last_beat_) > timeout;
    }

private:
    std::chrono::steady_clock::time_point last_beat_;
};

} // namespace