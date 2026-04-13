#pragma once

#include <string>
#include <unordered_map>

namespace httpserver::application::http {

class HttpResponse {
public:
    HttpResponse() : status_code_(200), status_message_("OK") {}

    void SetStatus(int code, const std::string& message = "");
    void AddHeader(const std::string& key, const std::string& value) { headers_[key] = value; }
    void SetBody(const std::string& body) { body_ = body; }
    void SetContentType(const std::string& type) { AddHeader("Content-Type", type); }

    std::string ToString() const;

private:
    static std::string GetDefaultMessage(int code);

    int status_code_;
    std::string status_message_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

} // namespace httpserver::application::http