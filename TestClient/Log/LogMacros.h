#pragma once
#include "Logger.h"
#include "LogConfig.h"

#if ENABLE_LOG

#define LOG_TRACE(fmt, ...) \
    Logger::Log(LogLevel::Trace, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
    Logger::Log(LogLevel::Debug, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    Logger::Log(LogLevel::Info, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    Logger::Log(LogLevel::Warn, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    Logger::Log(LogLevel::Error, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_CRITICAL(fmt, ...) \
    Logger::Log(LogLevel::Critical, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#else
#define LOG_TRACE(...)     ((void)0)
#define LOG_DEBUG(...)     ((void)0)
#define LOG_INFO(...)      ((void)0)
#define LOG_WARN(...)      ((void)0)
#define LOG_ERROR(...)     ((void)0)
#define LOG_CRITICAL(...)  ((void)0)
#endif
