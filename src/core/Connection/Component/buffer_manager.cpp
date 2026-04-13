#include <vector>
#include <deque>
#include <cstddef>
#include <mutex>

namespace httpserver::core {

struct BufferView {
    const char* data;
    size_t size;
};

class SendBufferQueue {
public:
    bool EnqueueSend(BufferView view) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (view.size == 0) return true;
        chunks_.push_back({std::vector<char>(view.data, view.data + view.size), 0});
        total_bytes_ += view.size;
        return true;
    }

    BufferView PeekSend() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (chunks_.empty()) return {nullptr, 0};
        const auto& front = chunks_.front();
        return {front.data.data() + front.offset, front.data.size() - front.offset};
    }

    void ConsumeSend(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bytes == 0 || chunks_.empty()) return;
        auto& front = chunks_.front();
        size_t consumed = std::min(bytes, front.data.size() - front.offset);
        front.offset += consumed;
        total_bytes_ -= consumed;
        if (front.offset == front.data.size()) {
            chunks_.pop_front();
        }
    }

    bool IsSendQueueEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return chunks_.empty();
    }

    size_t GetSendQueueSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_bytes_;
    }

    void ClearAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        chunks_.clear();
        total_bytes_ = 0;
    }

private:
    mutable std::mutex mutex_;
    struct Chunk {
        std::vector<char> data;
        size_t offset;
    };
    std::deque<Chunk> chunks_;
    size_t total_bytes_ = 0;
};

class ReceiveBuffer {
public:
    BufferView GetReceiveBuffer() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (write_pos_ == buffer_.size()) {
            buffer_.resize(buffer_.size() * 2);
        }
        return {buffer_.data() + write_pos_, buffer_.size() - write_pos_};
    }

    void CommitReceive(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        write_pos_ += bytes;
    }

    size_t GetReceiveBufferSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return write_pos_ - read_pos_;
    }

    void ClearAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        read_pos_ = write_pos_ = 0;
    }

private:
    mutable std::mutex mutex_;
    std::vector<char> buffer_{64 * 1024};
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
};

} // namespace