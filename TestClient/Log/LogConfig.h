#pragma once

#ifndef ENABLE_LOG
#define ENABLE_LOG 1
#endif

#ifndef ENABLE_ASYNC_LOG
#define ENABLE_ASYNC_LOG 0
#endif

#if defined(_DEBUG)
#define DEFAULT_LOG_LEVEL LogLevel::Debug
#else
#define DEFAULT_LOG_LEVEL LogLevel::Info
#endif
