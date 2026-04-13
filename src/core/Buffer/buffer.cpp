// src/core/Buffer/buffer.cpp
#include "httpserver/core/Buffer/buffer.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace httpserver::core {

Buffer::Buffer(void* data, size_t capacity, std::function<void(void*)> deleter)
    : data_(data), size_(0), capacity_(capacity), deleter_(std::move(deleter)) {
    // 初始有效数据大小为0
}

Buffer::~Buffer() {
    if (deleter_ && data_) {
        deleter_(data_);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : data_(other.data_)
    , size_(other.size_)
    , capacity_(other.capacity_)
    , deleter_(std::move(other.deleter_)) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    other.deleter_ = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (deleter_ && data_) {
            deleter_(data_);
        }
        // 转移资源
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        deleter_ = std::move(other.deleter_);
        // 清空源对象
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.deleter_ = nullptr;
    }
    return *this;
}

char* Buffer::data() {
    return static_cast<char*>(data_);
}

const char* Buffer::data() const {
    return static_cast<const char*>(data_);
}

size_t Buffer::size() const {
    return size_;
}

size_t Buffer::capacity() const {
    return capacity_;
}

void Buffer::resize(size_t new_size) {
    if (new_size > capacity_) {
        throw std::out_of_range("Buffer::resize: new size exceeds capacity");
    }
    size_ = new_size;
}

} // namespace httpserver::core