// ============================================================================
// 文件: src/net/buffer.cpp
// 描述: RingBuffer 环形缓冲区实现
// ============================================================================

#include "../../include/httpserver/net/buffer.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace httpserver::net {

// ============================================================================
// 构造函数
// ============================================================================

RingBuffer::RingBuffer(size_t initial_capacity) 
    : buffer_(std::max(initial_capacity, size_t(4))) {
    if (initial_capacity == 0) {
        throw std::invalid_argument("RingBuffer capacity cannot be 0");
    }
    read_pos_ = 0;
    write_pos_ = 0;
}

// ============================================================================
// 辅助函数
// ============================================================================

size_t RingBuffer::front_capacity() const {
    if (read_pos_ <= write_pos_) {
        return 0;
    }
    return read_pos_ - write_pos_;
}

size_t RingBuffer::back_capacity() const {
    if (write_pos_ < read_pos_) {
        return read_pos_ - write_pos_;
    }
    return capacity() - write_pos_;
}

void RingBuffer::expand_if_needed(size_t len) {
    if (writable_bytes() >= len) {
        return;
    }
    
    size_t new_capacity = capacity();
    while (new_capacity - readable_bytes() - 1 < len) {
        new_capacity *= 2;
    }
    
    std::vector<char> new_buffer(new_capacity);
    
    if (!empty()) {
        auto areas = readable_areas();
        char* dst = new_buffer.data();
        
        if (areas.first.len > 0) {
            std::memcpy(dst, areas.first.data, areas.first.len);
            dst += areas.first.len;
        }
        
        if (areas.second.len > 0) {
            std::memcpy(dst, areas.second.data, areas.second.len);
        }
    }
    
    buffer_.swap(new_buffer);
    read_pos_ = 0;
    write_pos_ = readable_bytes();
}

// ============================================================================
// 写入操作
// ============================================================================

size_t RingBuffer::write(const void* data, size_t len) {
    if (!data || len == 0) {
        return 0;
    }
    
    expand_if_needed(len);
    
    size_t writable = writable_bytes();
    size_t write_len = std::min(len, writable);
    
    if (write_len == 0) {
        return 0;
    }
    
    size_t back = back_capacity();
    const char* src = static_cast<const char*>(data);
    
    if (back >= write_len) {
        // 一次性写入后面
        std::memcpy(&buffer_[write_pos_], src, write_len);
    } else {
        // 分两部分写入
        std::memcpy(&buffer_[write_pos_], src, back);
        std::memcpy(&buffer_[0], src + back, write_len - back);
    }
    
    write_pos_ = (write_pos_ + write_len) % capacity();
    return write_len;
}

size_t RingBuffer::write(const std::string& str) {
    return write(str.data(), str.size());
}

// ============================================================================
// 读取操作
// ============================================================================

size_t RingBuffer::read(void* buf, size_t len) {
    if (!buf || len == 0 || empty()) {
        return 0;
    }
    
    size_t readable = readable_bytes();
    size_t read_len = std::min(len, readable);
    
    char* dst = static_cast<char*>(buf);
    size_t back = capacity() - read_pos_;
    
    if (back >= read_len) {
        // 一次性读取
        std::memcpy(dst, &buffer_[read_pos_], read_len);
    } else {
        // 分两部分读取
        std::memcpy(dst, &buffer_[read_pos_], back);
        std::memcpy(dst + back, &buffer_[0], read_len - back);
    }
    
    has_read(read_len);
    return read_len;
}

std::string RingBuffer::read_string(size_t len) {
    if (len == 0 || empty()) {
        return "";
    }
    
    size_t readable = readable_bytes();
    size_t read_len = std::min(len, readable);
    
    std::string result;
    result.resize(read_len);
    
    size_t actual = read(&result[0], read_len);
    result.resize(actual);
    
    return result;
}

// ============================================================================
// 零拷贝操作
// ============================================================================

std::pair<RingBuffer::Iovec, RingBuffer::Iovec> 
RingBuffer::writable_areas() const {
    Iovec first, second;
    first.data = nullptr;
    second.data = nullptr;
    first.len = 0;
    second.len = 0;
    
    if (full()) {
        return {first, second};
    }
    
    if (write_pos_ < read_pos_) {
        first.data = &buffer_[write_pos_];
        first.len = read_pos_ - write_pos_ - 1;
    } else {
        first.data = &buffer_[write_pos_];
        first.len = capacity() - write_pos_;
        
        if (read_pos_ > 0) {
            if (write_pos_ == 0) {
                first.len = capacity() - 1;
            } else {
                second.data = &buffer_[0];
                second.len = read_pos_;
            }
        } else {
            first.len = capacity() - write_pos_ - 1;
        }
    }
    
    return {first, second};
}

std::pair<RingBuffer::Iovec, RingBuffer::Iovec> 
RingBuffer::readable_areas() const {
    Iovec first, second;
    first.data = nullptr;
    second.data = nullptr;
    first.len = 0;
    second.len = 0;
    
    if (empty()) {
        return {first, second};
    }
    
    if (write_pos_ > read_pos_) {
        first.data = &buffer_[read_pos_];
        first.len = write_pos_ - read_pos_;
    } else {
        first.data = &buffer_[read_pos_];
        first.len = capacity() - read_pos_;
        
        if (write_pos_ > 0) {
            second.data = &buffer_[0];
            second.len = write_pos_;
        }
    }
    
    return {first, second};
}

// ============================================================================
// 指针移动
// ============================================================================

void RingBuffer::has_written(size_t len) {
    if (len > writable_bytes()) {
        throw std::out_of_range("has_written exceeds writable bytes");
    }
    write_pos_ = (write_pos_ + len) % capacity();
}

void RingBuffer::has_read(size_t len) {
    if (len > readable_bytes()) {
        throw std::out_of_range("has_read exceeds readable bytes");
    }
    read_pos_ = (read_pos_ + len) % capacity();
    
    if (read_pos_ == write_pos_) {
        read_pos_ = 0;
        write_pos_ = 0;
    }
}

// ============================================================================
// 查找操作
// ============================================================================

size_t RingBuffer::find(char c) const {
    if (empty()) {
        return std::string::npos;
    }
    
    size_t readable = readable_bytes();
    size_t back = capacity() - read_pos_;
    
    if (readable <= back) {
        auto it = std::find(buffer_.begin() + read_pos_, 
                           buffer_.begin() + read_pos_ + readable, c);
        if (it != buffer_.begin() + read_pos_ + readable) {
            return it - (buffer_.begin() + read_pos_);
        }
    } else {
        auto it1 = std::find(buffer_.begin() + read_pos_, buffer_.end(), c);
        if (it1 != buffer_.end()) {
            return it1 - (buffer_.begin() + read_pos_);
        }
        
        auto it2 = std::find(buffer_.begin(), 
                            buffer_.begin() + (readable - back), c);
        if (it2 != buffer_.begin() + (readable - back)) {
            return (buffer_.end() - (buffer_.begin() + read_pos_)) + 
                   (it2 - buffer_.begin());
        }
    }
    
    return std::string::npos;
}

size_t RingBuffer::find(const std::string& pattern) const {
    if (pattern.empty() || pattern.size() > readable_bytes()) {
        return std::string::npos;
    }
    
    for (size_t i = 0; i <= readable_bytes() - pattern.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < pattern.size(); ++j) {
            size_t pos = (read_pos_ + i + j) % capacity();
            if (buffer_[pos] != pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    
    return std::string::npos;
}

// ============================================================================
// 内存管理
// ============================================================================

void RingBuffer::ensure_writable(size_t len) {
    expand_if_needed(len);
}

void RingBuffer::shrink_to_fit() {
    if (empty()) {
        if (capacity() > 1024) {
            buffer_.resize(1024);
        }
        read_pos_ = 0;
        write_pos_ = 0;
        return;
    }
    
    size_t readable = readable_bytes();
    size_t new_capacity = std::max(readable * 2, size_t(1024));
    new_capacity = std::min(new_capacity, capacity());
    
    if (new_capacity >= capacity()) {
        return;
    }
    
    std::vector<char> new_buffer(new_capacity);
    
    auto areas = readable_areas();
    char* dst = new_buffer.data();
    
    if (areas.first.len > 0) {
        std::memcpy(dst, areas.first.data, areas.first.len);
        dst += areas.first.len;
    }
    
    if (areas.second.len > 0) {
        std::memcpy(dst, areas.second.data, areas.second.len);
    }
    
    buffer_.swap(new_buffer);
    read_pos_ = 0;
    write_pos_ = readable;
}

} // namespace httpserver::net