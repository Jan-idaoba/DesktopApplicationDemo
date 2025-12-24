#pragma once
#include <string>

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

class Logger
{
public:
    static void Init(
        const std::string& logDir = "logs",
        LogLevel level = LogLevel::Info,
        bool enableConsole = true
    );

    static void Shutdown();

    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();

    template<typename... Args>
    static void Log(
        LogLevel level,
        const char* file,
        int line,
        const char* func,
        const char* fmt,
        Args&&... args
    );

private:
    template<typename... Args>
    static void LogInternal(
        LogLevel level,
        const char* file,
        int line,
        const char* func,
        const char* fmt,
        Args&&... args
    );
};
