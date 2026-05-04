#ifndef COMMON_LOGGER_H
#define COMMON_LOGGER_H

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <sstream>
#include <vector>
#include <cstdarg>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    // format: "text" | "json"
    void init(const std::string& log_file = "logs/seckill.log",
              const std::string& level    = "INFO",
              const std::string& format   = "text");

    void debug(const std::string& msg, const std::string& trace_id = "");
    void info (const std::string& msg, const std::string& trace_id = "");
    void warn (const std::string& msg, const std::string& trace_id = "");
    void error(const std::string& msg, const std::string& trace_id = "");

    /**
     * 热更新日志级别（可被 SIGHUP 信号触发）
     * @param level "DEBUG" | "INFO" | "WARN" | "ERROR"
     */
    void set_level(const std::string& level);

    void log(LogLevel level, const std::string& msg, const std::string& trace_id = "");

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string level_to_string(LogLevel level);
    std::string get_timestamp();
    std::string get_timestamp_iso();
    LogLevel    parse_level(const std::string& level);

    // 转义 JSON 字符串中的特殊字符
    std::string json_escape(const std::string& s);

    std::mutex  mutex_;
    std::string log_file_;
    std::atomic<LogLevel> min_level_{LogLevel::INFO};  // atomic: 支持热更新
    bool        json_format_ = false;
};

#endif // COMMON_LOGGER_H

