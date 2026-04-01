#include "httpserver/application/http/http_response.hpp"
#include <sstream>

namespace httpserver::application::http {

std::string HttpResponse::GetDefaultMessage(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

void HttpResponse::SetStatus(int code, const std::string& message) {
    status_code_ = code;
    status_message_ = message.empty() ? GetDefaultMessage(code) : message;
}

std::string HttpResponse::ToString() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code_ << " " << status_message_ << "\r\n";
    for (const auto& [key, value] : headers_) {
        oss << key << ": " << value << "\r\n";
    }
    oss << "Content-Length: " << body_.size() << "\r\n";
    oss << "\r\n";
    oss << body_;
    return oss.str();
}

} // namespace httpserver::application::http
