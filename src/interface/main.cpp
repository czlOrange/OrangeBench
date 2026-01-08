// src/interface/main.cpp
#include "server.hpp"
#include "httpserver/infrastructure/ilogger.hpp"

// 标准库头文件按字母序排列，区分业务头文件
#include <csignal>
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>

// 全局常量提取（避免魔法值，语义化）
namespace {
    // 服务器配置常量
    constexpr const char* kServerIp = "0.0.0.0";
    constexpr uint16_t kServerPort = 8080;
    constexpr size_t kMaxConnections = 100;
    constexpr int kReceiveTimeoutMs = 3000;
    
    // 主循环配置
    constexpr std::chrono::seconds kLoopSleepInterval{1};
    constexpr int kStatLogInterval = 5;  // 每5秒输出一次统计
    
    // 全局运行状态（原子变量保证线程安全）
    std::atomic<bool> g_isServerRunning{true};
}

/**
 * @brief 信号处理函数
 * @param signal 接收到的信号值
 * @note 处理SIGINT(Ctrl+C)和SIGTERM(进程终止)信号，触发服务器优雅退出
 */
void handleSignal(int signal) {
    std::cout << "\n[Signal] 收到信号: " << signal << "，开始优雅关闭服务器..." << std::endl;
    g_isServerRunning = false;
}

/**
 * @brief 注册信号处理器
 * @note 注册SIGINT和SIGTERM的处理逻辑，用于触发服务器退出
 */
void registerSignalHandlers() {
    std::signal(SIGINT, handleSignal);   // Ctrl+C
    std::signal(SIGTERM, handleSignal);  // kill 命令默认信号
}

int main() {
    // 1. 初始化信号处理
    registerSignalHandlers();

    try {
        // 2. 初始化日志器（基础设施层）
        auto logger = httpserver::infrastructure::create_console_logger();
        logger->info("[Init] TCP服务器开始初始化...");

        // 3. 创建并配置服务器
        httpserver::interface::TcpServer server(logger);
        server.set_max_connections(kMaxConnections);
        server.set_receive_timeout(kReceiveTimeoutMs);
        logger->info("[Config] 服务器配置完成 - 最大连接数: " + std::to_string(kMaxConnections) + 
                     ", 接收超时: " + std::to_string(kReceiveTimeoutMs) + "ms");

        // 4. 启动服务器
        if (server.start(kServerIp, kServerPort)) {
            logger->info("[Start] 服务器启动成功！监听地址: " + std::string(kServerIp) + 
                         ":" + std::to_string(kServerPort));
            logger->info("[Hint] 按 Ctrl+C 可停止服务器");

            // 5. 主循环（保持服务器运行，定期输出统计）
            int loopCount = 0;
            while (g_isServerRunning && server.is_running()) {
                // 降低CPU占用，休眠指定时间
                std::this_thread::sleep_for(kLoopSleepInterval);

                // 每kStatLogInterval秒输出一次活跃连接数
                if (++loopCount % kStatLogInterval == 0) {
                    const auto activeConnCount = server.get_connection_count();
                    logger->info("[Stat] 当前活跃连接数: " + std::to_string(activeConnCount));
                }
            }

            // 6. 优雅停止服务器
            logger->info("[Stop] 开始停止服务器...");
            server.stop();
            logger->info("[Stop] 服务器已成功停止");
        } else {
            logger->error("[Error] 服务器启动失败！监听地址: " + std::string(kServerIp) + 
                          ":" + std::to_string(kServerPort));
            return 1;
        }

    } catch (const std::runtime_error& e) {
        // 细化异常捕获：运行时异常
        std::cerr << "[Fatal] 运行时错误: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        // 通用异常捕获
        std::cerr << "[Fatal] 未预期的异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        // 兜底捕获所有未处理异常
        std::cerr << "[Fatal] 未知致命错误！" << std::endl;
        return 1;
    }

    return 0;
}