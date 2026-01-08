// src/net/buffer.cpp
#include "../../include/httpserver/net/buffer.hpp"
#include <cstring>
#include <stdexcept>

namespace httpserver::net {

RingBuffer::RingBuffer(size_t initial_capacity)
    : buffer_(initial_capacity) {
    if (initial_capacity == 0) {
        throw std::invalid_argument("Buffer capacity cannot be zero");
    }
    read_pos_ = 0;
    write_pos_ = 0;
}

size_t RingBuffer::write(const void* data, size_t len) {
    if (len == 0) return 0;
    
    ensure_writable(len);
    
    size_t written = 0;
    const char* src = static_cast<const char*>(data);
    
    // 如果写指针在读指针后面（正常情况）
    if (write_pos_ >= read_pos_) {
        size_t space_to_end = buffer_.size() - write_pos_;
        size_t to_write = std::min(len, space_to_end);
        
        if (to_write > 0) {
            std::memcpy(buffer_.data() + write_pos_, src, to_write);
            written += to_write;
            write_pos_ += to_write;
            src += to_write;
            len -= to_write;
        }
        
        // 如果还有数据要写，从头部开始写
        if (len > 0 && read_pos_ > 0) {
            to_write = std::min(len, read_pos_);
            std::memcpy(buffer_.data(), src, to_write);
            written += to_write;
            write_pos_ = to_write;
        }
    } 
    // 如果写指针在读指针前面（有回绕）
    else {
        size_t available_space = read_pos_ - write_pos_;
        size_t to_write = std::min(len, available_space);
        
        std::memcpy(buffer_.data() + write_pos_, src, to_write);
        written += to_write;
        write_pos_ += to_write;
    }
    
    return written;
}

size_t RingBuffer::write(const std::string& str) {
    return write(str.data(), str.size());
}

size_t RingBuffer::read(void* buf, size_t len) {
    if (len == 0) return 0;
    
    size_t to_read = std::min(len, readable_bytes());
    if (to_read == 0) return 0;
    
    char* dest = static_cast<char*>(buf);
    size_t read = 0;
    
    // 如果读指针在写指针前面（正常情况）
    if (read_pos_ < write_pos_) {
        size_t data_to_end = write_pos_ - read_pos_;
        size_t to_copy = std::min(to_read, data_to_end);
        
        std::memcpy(dest, buffer_.data() + read_pos_, to_copy);
        read += to_copy;
        read_pos_ += to_copy;
    }
    // 如果读指针在写指针后面（有回绕）
    else {
        size_t data_to_end = buffer_.size() - read_pos_;
        size_t to_copy = std::min(to_read, data_to_end);
        
        std::memcpy(dest, buffer_.data() + read_pos_, to_copy);
        read += to_copy;
        read_pos_ += to_copy;
        dest += to_copy;
        to_read -= to_copy;
        
        // 如果还有数据要读，从头部开始读
        if (to_read > 0) {
            read_pos_ = 0;
            to_copy = std::min(to_read, write_pos_);
            
            std::memcpy(dest, buffer_.data(), to_copy);
            read += to_copy;
            read_pos_ = to_copy;
        }
    }
    
    // 如果读取了所有数据，重置指针以提高效率
    if (read_pos_ == write_pos_) {
        read_pos_ = 0;
        write_pos_ = 0;
    }
    
    return read;
}

std::string RingBuffer::read_string(size_t len) {
    size_t to_read = std::min(len, readable_bytes());
    if (to_read == 0) return "";
    
    std::string result(to_read, '\0');
    read(&result[0], to_read);
    return result;
}

std::pair<RingBuffer::Iovec, RingBuffer::Iovec> RingBuffer::writable_areas() const {
    Iovec first = {nullptr, 0};
    Iovec second = {nullptr, 0};
    
    size_t available_space = writable_bytes();
    if (available_space == 0) {
        return {first, second};
    }
    
    // 如果写指针在读指针后面（正常情况）
    if (write_pos_ >= read_pos_) {
        size_t space_to_end = buffer_.size() - write_pos_;
        size_t space_before_read = (read_pos_ == 0) ? 0 : (read_pos_ - 1);
        
        first.data = buffer_.data() + write_pos_;
        first.len = std::min(available_space, space_to_end);
        
        if (first.len < available_space && space_before_read > 0) {
            second.data = buffer_.data();
            second.len = std::min(available_space - first.len, space_before_read);
        }
    }
    // 如果写指针在读指针前面（有回绕）
    else {
        first.data = buffer_.data() + write_pos_;
        first.len = read_pos_ - write_pos_;
    }
    
    return {first, second};
}

std::pair<RingBuffer::Iovec, RingBuffer::Iovec> RingBuffer::readable_areas() const {
    Iovec first = {nullptr, 0};
    Iovec second = {nullptr, 0};
    
    size_t readable = readable_bytes();
    if (readable == 0) {
        return {first, second};
    }
    
    // 如果读指针在写指针前面（正常情况）
    if (read_pos_ < write_pos_) {
        first.data = buffer_.data() + read_pos_;
        first.len = write_pos_ - read_pos_;
    }
    // 如果读指针在写指针后面（有回绕）
    else {
        size_t data_to_end = buffer_.size() - read_pos_;
        first.data = buffer_.data() + read_pos_;
        first.len = std::min(readable, data_to_end);
        
        if (first.len < readable) {
            second.data = buffer_.data();
            second.len = readable - first.len;
        }
    }
    
    return {first, second};
}

void RingBuffer::has_written(size_t len) {
    if (len > writable_bytes()) {
        throw std::out_of_range("has_written: length exceeds writable bytes");
    }
    
    write_pos_ = (write_pos_ + len) % buffer_.size();
}

void RingBuffer::has_read(size_t len) {
    if (len > readable_bytes()) {
        throw std::out_of_range("has_read: length exceeds readable bytes");
    }
    
    read_pos_ = (read_pos_ + len) % buffer_.size();
    
    // 如果读取了所有数据，重置指针以提高效率
    if (read_pos_ == write_pos_) {
        read_pos_ = 0;
        write_pos_ = 0;
    }
}

size_t RingBuffer::find(const std::string& pattern) const {
    if (pattern.empty() || readable_bytes() < pattern.size()) {
        return -1;
    }
    
    size_t total_readable = readable_bytes();
    auto areas = readable_areas();
    
    // 在第一段连续内存中搜索
    if (areas.first.len >= pattern.size()) {
        const char* start = static_cast<const char*>(areas.first.data);
        const char* end = start + areas.first.len - pattern.size() + 1;
        
        for (const char* p = start; p < end; ++p) {
            if (std::memcmp(p, pattern.data(), pattern.size()) == 0) {
                return (p - start) + (buffer_.data() + read_pos_ - start);
            }
        }
    }
    
    // 如果跨越了两段内存
    if (areas.second.len > 0) {
        // 先处理跨越边界的情况
        if (areas.first.len + areas.second.len >= pattern.size()) {
            for (size_t i = 0; i < areas.first.len; ++i) {
                bool found = true;
                
                // 检查第一段
                size_t j;
                for (j = 0; j < pattern.size() - i && j < areas.first.len - i; ++j) {
                    if (static_cast<const char*>(areas.first.data)[i + j] != pattern[j]) {
                        found = false;
                        break;
                    }
                }
                
                // 检查第二段（如果需要）
                if (found && j < pattern.size()) {
                    for (size_t k = 0; k < pattern.size() - j; ++k) {
                        if (static_cast<const char*>(areas.second.data)[k] != pattern[j + k]) {
                            found = false;
                            break;
                        }
                    }
                }
                
                if (found) {
                    return i;
                }
            }
        }
        
        // 在第二段内存中搜索
        if (areas.second.len >= pattern.size()) {
            const char* start = static_cast<const char*>(areas.second.data);
            const char* end = start + areas.second.len - pattern.size() + 1;
            
            for (const char* p = start; p < end; ++p) {
                if (std::memcmp(p, pattern.data(), pattern.size()) == 0) {
                    return areas.first.len + (p - start);
                }
            }
        }
    }
    
    return -1; // 没找到
}

size_t RingBuffer::find(char c) const {
    size_t readable = readable_bytes();
    if (readable == 0) return -1;
    
    auto areas = readable_areas();
    
    // 在第一段连续内存中搜索
    const char* p1 = static_cast<const char*>(areas.first.data);
    const char* end1 = p1 + areas.first.len;
    for (const char* p = p1; p < end1; ++p) {
        if (*p == c) {
            return (p - p1);
        }
    }
    
    // 在第二段连续内存中搜索
    if (areas.second.len > 0) {
        const char* p2 = static_cast<const char*>(areas.second.data);
        const char* end2 = p2 + areas.second.len;
        for (const char* p = p2; p < end2; ++p) {
            if (*p == c) {
                return areas.first.len + (p - p2);
            }
        }
    }
    
    return -1; // 没找到
}

void RingBuffer::ensure_writable(size_t len) {
    if (writable_bytes() >= len) {
        return;
    }
    
    // 如果需要扩容
    size_t new_capacity = capacity();
    size_t required_capacity = readable_bytes() + len;
    
    while (new_capacity < required_capacity) {
        new_capacity = new_capacity * 2;
    }
    
    // 重新分配内存并整理数据
    std::vector<char> new_buffer(new_capacity);
    
    if (readable_bytes() > 0) {
        auto areas = readable_areas();
        size_t offset = 0;
        
        std::memcpy(new_buffer.data() + offset, areas.first.data, areas.first.len);
        offset += areas.first.len;
        
        if (areas.second.len > 0) {
            std::memcpy(new_buffer.data() + offset, areas.second.data, areas.second.len);
            offset += areas.second.len;
        }
    }
    
    buffer_.swap(new_buffer);
    read_pos_ = 0;
    write_pos_ = readable_bytes();
}

void RingBuffer::shrink_to_fit() {
    if (readable_bytes() == 0) {
        buffer_.resize(1024); // 最小容量
        read_pos_ = 0;
        write_pos_ = 0;
        return;
    }
    
    size_t new_capacity = std::max(readable_bytes() + 1, static_cast<size_t>(1024));
    
    if (new_capacity < capacity()) {
        std::vector<char> new_buffer(new_capacity);
        
        auto areas = readable_areas();
        size_t offset = 0;
        
        std::memcpy(new_buffer.data() + offset, areas.first.data, areas.first.len);
        offset += areas.first.len;
        
        if (areas.second.len > 0) {
            std::memcpy(new_buffer.data() + offset, areas.second.data, areas.second.len);
            offset += areas.second.len;
        }
        
        buffer_.swap(new_buffer);
        read_pos_ = 0;
        write_pos_ = readable_bytes();
    }
}

size_t RingBuffer::front_capacity() const {
    if (write_pos_ >= read_pos_) {
        return buffer_.size() - write_pos_;
    }
    return read_pos_ - write_pos_;
}

size_t RingBuffer::back_capacity() const {
    if (write_pos_ >= read_pos_) {
        return read_pos_;
    }
    return buffer_.size() - read_pos_;
}

void RingBuffer::expand_if_needed(size_t len) {
    if (writable_bytes() < len) {
        ensure_writable(len);
    }
}

} // namespace httpserver::net