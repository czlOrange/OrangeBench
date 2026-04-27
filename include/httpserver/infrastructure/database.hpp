// include/infrastructure/database.hpp
#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include <mutex>
#include <memory>
#include <iostream>
namespace httpserver::application::http {

struct SpeedRecord {
    int id = 0;
    std::string device_id;
    std::string device_name;
    double speed_mbps = 0.0;
    double upload_speed_mbps = 0.0;   // 新增：上传速度
    int latency_ms = 0;
    double jitter_ms = 0.0;           // 新增：抖动
    double packet_loss = 0.0;
    size_t total_bytes = 0;
    double test_duration = 0.0;
    std::string created_at;
};

struct DeviceInfo {
    std::string device_id;
    std::string device_name;
    std::string user_agent;
    std::string first_seen;
    std::string last_seen;
    int total_tests = 0;
    double avg_speed = 0.0;
    int avg_latency = 0;              // 新增：平均延迟
    double avg_packet_loss = 0.0;     // 新增：平均丢包率
};

struct StatsInfo {
    int total_tests = 0;
    double avg_speed = 0.0;
    double max_speed = 0.0;
    int active_devices = 0;
    long total_bytes_transferred = 0;
    double avg_latency = 0.0;         // 新增：平均延迟
    double avg_packet_loss = 0.0;     // 新增：平均丢包率
};

class Database {
public:
    Database();
    ~Database();
    
    bool init();
    bool saveSpeedRecord(const SpeedRecord& record);
    std::vector<SpeedRecord> getHistory(int limit = 100);
    StatsInfo getStats();
    std::vector<DeviceInfo> getDevices();
    
private:
    sqlite3* db_;
    mutable std::recursive_mutex db_mutex_;  // 改为递归互斥锁
    bool executeSQL(const std::string& sql);
    bool createTables();
};

} // namespace httpserver::application::http