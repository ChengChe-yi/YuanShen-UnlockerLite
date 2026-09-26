#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <atomic>

namespace Logger
{

    extern std::atomic<bool> g_logWriteEnabled;

    void InitLogFile();

    void WriteLog(const char* text);

    void CloseLog();
}

#ifdef _DEBUG

#define LOG(tag, fmt, ...)                                                          \
    do {                                                                            \
        SYSTEMTIME _st;                                                             \
        GetLocalTime(&_st);                                                         \
        char _fmt[1024];                                                            \
        sprintf_s(_fmt, sizeof(_fmt), "[%%02d:%%02d:%%02d.%%03d][%s] %s\n",         \
            (tag), (fmt));                                                          \
        char _buf[1024];                                                            \
        int _len = sprintf_s(_buf, sizeof(_buf), _fmt,                              \
            _st.wHour, _st.wMinute, _st.wSecond, _st.wMilliseconds,                 \
            __VA_ARGS__);                                                           \
        if (_len > 0) ::Logger::WriteLog(_buf);                                     \
    } while (0)

#define LOG_MSG(tag, msg)                                                           \
    do {                                                                            \
        SYSTEMTIME _st;                                                             \
        GetLocalTime(&_st);                                                         \
        char _fmt[1024];                                                            \
        sprintf_s(_fmt, sizeof(_fmt), "[%%02d:%%02d:%%02d.%%03d][%s] %s\n",         \
            (tag), (msg));                                                          \
        char _buf[1024];                                                            \
        int _len = sprintf_s(_buf, sizeof(_buf), _fmt,                              \
            _st.wHour, _st.wMinute, _st.wSecond, _st.wMilliseconds);                \
        if (_len > 0) ::Logger::WriteLog(_buf);                                     \
    } while (0)

#else

#define LOG(tag, fmt, ...) ((void)0)
#define LOG_MSG(tag, msg)  ((void)0)

#endif
