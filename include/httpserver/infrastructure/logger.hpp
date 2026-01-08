// include/httpserver/infrastructure/ilogger.hpp
#pragma once
#include <string>

namespace httpserver::infrastructure {
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void info(const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
};

// 简单工厂
std::shared_ptr<ILogger> create_console_logger();
}