#include "Logger.h"
#include "LogConfig.h"

#if ENABLE_LOG

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#if ENABLE_ASYNC_LOG
#include <spdlog/async.h>
#endif

static std::shared_ptr<spdlog::logger> g_logger;

static spdlog::level::level_enum ToSpd(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:     
        return spdlog::level::trace;
    case LogLevel::Debug:     
        return spdlog::level::debug;
    case LogLevel::Info:      
        return spdlog::level::info;
    case LogLevel::Warn:      
        return spdlog::level::warn;
    case LogLevel::Error:     
        return spdlog::level::err;
    case LogLevel::Critical:  
        return spdlog::level::critical;
    default:                  
        return spdlog::level::off;
    }
}

void Logger::Init(const std::string& logDir, LogLevel level, bool enableConsole)
{
    if (g_logger)
        return;

#if ENABLE_ASYNC_LOG
    spdlog::init_thread_pool(8192, 1);
#endif

    std::vector<spdlog::sink_ptr> sinks;

    if (enableConsole)
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logDir + "/app.log", 5 * 1024 * 1024, 3));

#if ENABLE_ASYNC_LOG
    g_logger = std::make_shared<spdlog::async_logger>(
        "app", sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);
#else
    g_logger = std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());
#endif

    spdlog::register_logger(g_logger);

    g_logger->set_level(ToSpd(level));
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    g_logger->flush_on(spdlog::level::warn);
}

void Logger::Shutdown()
{
    spdlog::shutdown();
    g_logger.reset();
}

void Logger::SetLevel(LogLevel level)
{
    if (g_logger)
        g_logger->set_level(ToSpd(level));
}

LogLevel Logger::GetLevel()
{
    if (!g_logger)
        return LogLevel::Off;
    return static_cast<LogLevel>(g_logger->level());
}

template<typename... Args>
void Logger::LogInternal(
    LogLevel level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    Args&&... args)
{
    if (!g_logger)
        return;

    auto spdLevel = ToSpd(level);
    if (!g_logger->should_log(spdLevel))
        return;

    g_logger->log(
        spdlog::source_loc{ file, line, func },
        spdLevel,
        fmt,
        std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::Log(
    LogLevel level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    Args&&... args)
{
    LogInternal(level, file, line, func, fmt, std::forward<Args>(args)...);
}

template void Logger::Log<>(LogLevel, const char*, int, const char*, const char*);

#else
void Logger::Init(const std::string&, LogLevel, bool) {}
void Logger::Shutdown() {}
void Logger::SetLevel(LogLevel) {}
LogLevel Logger::GetLevel() { return LogLevel::Off; }
#endif
