#ifndef SQLOG_HPP
#define SQLOG_HPP

#include <iostream>
#include <filesystem>
#include <mutex>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include "nlohmann/json.hpp"
#include "SQLog/engine.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

enum SQLogError {
    SQLOG_FILE_NOT_FOUND,
    SQLOG_FOLDER_NOT_FOUND,
    SQLOG_ERROR_TYPE,
    SQLOG_NOT_OPEN,
    SQLOG_SUCCESS
};

// ใช้ inline เพื่อไม่ให้ error "multiple definition" ตอน include หลายไฟล์ (C++17+)
inline fs::path     LogPath;
inline bool         SQLog_SaveLastLog   = false;   // true = เก็บไฟล์เก่าไว้ใน archive/ ตอนเต็ม, false = ล้างทิ้ง
inline double       SQLog_MaxLogSizeKB  = 4000;    // ขนาดสูงสุดต่อไฟล์ (KB)
inline std::string  SQLog_MainLogName   = "main.log";
inline bool         Loaded              = false;
inline bool         IsOpen_Log          = false;
inline std::string  IsOpen_LogName      = "";
inline std::mutex   SQLog_Mutex;

inline const std::string SQLog_InitLogsJson = R"({
    "MainLogsFile": "main.log",
    "SaveLastLog": false,
    "LogSaveInit": {},
    "MaxLogSize": 4000
}
)";

// ---------------------------------------------------------------- utils

inline void createFile(const fs::path& target, const std::string& data) {
    std::ofstream file(target, std::ios::trunc);
    file << data;
}

inline std::string getCurrentTime(const char* fmt = "%Y-%m-%dT%H:%M:%S") {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);   // thread-safe (std::localtime ไม่ thread-safe)
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, fmt);
    return oss.str();
}

inline double getFileSizeKB(const fs::path& target) {
    std::error_code ec;
    if (!fs::exists(target, ec)) return 0.0;
    return static_cast<double>(fs::file_size(target, ec)) / 1024.0;
}

// escape " \ และขึ้นบรรทัดใหม่ เพื่อให้ 1 log = 1 บรรทัดเสมอ
inline std::string escapeContent(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------- rotation

// ถ้าไฟล์ log เต็ม -> เก็บไปไว้ archive/ (ถ้า SaveLastLog) แล้วเริ่มไฟล์ใหม่
// เรียกภายใต้ lock เท่านั้น
inline void rotateIfFull() {
    fs::path current = LogPath / IsOpen_LogName;
    if (getFileSizeKB(current) < SQLog_MaxLogSizeKB) return;

    if (SQLog_SaveLastLog) {
        fs::path archiveDir = LogPath / "archive";
        std::error_code ec;
        fs::create_directories(archiveDir, ec);

        fs::path name = current.stem();
        std::string archived = name.string() + "_" + getCurrentTime("%Y%m%d_%H%M%S")
                             + current.extension().string();
        fs::rename(current, archiveDir / archived, ec);
        if (ec) {
            std::cerr << "[SQLog] archive failed: " << ec.message() << "\n";
        }
    }
    createFile(current, "");   // สร้างใหม่ / ล้างไฟล์
}

// ---------------------------------------------------------------- core API
inline std::string sanitizeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            if (out.empty() || out.back() != '_') out += '_';   // ยุบ space ติดกันเป็น _ ตัวเดียว
        } else if (c == '[') {
            out += '(';
        } else if (c == ']') {
            out += ')';
        } else {
            out += static_cast<char>(c);
        }
    }
    // ตัด _ หัวท้าย
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back()  == '_') out.pop_back();
    if (out.empty()) out = "-";
    return out;
}
// รูปแบบ: [Tag] [time] [config] "content"
inline int append(const std::string& tag,
                  const std::string& config,
                  const std::string& content) {
    std::lock_guard<std::mutex> lock(SQLog_Mutex);

    if (!Loaded || !IsOpen_Log) return SQLogError::SQLOG_NOT_OPEN;

    rotateIfFull();

    std::ofstream file(LogPath / IsOpen_LogName, std::ios::app);
    if (!file) return SQLogError::SQLOG_FILE_NOT_FOUND;

    file << "[" << sanitizeField(tag) << "] "
     << "[" << sanitizeField(getCurrentTime("%Y-%m-%dT%H:%M:%S")) << "] "
     << "[" << sanitizeField(config) << "] "
     << "\"" << escapeContent(content) << "\"\n";

    return SQLogError::SQLOG_SUCCESS;
}

// shortcut ตาม tag ที่ใช้บ่อย
inline int logInfo (const std::string& config, const std::string& msg) { return append("INFO",  config, msg); }
inline int logWarn (const std::string& config, const std::string& msg) { return append("WARN",  config, msg); }
inline int logError(const std::string& config, const std::string& msg) { return append("ERROR", config, msg); }
inline int logDebug(const std::string& config, const std::string& msg) { return append("DEBUG", config, msg); }

// ---------------------------------------------------------------- profile / init

inline int createNewLog(const std::string& LogName) {
    IsOpen_Log = false;
    createFile(LogPath / LogName, "");
    IsOpen_Log = true;
    IsOpen_LogName = LogName;
    return SQLogError::SQLOG_SUCCESS;
}

inline int loadProfile(const std::string& LogName) {
    IsOpen_Log = false;
    fs::path p = LogPath / LogName;
    if (!fs::exists(p)) return SQLogError::SQLOG_FILE_NOT_FOUND;
    if (fs::is_directory(p)) return SQLogError::SQLOG_ERROR_TYPE;
    IsOpen_Log = true;
    IsOpen_LogName = LogName;
    return SQLogError::SQLOG_SUCCESS;
}

inline fs::path getLogPath() {
    return fs::canonical(fs::absolute(LogPath/IsOpen_LogName));
}

inline int LoadLogs(const fs::path& target) {
    if (!fs::exists(target)) return SQLogError::SQLOG_FOLDER_NOT_FOUND;
    if (!fs::is_directory(target)) return SQLogError::SQLOG_ERROR_TYPE;

    LogPath = target;

    if (!fs::exists(LogPath / "init.json")) {
        createFile(LogPath / "init.json", SQLog_InitLogsJson);
    }

    // อ่านค่า config จาก init.json
    try {
        std::ifstream in(LogPath / "init.json");
        json j = json::parse(in);
        SQLog_MainLogName  = j.value("MainLogsFile", "main.log");
        SQLog_SaveLastLog  = j.value("SaveLastLog", false);
        SQLog_MaxLogSizeKB = j.value("MaxLogSize", 4000.0);
    } catch (const std::exception& e) {
        std::cerr << "[SQLog] init.json parse error: " << e.what() << " (default)\n";
    }

    // เปิดไฟล์ main log (ถ้ายังไม่มีก็สร้าง)
    if (fs::exists(LogPath / SQLog_MainLogName)) {
        int r = loadProfile(SQLog_MainLogName);
        if (r != SQLogError::SQLOG_SUCCESS) return r;
    } else {
        createNewLog(SQLog_MainLogName);
    }

    Loaded = true;
    return SQLogError::SQLOG_SUCCESS;
}

#endif