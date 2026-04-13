// src/core/Buffer/buffer_view.cpp
#include "httpserver/core/Buffer/buffer_view.hpp"
#include "httpserver/core/Buffer/buffer.hpp"
#include <cstring>

namespace httpserver::core {

// ============================================================================
// BufferView 实现
// ============================================================================

BufferView::BufferView() : data(nullptr), size(0) {}

BufferView::BufferView(const char* d, size_t s) : data(d), size(s) {}

BufferView::BufferView(const std::string& str)
    : data(str.data()), size(str.size()) {}

BufferView::BufferView(const Buffer& buf)
    : data(buf.data()), size(buf.size()) {}

std::string_view BufferView::ToStringView() const {
    return std::string_view(data, size);
}

// ============================================================================
// MutableBufferView 实现
// ============================================================================

MutableBufferView::MutableBufferView() : data(nullptr), size(0) {}

MutableBufferView::MutableBufferView(char* d, size_t s) : data(d), size(s) {}

MutableBufferView::MutableBufferView(Buffer& buf)
    : data(buf.data()), size(buf.capacity()) {}

std::string_view MutableBufferView::ToStringView() const {
    return std::string_view(data, size);
}

} // namespace httpserver::core