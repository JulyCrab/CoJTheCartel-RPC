#include "Log.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <share.h>
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
    // Keep the previous session's log so an accidental relaunch doesn't wipe it
    const std::string file_path(path);
    const std::string previous = file_path.substr(0, file_path.find_last_of('.')) + ".prev.log";
    MoveFileExA(file_path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);

    // fopen_s would lock the file, this keeps it readable while the game runs
    g_file.reset(_fsopen(file_path.c_str(), "w", _SH_DENYWR));
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

