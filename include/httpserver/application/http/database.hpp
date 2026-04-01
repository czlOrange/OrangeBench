#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>

namespace httpserver::application::http {

struct SpeedRecord {
    int id;
    std::string device_id;
    std::string device_name;
    double speed_mbps;
    int latency_ms;
    double packet_loss;
    long total_bytes;
    double test_duration;
    std::string created_at;
};

struct DeviceInfo {
    std::string device_id;
    std::string device_name;        
    std::string user_agent;
    std::string first_seen;
    std::string last_seen;
    int total_tests;
    double avg_speed;
};

struct StatsInfo {
    int total_tests;
    double avg_speed;
    double max_speed;
    int active_devices;
    long total_bytes_transferred;
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
    bool executeSQL(const std::string& sql);
    bool createTables();
};

} // namespace httpserver::application::http
