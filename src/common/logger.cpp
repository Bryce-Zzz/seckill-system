#include "common/logger.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <sstream>

void Logger::init(const std::string& log_file,
                  const std::string& level,
                  const std::string& format) {
    log_file_    = log_file;
    min_level_.store(parse_level(level));
    json_format_ = (format == "json");

    // 创建日志目录
    size_t pos = log_file_.find_last_of('/');
    if (pos != std::string::npos) {
        std::string dir = log_file_.substr(0, pos);
        mkdir(dir.c_str(), 0755);
    }
}

void Logger::set_level(const std::string& level) {
    min_level_.store(parse_level(level));
}

void Logger::debug(const std::string& msg, const std::string& trace_id) { log(LogLevel::DEBUG, msg, trace_id); }
void Logger::info (const std::string& msg, const std::string& trace_id) { log(LogLevel::INFO,  msg, trace_id); }
void Logger::warn (const std::string& msg, const std::string& trace_id) { log(LogLevel::WARN,  msg, trace_id); }
void Logger::error(const std::string& msg, const std::string& trace_id) { log(LogLevel::ERROR, msg, trace_id); }

void Logger::log(LogLevel level, const std::string& msg, const std::string& trace_id) {
    if (level < min_level_.load()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    std::string log_line;

    if (json_format_) {
        // JSON 格式：{ "ts": "...", "level": "INFO", "trace_id": "...", "msg": "..." }
        std::ostringstream oss;
        oss << "{\"ts\":\"" << get_timestamp_iso() << "\""
            << ",\"level\":\"" << level_to_string(level) << "\""
            << ",\"trace_id\":\"" << json_escape(trace_id) << "\""
            << ",\"msg\":\"" << json_escape(msg) << "\"}\n";
        log_line = oss.str();
    } else {
        // 传统文本格式
        std::string ts       = get_timestamp();
        std::string level_str = level_to_string(level);
        log_line = "[" + ts + "] [" + level_str + "] ";
        if (!trace_id.empty()) log_line += "[" + trace_id + "] ";
        log_line += msg + "\n";
    }

    // 输出到控制台
    std::cout << log_line;

    // 输出到文件
    std::ofstream ofs(log_file_, std::ios::app);
    if (ofs.is_open()) {
        ofs << log_line;
        ofs.close();
    }
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

std::string Logger::get_timestamp() {
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

std::string Logger::get_timestamp_iso() {
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    return std::string(buf);
}

LogLevel Logger::parse_level(const std::string& level) {
    if (level == "DEBUG" || level == "debug") return LogLevel::DEBUG;
    if (level == "INFO"  || level == "info")  return LogLevel::INFO;
    if (level == "WARN"  || level == "warn")  return LogLevel::WARN;
    if (level == "ERROR" || level == "error") return LogLevel::ERROR;
    return LogLevel::INFO;
}

std::string Logger::json_escape(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:   oss << c;      break;
        }
    }
    return oss.str();
}
