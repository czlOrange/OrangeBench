// ========== 3. SpeedTestApi - 测速业务 ==========
// 就像厨师：做测速相关的菜

class SpeedTestApi {
public:
    explicit SpeedTestApi(Database& db);
    
    // 注册到 HTTP 处理器
    void Register(HttpProtocolHandler& handler);
    
private:
    void handleSaveRecord(const HttpRequest& req, HttpResponse& resp);
    void handleGetStats(const HttpRequest& req, HttpResponse& resp);
    void handleGetDevices(const HttpRequest& req, HttpResponse& resp);
    void handleGetHistory(const HttpRequest& req, HttpResponse& resp);
    void handleAdminPage(const HttpRequest& req, HttpResponse& resp);
    
    Database& db_;
};