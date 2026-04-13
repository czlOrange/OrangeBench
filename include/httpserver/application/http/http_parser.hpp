#pragma once

#include <string>

namespace httpserver::application::http {

class HttpRequest;

class HttpParser {
public:
    static int Parse(const std::string& data, HttpRequest& request);
};

} // namespace httpserver::application::http