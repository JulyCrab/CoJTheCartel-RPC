#include "Log.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <memory>

namespace {
struct FileCloser {
    void operator()(FILE* f) const { if (f) std::fclose(f); }
};
std::unique_ptr<FILE, FileCloser> g_file;
std::mutex g_mutex;
}

void OpenLog(std::string_view path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    FILE* file = nullptr;
    fopen_s(&file, std::string(path).c_str(), "w");
    g_file.reset(file);
}

void Log(const char* format, ...) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fprintf(g_file.get(), "[%02u:%02u:%02u] ", time.wHour, time.wMinute, time.wSecond);
    va_list args;
    va_start(args, format);
    std::vfprintf(g_file.get(), format, args);
    va_end(args);
    std::fputc('\n', g_file.get());
    std::fflush(g_file.get());
}

