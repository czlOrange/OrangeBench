// ========== 4. HttpServer - 门面类 ==========
// 就像餐厅门口迎宾：把客人引导到正确的地方

class HttpServer {
public:
    explicit HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher);
    
    // 对外接口（保持不变）
    void Get(const std::string& path, HttpHandler handler);
    void Post(const std::string& path, HttpHandler handler);
    void ServeStatic(const std::string& url_prefix, const std::string& root_dir);
    void Listen(const std::string& host, uint16_t port);
    
private:
    std::shared_ptr<core::IEventDispatcher> dispatcher_;
    std::shared_ptr<core::IIOHandler> io_handler_;
    TcpAcceptor acceptor_;
    HttpProtocolHandler protocol_handler_;
    Database db_;
    SpeedTestApi speed_test_api_;
};