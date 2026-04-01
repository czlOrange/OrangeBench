// include/httpserver/coroutine/connection_adapter.hpp
#pragma once

#include "../core/connection.hpp"
#include "scheduler.hpp"

namespace httpserver::coroutine {

// 连接协程适配器：将Connection接口适配为协程接口
template<typename ConnectionType>
class ConnectionAdapter {
public:
    explicit ConnectionAdapter(std::shared_ptr<ConnectionType> connection);
    
    // 协程化的连接操作
    Task<std::error_code> Connect();
    Task<std::error_code> Disconnect();
    
    // 协程化的数据操作
    Task<size_t> Send(std::string_view data);
    Task<std::string> Recv(size_t max_len = 4096);
    
    // 流式操作（类似生成器）
    class StreamReader {
    public:
        explicit StreamReader(std::shared_ptr<ConnectionType> conn);
        
        // 协程生成器：持续读取数据直到连接关闭
        Task<std::optional<std::string>> ReadChunk(size_t chunk_size = 4096);
        
        // 读取指定长度的数据
        Task<std::string> ReadExactly(size_t length);
        
        // 读取直到遇到分隔符
        Task<std::string> ReadUntil(std::string_view delimiter);
        
        // 读取一行
        Task<std::string> ReadLine();
    };
    
    // 获取流式读取器
    StreamReader GetStreamReader();
    
    // 批量操作
    Task<> SendAll(std::vector<std::string_view> chunks);
    Task<std::vector<std::string>> RecvMultiple(size_t count, size_t chunk_size = 4096);
    
    // 超时控制
    template<typename T>
    Task<std::optional<T>> WithTimeout(Task<T> task, std::chrono::milliseconds timeout);
    
    // 重试机制
    template<typename T>
    Task<T> WithRetry(Task<T> task, 
                     size_t max_attempts = 3,
                     std::chrono::milliseconds delay = std::chrono::milliseconds(100));
    
    // 连接池集成
    class PooledConnection {
    public:
        // 借用连接（协程感知的连接池）
        static Task<PooledConnection> Borrow(std::string_view endpoint);
        
        // 使用连接执行操作
        template<typename Func>
        Task<typename std::invoke_result_t<Func, ConnectionType&>> Execute(Func func);
        
        // 自动归还连接（RAII）
        ~PooledConnection();
    };
    
private:
    std::shared_ptr<ConnectionType> connection_;
};

// HTTP专用的协程适配器
class HttpConnectionAdapter {
public:
    explicit HttpConnectionAdapter(std::shared_ptr<core::Connection> connection);
    
    // HTTP请求/响应
    struct HttpRequest {
        std::string method;
        std::string path;
        std::unordered_map<std::string, std::string> headers;
        std::string body;
    };
    
    struct HttpResponse {
        int status_code;
        std::string status_text;
        std::unordered_map<std::string, std::string> headers;
        std::string body;
    };
    
    // HTTP操作
    Task<HttpResponse> SendRequest(const HttpRequest& request);
    Task<HttpResponse> Get(std::string_view path);
    Task<HttpResponse> Post(std::string_view path, std::string_view body);
    
    // 流式请求体
    class RequestStream {
    public:
        Task<> WriteChunk(std::string_view chunk);
        Task<> WriteEnd();
    };
    
    // 流式响应体
    class ResponseStream {
    public:
        Task<std::optional<std::string>> ReadChunk();
    };
          
    // 支持分块传输编码
    Task<HttpResponse> SendChunkedRequest(std::string_view path, 
                                         std::function<Task<>(RequestStream&)> body_writer);
    
    // WebSocket支持
    class WebSocketSession {
    public:
        Task<> SendText(std::string_view text);
        Task<> SendBinary(std::string_view data);
        Task<std::string> ReceiveText();
        Task<> Close();
    };
    
    Task<WebSocketSession> UpgradeToWebSocket(std::string_view path);
};

} // namespace httpserver::coroutine