#include "infrastructure/database.hpp"
#include <iostream>
#include <sstream>

namespace httpserver::application::http {

Database::Database() : db_(nullptr) {}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

bool Database::executeSQL(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::createTables() {
    std::string createRecords = 
        "CREATE TABLE IF NOT EXISTS speed_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device_id TEXT NOT NULL,"
        "device_name TEXT,"
        "speed_mbps REAL NOT NULL,"
        "latency_ms INTEGER DEFAULT 0,"
        "packet_loss REAL DEFAULT 0,"
        "total_bytes INTEGER DEFAULT 0,"
        "test_duration REAL DEFAULT 0,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    
    std::string createDevices = 
        "CREATE TABLE IF NOT EXISTS devices ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device_id TEXT UNIQUE NOT NULL,"
        "device_name TEXT,"
        "user_agent TEXT,"
        "first_seen DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "last_seen DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "total_tests INTEGER DEFAULT 0,"
        "avg_speed REAL DEFAULT 0"
        ");";
    
    return executeSQL(createRecords) && executeSQL(createDevices);
}

bool Database::init() {
    if (sqlite3_open("speedtest.db", &db_) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return createTables();
}

bool Database::saveSpeedRecord(const SpeedRecord& record) {
    std::stringstream ss;
    ss << "INSERT INTO speed_records (device_id, device_name, speed_mbps, latency_ms, "
       << "packet_loss, total_bytes, test_duration) VALUES ("
       << "'" << record.device_id << "',"
       << "'" << record.device_name << "',"
       << record.speed_mbps << ","
       << record.latency_ms << ","
       << record.packet_loss << ","
       << record.total_bytes << ","
       << record.test_duration << ");";
    if (!executeSQL(ss.str())) return false;
    
    std::stringstream update;
    update << "INSERT INTO devices (device_id, device_name, total_tests, avg_speed) VALUES ("
           << "'" << record.device_id << "',"
           << "'" << record.device_name << "',1," << record.speed_mbps << ") "
           << "ON CONFLICT(device_id) DO UPDATE SET "
           << "device_name = excluded.device_name,"
           << "last_seen = CURRENT_TIMESTAMP,"
           << "total_tests = total_tests + 1,"
           << "avg_speed = (avg_speed * (total_tests - 1) + " << record.speed_mbps << ") / total_tests;";
    return executeSQL(update.str());
}

static int recordCallback(void* data, int argc, char** argv, char**) {
    auto* vec = static_cast<std::vector<SpeedRecord>*>(data);
    if (vec && argc >= 9) {
        SpeedRecord r;
        r.id = argv[0] ? std::stoi(argv[0]) : 0;
        r.device_id = argv[1] ? argv[1] : "";
        r.device_name = argv[2] ? argv[2] : "";
        r.speed_mbps = argv[3] ? std::stod(argv[3]) : 0;
        r.latency_ms = argv[4] ? std::stoi(argv[4]) : 0;
        r.packet_loss = argv[5] ? std::stod(argv[5]) : 0;
        r.total_bytes = argv[6] ? std::stol(argv[6]) : 0;
        r.test_duration = argv[7] ? std::stod(argv[7]) : 0;
        r.created_at = argv[8] ? argv[8] : "";
        vec->push_back(r);
    }
    return 0;
}

std::vector<SpeedRecord> Database::getHistory(int limit) {
    std::vector<SpeedRecord> records;
    std::string sql = "SELECT * FROM speed_records ORDER BY id DESC LIMIT " + std::to_string(limit);
    char* err = nullptr;
    sqlite3_exec(db_, sql.c_str(), recordCallback, &records, &err);
    return records;
}

static int statsCallback(void* data, int argc, char** argv, char**) {
    auto* stats = static_cast<StatsInfo*>(data);
    if (stats && argc >= 5) {
        stats->total_tests = argv[0] ? std::stoi(argv[0]) : 0;
        stats->avg_speed = argv[1] ? std::stod(argv[1]) : 0;
        stats->max_speed = argv[2] ? std::stod(argv[2]) : 0;
        stats->active_devices = argv[3] ? std::stoi(argv[3]) : 0;
        stats->total_bytes_transferred = argv[4] ? std::stol(argv[4]) : 0;
    }
    return 0;
}

StatsInfo Database::getStats() {
    StatsInfo stats = {0,0,0,0,0};
    std::string sql = "SELECT COUNT(*), AVG(speed_mbps), MAX(speed_mbps), "
                      "COUNT(DISTINCT device_id), SUM(total_bytes) FROM speed_records";
    char* err = nullptr;
    sqlite3_exec(db_, sql.c_str(), statsCallback, &stats, &err);
    return stats;
}

static int deviceCallback(void* data, int argc, char** argv, char**) {
    auto* vec = static_cast<std::vector<DeviceInfo>*>(data);
    if (vec && argc >= 7) {
        DeviceInfo d;
        d.device_id = argv[0] ? argv[0] : "";
        d.device_name = argv[1] ? argv[1] : "";
        d.user_agent = argv[2] ? argv[2] : "";
        d.first_seen = argv[3] ? argv[3] : "";
        d.last_seen = argv[4] ? argv[4] : "";
        d.total_tests = argv[5] ? std::stoi(argv[5]) : 0;
        d.avg_speed = argv[6] ? std::stod(argv[6]) : 0;
        vec->push_back(d);
    }
    return 0;
}

std::vector<DeviceInfo> Database::getDevices() {
    std::vector<DeviceInfo> devices;
    std::string sql = "SELECT device_id, device_name, user_agent, first_seen, last_seen, total_tests, avg_speed "
                      "FROM devices ORDER BY last_seen DESC";
    char* err = nullptr;
    sqlite3_exec(db_, sql.c_str(), deviceCallback, &devices, &err);
    return devices;
}

} // namespace httpserver::application::http
