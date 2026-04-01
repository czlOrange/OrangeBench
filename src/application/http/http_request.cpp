#include "httpserver/application/http/http_request.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace httpserver::application::http {

// URL 解码函数
static std::string urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            // 解码 %XX
            char hex[3] = {str[i+1], str[i+2], 0};
            char* endptr;
            long val = strtol(hex, &endptr, 16);
            if (endptr == hex + 2) {
                result += static_cast<char>(val);
                i += 2;
                continue;
            }
        } else if (str[i] == '+') {
            result += ' ';
            continue;
        }
        result += str[i];
    }
    return result;
}

void HttpRequest::ParseFormData() {
    // 检查 Content-Type 是否为 application/x-www-form-urlencoded
    std::string content_type = GetHeader("Content-Type");
    if (content_type.find("application/x-www-form-urlencoded") == std::string::npos) {
        return;
    }
    
    form_data_.clear();
    std::string body = body_;
    
    // 按 & 分割
    size_t start = 0;
    size_t end = body.find('&');
    
    while (true) {
        std::string pair;
        if (end == std::string::npos) {
            pair = body.substr(start);
        } else {
            pair = body.substr(start, end - start);
        }
        
        // 按 = 分割
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            form_data_[urlDecode(key)] = urlDecode(value);
        }
        
        if (end == std::string::npos) break;
        start = end + 1;
        end = body.find('&', start);
    }
}

std::string HttpRequest::GetFormValue(const std::string& key) const {
    auto it = form_data_.find(key);
    return it != form_data_.end() ? it->second : "";
}

} // namespace httpserver::application::http
