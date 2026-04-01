#pragma once

#include "http_request.hpp"
#include <string>

namespace httpserver::application::http {

class HttpParser {
public:
    static int Parse(const std::string& data, HttpRequest& request);
};

} // namespace httpserver::application::http
