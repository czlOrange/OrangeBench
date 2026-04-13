#pragma once

#include <string>
#include <unordered_map>

namespace httpserver::application::http {

class HttpRequest {
public:
    HttpRequest() = default;

    // 请求行
    void SetMethod(const std::string& method) { method_ = method; }
    void SetPath(const std::string& path) { path_ = path; }
    void SetVersion(const std::string& version) { version_ = version; }

    const std::string& GetMethod() const { return method_; }
    const std::string& GetPath() const { return path_; }
    const std::string& GetVersion() const { return version_; }

    // 头部
    void AddHeader(const std::string& key, const std::string& value) { headers_[key] = value; }
    const std::unordered_map<std::string, std::string>& GetHeaders() const { return headers_; }
    std::string GetHeader(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    // 请求体
    void SetBody(const std::string& body) { body_ = body; }
    const std::string& GetBody() const { return body_; }

    // 表单数据解析（声明，实现放在 .cpp）
    void ParseFormData();
    std::string GetFormValue(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& GetFormData() const { return form_data_; }
    bool HasFormData() const { return !form_data_.empty(); }

private:
    std::string method_;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::unordered_map<std::string, std::string> form_data_;
};

} // namespace httpserver::application::http