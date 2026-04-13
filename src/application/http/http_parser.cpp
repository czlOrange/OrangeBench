#include "application/http/http_parser.hpp"
#include "httpserver/application/http/http_request.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace httpserver::application::http {

int HttpParser::Parse(const std::string& data, HttpRequest& request) {
    // 查找请求行结束位置（\r\n）
    size_t line_end = data.find("\r\n");
    if (line_end == std::string::npos) {
        return 0; // 数据不完整，等待更多数据
    }

    // 解析请求行: "GET /path HTTP/1.1"
    std::string request_line = data.substr(0, line_end);
    std::istringstream iss(request_line);
    std::string method, path, version;
    if (!(iss >> method >> path >> version)) {
        return -1; // 解析失败
    }

    request.SetMethod(method);
    request.SetPath(path);
    request.SetVersion(version);

    // 解析头部
    size_t pos = line_end + 2;
    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return 0; // 头部未完整
    }

    while (pos < header_end) {
        size_t line_end2 = data.find("\r\n", pos);
        if (line_end2 == std::string::npos) break;
        std::string header_line = data.substr(pos, line_end2 - pos);
        size_t colon = header_line.find(':');
        if (colon != std::string::npos) {
            std::string key = header_line.substr(0, colon);
            std::string value = header_line.substr(colon + 1);
            // 去除前导空格
            value.erase(0, value.find_first_not_of(" \t"));
            request.AddHeader(key, value);
        }
        pos = line_end2 + 2;
    }

    // 解析请求体
    size_t body_start = header_end + 4;
    if (body_start < data.size()) {
        std::string content_length_str = request.GetHeader("Content-Length");
        if (!content_length_str.empty()) {
            size_t content_length = std::stoul(content_length_str);
            if (body_start + content_length <= data.size()) {
                std::string body = data.substr(body_start, content_length);
                request.SetBody(body);
                
                // 如果是表单数据，自动解析
                if (request.GetHeader("Content-Type").find("application/x-www-form-urlencoded") != std::string::npos) {
                    const_cast<HttpRequest&>(request).ParseFormData();
                }
            }
        }
    }

    return header_end + 4;
}

} // namespace httpserver::application::http
