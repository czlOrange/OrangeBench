#include <chrono>
#include <atomic>

namespace httpserver::core {

class Statistics {
public:
    Statistics() 
        : bytes_sent_(0), bytes_received_(0), total_ops_(0) {
        auto now = std::chrono::steady_clock::now();
        connect_time_ = now;
        last_activity_ = now;
    }

    void RecordSent(size_t bytes) {
        bytes_sent_ += bytes;
        total_ops_++;
    }

    void RecordReceived(size_t bytes) {
        bytes_received_ += bytes;
        total_ops_++;
    }

    void UpdateActivity() {
        last_activity_ = std::chrono::steady_clock::now();
    }

    void SetConnectTime(std::chrono::steady_clock::time_point t) {
        connect_time_ = t;
    }

    size_t GetBytesSent() const { return bytes_sent_; }
    size_t GetBytesReceived() const { return bytes_received_; }
    size_t GetTotalOperations() const { return total_ops_; }
    auto GetConnectTime() const { return connect_time_; }
    auto GetLastActivityTime() const { return last_activity_; }

private:
    std::atomic<size_t> bytes_sent_;
    std::atomic<size_t> bytes_received_;
    std::atomic<size_t> total_ops_;
    std::chrono::steady_clock::time_point connect_time_;
    std::chrono::steady_clock::time_point last_activity_;
};

} // namespace