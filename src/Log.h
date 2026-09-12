#pragma once

#include <string>
#include <string_view>

void OpenLog(std::string_view path);
void Log(const char* format, ...);

