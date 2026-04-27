// src/application/http/handler/AdminPageHandler.cpp
#include "httpserver/application/http/handler/AdminPageHandler.hpp"
#include "httpserver/application/http/http_protocol_handler.hpp"
#include "httpserver/application/http/http_request.hpp"
#include "httpserver/application/http/http_response.hpp"
#include "httpserver/infrastructure/database.hpp"
#include <iostream>
#include <sstream>

namespace httpserver::application::http {

    
AdminPageHandler::AdminPageHandler(std::shared_ptr<Database> db)
    : db_(std::move(db)) {std::cout << "handleAdminPage called" << std::endl;}

void AdminPageHandler::Register(HttpProtocolHandler& handler) {
    std::cout << "Registering /admin route..." << std::endl;
    handler.Get("/admin", [this](const HttpRequest& req, HttpResponse& resp) {
        std::cout << "Lambda called for /admin" << std::endl;
        handleAdminPage(req, resp);
    });
}

void AdminPageHandler::handleAdminPage(const HttpRequest& req, HttpResponse& resp) {
    // 实现管理员页面逻辑
    if (!db_) {
        resp.SetBody("<html><body><h1>Database not available</h1></body></html>");
        resp.SetContentType("text/html");
        return;
    }
    
    auto stats = db_->getStats();
    auto devices = db_->getDevices();
    auto history = db_->getHistory(20);
    
    std::stringstream html;
    html << R"(
<!DOCTYPE html>
<html>
<head>
    <title>Admin Panel - Speed Test Server</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        table { border-collapse: collapse; width: 100%; margin-bottom: 20px; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #4CAF50; color: white; }
        .stats { background-color: #f2f2f2; padding: 10px; margin-bottom: 20px; border-radius: 5px; }
    </style>
</head>
<body>
    <h1>Admin Panel</h1>
    
    <div class="stats">
        <h2>Statistics</h2>
        <p>Total Tests: )" << stats.total_tests << R"(</p>
        <p>Average Speed: )" << stats.avg_speed << R"( Mbps</p>
        <p>Max Speed: )" << stats.max_speed << R"( Mbps</p>
        <p>Average Latency: )" << stats.avg_latency << R"( ms</p>
        <p>Active Devices: )" << stats.active_devices << R"(</p>
        <p>Total Data Transferred: )" << stats.total_bytes_transferred / 1024 / 1024 << R"( MB</p>
    </div>
    
    <h2>Devices</h2>
    <table>
        <tr><th>Device ID</th><th>Name</th><th>Tests</th><th>Avg Speed</th><th>Avg Latency</th><th>Last Seen</th></tr>
)";
    
    for (const auto& device : devices) {
        html << "<tr>"
             << "<td>" << device.device_id << "</td>"
             << "<td>" << device.device_name << "</table>"
             << "<td>" << device.total_tests << "</td>"
             << "<td>" << device.avg_speed << " Mbps</td>"
             << "<td>" << device.avg_latency << " ms</td>"
             << "<td>" << device.last_seen << "</td>"
             << "</tr>";
    }
    
    html << R"(
    </table>
    
    <h2>Recent Tests (Last 20)</h2>
    <table>
        <tr><th>ID</th><th>Device</th><th>Download</th><th>Upload</th><th>Latency</th><th>Time</th></tr>
)";
    
    for (const auto& record : history) {
        html << "<tr>"
             << "<td>" << record.id << "</td>"
             << "<td>" << record.device_name << "</td>"
             << "<td>" << record.speed_mbps << " Mbps</td>"
             << "<td>" << record.upload_speed_mbps << " Mbps</td>"
             << "<td>" << record.latency_ms << " ms</td>"
             << "<td>" << record.created_at << "</td>"
             << "</tr>";
    }
    
    html << R"(
    </table>
</body>
</html>
)";
    
    resp.SetBody(html.str());
    resp.SetContentType("text/html");
}

} // namespace httpserver::application::http