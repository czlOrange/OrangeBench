// src/infrastructure/database.cpp
#include "infrastructure/database.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace httpserver::application::http {

Database::Database() : db_(nullptr) {}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
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
        "upload_speed_mbps REAL DEFAULT 0,"
        "latency_ms INTEGER DEFAULT 0,"
        "jitter_ms REAL DEFAULT 0,"
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
        "avg_speed REAL DEFAULT 0,"
        "avg_latency INTEGER DEFAULT 0,"
        "avg_packet_loss REAL DEFAULT 0"
        ");";

    return executeSQL(createRecords) && executeSQL(createDevices);
}

bool Database::init() {
    if (sqlite3_open("speedtest.db", &db_) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    // 兼容旧表，添加可能缺失的列（忽略错误）
    const char* alterQueries[] = {
        "ALTER TABLE speed_records ADD COLUMN upload_speed_mbps REAL DEFAULT 0",
        "ALTER TABLE speed_records ADD COLUMN jitter_ms REAL DEFAULT 0",
        "ALTER TABLE devices ADD COLUMN avg_latency INTEGER DEFAULT 0",
        "ALTER TABLE devices ADD COLUMN avg_packet_loss REAL DEFAULT 0"
    };
    for (const char* sql : alterQueries) {
        sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    }

    return createTables();
}

bool Database::saveSpeedRecord(const SpeedRecord& record) {
    // 使用参数化查询避免 SQL 注入
    const char* insertSQL = "INSERT INTO speed_records "
        "(device_id, device_name, speed_mbps, upload_speed_mbps, latency_ms, jitter_ms, packet_loss, total_bytes, test_duration) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    
    // 只在执行 SQL 时加锁
    {
        //std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        
        if (sqlite3_prepare_v2(db_, insertSQL, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Failed to prepare insert statement: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, record.device_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, record.device_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 3, record.speed_mbps);
        sqlite3_bind_double(stmt, 4, record.upload_speed_mbps);
        sqlite3_bind_int(stmt, 5, record.latency_ms);
        sqlite3_bind_double(stmt, 6, record.jitter_ms);
        sqlite3_bind_double(stmt, 7, record.packet_loss);
        sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(record.total_bytes));
        sqlite3_bind_double(stmt, 9, record.test_duration);

        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    
    if (!ok) return false;

    // 更新设备表（使用 UPSERT）
    const char* upsertSQL = R"(
        INSERT INTO devices (device_id, device_name, total_tests, avg_speed, avg_latency, avg_packet_loss)
        VALUES (?, ?, 1, ?, ?, ?)
        ON CONFLICT(device_id) DO UPDATE SET
            device_name = excluded.device_name,
            last_seen = CURRENT_TIMESTAMP,
            total_tests = total_tests + 1,
            avg_speed = (avg_speed * (total_tests - 1) + excluded.avg_speed) / total_tests,
            avg_latency = (avg_latency * (total_tests - 1) + excluded.avg_latency) / total_tests,
            avg_packet_loss = (avg_packet_loss * (total_tests - 1) + excluded.avg_packet_loss) / total_tests
    )";

    sqlite3_stmt* stmt2 = nullptr;
    
    // 只在执行 SQL 时加锁
    {
        //std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        
        if (sqlite3_prepare_v2(db_, upsertSQL, -1, &stmt2, nullptr) != SQLITE_OK) {
            std::cerr << "Failed to prepare upsert statement: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        sqlite3_bind_text(stmt2, 1, record.device_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt2, 2, record.device_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt2, 3, record.speed_mbps);
        sqlite3_bind_int(stmt2, 4, record.latency_ms);
        sqlite3_bind_double(stmt2, 5, record.packet_loss);

        ok = (sqlite3_step(stmt2) == SQLITE_DONE);
        sqlite3_finalize(stmt2);
    }

    return ok;
}

// 安全转换辅助函数
static int safe_stoi(const char* str) {
    if (!str) return 0;
    try { return std::stoi(str); } catch (...) { return 0; }
}
static long safe_stol(const char* str) {
    if (!str) return 0;
    try { return std::stol(str); } catch (...) { return 0; }
}
static double safe_stod(const char* str) {
    if (!str) return 0.0;
    try { return std::stod(str); } catch (...) { return 0.0; }
}

static int recordCallback(void* data, int argc, char** argv, char**) {
    auto* vec = static_cast<std::vector<SpeedRecord>*>(data);
    if (!vec || argc < 11) return 0;
    SpeedRecord r;
    r.id = safe_stoi(argv[0]);
    r.device_id = argv[1] ? argv[1] : "";
    r.device_name = argv[2] ? argv[2] : "";
    r.speed_mbps = safe_stod(argv[3]);
    r.upload_speed_mbps = safe_stod(argv[4]);
    r.latency_ms = safe_stoi(argv[5]);
    r.jitter_ms = safe_stod(argv[6]);
    r.packet_loss = safe_stod(argv[7]);
    r.total_bytes = safe_stol(argv[8]);
    r.test_duration = safe_stod(argv[9]);
    r.created_at = argv[10] ? argv[10] : "";
    vec->push_back(r);
    return 0;
}

std::vector<SpeedRecord> Database::getHistory(int limit) {
    std::vector<SpeedRecord> records;
    std::string sql = "SELECT id, device_id, device_name, speed_mbps, upload_speed_mbps, "
                      "latency_ms, jitter_ms, packet_loss, total_bytes, test_duration, created_at "
                      "FROM speed_records ORDER BY id DESC LIMIT " + std::to_string(limit);
    
    char* err = nullptr;
    
    // 只在执行 SQL 时加锁
    {
        //std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        if (sqlite3_exec(db_, sql.c_str(), recordCallback, &records, &err) != SQLITE_OK) {
            std::cerr << "getHistory error: " << err << std::endl;
            sqlite3_free(err);
        }
    }
    
    return records;
}

static int statsCallback(void* data, int argc, char** argv, char**) {
    auto* stats = static_cast<StatsInfo*>(data);
    if (!stats) return 0;
    
    if (argc >= 5) {
        stats->total_tests = safe_stoi(argv[0]);
        stats->avg_speed = safe_stod(argv[1]);
        stats->max_speed = safe_stod(argv[2]);
        stats->active_devices = safe_stoi(argv[3]);
        stats->total_bytes_transferred = safe_stol(argv[4]);
    }
    if (argc >= 7) {
        stats->avg_latency = safe_stod(argv[5]);
        stats->avg_packet_loss = safe_stod(argv[6]);
    }
    
    return 0;
}

StatsInfo Database::getStats() {
    StatsInfo stats = {0, 0, 0, 0, 0, 0, 0};
    std::string sql = "SELECT COUNT(*), AVG(speed_mbps), MAX(speed_mbps), "
                      "COUNT(DISTINCT device_id), SUM(total_bytes), "
                      "AVG(latency_ms), AVG(packet_loss) FROM speed_records";
    
    char* err = nullptr;
    
    // 只在执行 SQL 时加锁
    {
        //std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        if (sqlite3_exec(db_, sql.c_str(), statsCallback, &stats, &err) != SQLITE_OK) {
            std::cerr << "SQL error: " << err << std::endl;
            sqlite3_free(err);
        }
    }
    
    return stats;
}

static int deviceCallback(void* data, int argc, char** argv, char**) {
    auto* vec = static_cast<std::vector<DeviceInfo>*>(data);
    if (!vec || argc < 9) return 0;
    DeviceInfo d;
    d.device_id = argv[0] ? argv[0] : "";
    d.device_name = argv[1] ? argv[1] : "";
    d.user_agent = argv[2] ? argv[2] : "";
    d.first_seen = argv[3] ? argv[3] : "";
    d.last_seen = argv[4] ? argv[4] : "";
    d.total_tests = safe_stoi(argv[5]);
    d.avg_speed = safe_stod(argv[6]);
    d.avg_latency = safe_stoi(argv[7]);
    d.avg_packet_loss = safe_stod(argv[8]);
    vec->push_back(d);
    return 0;
}

std::vector<DeviceInfo> Database::getDevices() {
    std::vector<DeviceInfo> devices;
    std::string sql = "SELECT device_id, device_name, user_agent, first_seen, last_seen, "
                      "total_tests, avg_speed, avg_latency, avg_packet_loss "
                      "FROM devices ORDER BY last_seen DESC";
    
    char* err = nullptr;
    
    // 只在执行 SQL 时加锁
    {
        //std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        if (sqlite3_exec(db_, sql.c_str(), deviceCallback, &devices, &err) != SQLITE_OK) {
            std::cerr << "getDevices error: " << err << std::endl;
            sqlite3_free(err);
        }
    }
    
    return devices;
}

} // namespace httpserver::application::http