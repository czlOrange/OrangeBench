// ========== 2. HttpProtocolHandler - HTTP 协议处理 ==========
// 就像点餐员：听懂客人说什么，把菜端给客人

class HttpProtocolHandler {
public:
    explicit HttpProtocolHandler(std::shared_ptr<core::IEventDispatcher> dispatcher);
    
    // 设置路由表
    void SetGetHandler(const std::string& path, HttpHandler handler);
    void SetPostHandler(const std::string& path, HttpHandler handler);
    void SetStaticFileHandler(const std::string& url_prefix, const std::string& root_dir);
    
    // 处理数据（由 TCP 连接触发）
    void OnData(std::shared_ptr<core::IConnection> conn, std::string_view data);
    
    // 处理错误
    void OnError(std::shared_ptr<core::IConnection> conn, std::error_code ec);
    
private:
    void parseHttpRequest(std::string_view data, HttpRequest& req);
    void sendResponse(std::shared_ptr<core::IConnection> conn, const HttpResponse& resp);
    void handleWebSocketUpgrade(std::shared_ptr<core::IConnection> conn, const std::string& request);
    
    std::unordered_map<std::string, HttpHandler> get_handlers_;
    std::unordered_map<std::string, HttpHandler> post_handlers_;
    std::shared_ptr<core::IEventDispatcher> dispatcher_;
};