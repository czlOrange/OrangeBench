// src/application/http/handler/SpeedTestApi.cpp
#include "httpserver/application/http/handler/SpeedTestApi.hpp"
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace httpserver::application::http {

static std::string findJsonValue(const std::string& body, const std::string& key) {
    size_t pos = body.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = body.find(":", pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) pos++;
    if (body[pos] == '"') {
        pos++;
        size_t end = body.find("\"", pos);
        return body.substr(pos, end - pos);
    } else {
        size_t end = body.find_first_of(",}", pos);
        return body.substr(pos, end - pos);
    }
}

SpeedTestApi::SpeedTestApi(std::shared_ptr<Database> db) : db_(std::move(db)) {
    if (db_) {
        std::cout << "[SpeedTestApi] Database initialized" << std::endl;
    } else {
        std::cerr << "[SpeedTestApi] WARNING: Database is null!" << std::endl;
    }
}

void SpeedTestApi::Register(HttpProtocolHandler& handler) {
    handler.Get("/api/stats", [this](const HttpRequest& req, HttpResponse& resp) {
        handleGetStats(req, resp);
    });
    
    handler.Get("/api/devices", [this](const HttpRequest& req, HttpResponse& resp) {
        handleGetDevices(req, resp);
    });
    
    handler.Get("/api/history", [this](const HttpRequest& req, HttpResponse& resp) {
        handleGetHistory(req, resp);
    });
    
    handler.Post("/api/speedtest", [this](const HttpRequest& req, HttpResponse& resp) {
        handleSaveRecord(req, resp);
    });
    
    std::cout << "[SpeedTestApi] API routes registered." << std::endl;
}

void SpeedTestApi::handleSaveRecord(const HttpRequest& req, HttpResponse& resp) {
    // ✅ 添加空指针检查
    if (!db_) {
        resp.SetStatus(500);
        resp.SetBody(R"({"status":"error","message":"Database not available"})");
        resp.SetContentType("application/json");
        return;
    }
    
    std::string body = req.GetBody();
    SpeedRecord record;

    // 原有字段
    std::string speed_str = findJsonValue(body, "speed");
    if (!speed_str.empty()) {
        try {
            record.speed_mbps = std::stod(speed_str);
        } catch (...) { record.speed_mbps = 0.0; }
    }

    std::string dev_id = findJsonValue(body, "device_id");
    if (!dev_id.empty()) record.device_id = dev_id;
    else record.device_id = "device_" + std::to_string(rand());

    std::string dev_name = findJsonValue(body, "device_name");
    if (!dev_name.empty()) record.device_name = dev_name;
    else record.device_name = "Unknown Device";

    // 新增字段
    std::string upload_speed_str = findJsonValue(body, "upload_speed");
    if (!upload_speed_str.empty()) {
        try {
            record.upload_speed_mbps = std::stod(upload_speed_str);
        } catch (...) { record.upload_speed_mbps = 0.0; }
    }

    std::string latency_str = findJsonValue(body, "latency_ms");
    if (!latency_str.empty()) {
        try {
            record.latency_ms = std::stoi(latency_str);
        } catch (...) { record.latency_ms = 0; }
    }

    std::string jitter_str = findJsonValue(body, "jitter_ms");
    if (!jitter_str.empty()) {
        try {
            record.jitter_ms = std::stod(jitter_str);
        } catch (...) { record.jitter_ms = 0.0; }
    }

    std::string loss_str = findJsonValue(body, "packet_loss");
    if (!loss_str.empty()) {
        try {
            record.packet_loss = std::stod(loss_str);
        } catch (...) { record.packet_loss = 0.0; }
    }

    std::string total_bytes_str = findJsonValue(body, "total_bytes");
    if (!total_bytes_str.empty()) {
        try {
            record.total_bytes = std::stoull(total_bytes_str);
        } catch (...) {
            record.total_bytes = static_cast<size_t>(record.speed_mbps * 1024 * 1024 / 8 * 10);
        }
    } else {
        record.total_bytes = static_cast<size_t>(record.speed_mbps * 1024 * 1024 / 8 * 10);
    }

    record.test_duration = 10;

    if (db_->saveSpeedRecord(record)) {
        resp.SetBody("{\"status\":\"ok\",\"message\":\"Record saved\"}");
    } else {
        resp.SetStatus(500);
        resp.SetBody("{\"status\":\"error\",\"message\":\"Database error\"}");
    }
    resp.SetContentType("application/json");
}

void SpeedTestApi::handleGetStats(const HttpRequest& req, HttpResponse& resp) {
    if (!db_) {
        resp.SetStatus(500);
        resp.SetBody(R"({"error":"Database not available"})");
        resp.SetContentType("application/json");
        return;
    }
    
    auto stats = db_->getStats();
    std::stringstream ss;
    ss << "{"
       << "\"total_tests\":" << stats.total_tests << ","
       << "\"avg_speed\":" << stats.avg_speed << ","
       << "\"max_speed\":" << stats.max_speed << ","
       << "\"active_devices\":" << stats.active_devices << ","
       << "\"total_bytes\":" << stats.total_bytes_transferred << ","
       << "\"avg_latency\":" << stats.avg_latency << ","
       << "\"avg_packet_loss\":" << stats.avg_packet_loss
       << "}";
    
    resp.SetBody(ss.str());
    resp.SetContentType("application/json");
}

void SpeedTestApi::handleGetDevices(const HttpRequest& req, HttpResponse& resp) {
    // ✅ 添加空指针检查
    if (!db_) {
        resp.SetStatus(500);
        resp.SetBody(R"({"error":"Database not available"})");
        resp.SetContentType("application/json");
        return;
    }
    
    auto devices = db_->getDevices();
    std::string json = "[";
    for (size_t i = 0; i < devices.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"device_id\":\"" + devices[i].device_id + "\","
                "\"device_name\":\"" + devices[i].device_name + "\","
                "\"last_seen\":\"" + devices[i].last_seen + "\","
                "\"total_tests\":" + std::to_string(devices[i].total_tests) + ","
                "\"avg_speed\":" + std::to_string(devices[i].avg_speed) + ","
                "\"avg_latency\":" + std::to_string(devices[i].avg_latency) + ","
                "\"avg_packet_loss\":" + std::to_string(devices[i].avg_packet_loss) + "}";
    }
    json += "]";
    resp.SetBody(json);
    resp.SetContentType("application/json");
}

void SpeedTestApi::handleGetHistory(const HttpRequest& req, HttpResponse& resp) {
    // ✅ 添加空指针检查
    if (!db_) {
        resp.SetStatus(500);
        resp.SetBody(R"({"error":"Database not available"})");
        resp.SetContentType("application/json");
        return;
    }
    
    auto records = db_->getHistory(50);
    std::string json = "[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"id\":" + std::to_string(records[i].id) + ","
                "\"device_name\":\"" + records[i].device_name + "\","
                "\"speed_mbps\":" + std::to_string(records[i].speed_mbps) + ","
                "\"created_at\":\"" + records[i].created_at + "\"}";
    }
    json += "]";
    resp.SetBody(json);
    resp.SetContentType("application/json");
}

} // namespace httpserver::application::http