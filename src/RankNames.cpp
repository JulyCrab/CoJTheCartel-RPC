#include "RankNames.h"
#include "GameFiles.h"

#include <windows.h>
#include <cctype>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

// Comes from whichever language pak is mounted (DataEs.pak, DataPl.pak, etc...)
constexpr const char* kTextsPath = "data\\MDTexts.scr";
constexpr int kRankCount = 55;

std::mutex g_mutex;
std::vector<std::string> g_police;
std::vector<std::string> g_criminal;

std::string ToUtf8(const wchar_t* text, int length) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, &result[0], size, nullptr, nullptr);
    return result;
}

// Lines look something like: String( "MP_Rank_Police_8", "Police Officer V" )
void ReadTitles(const std::wstring& texts, const wchar_t* prefix, std::vector<std::string>& out) {
    out.assign(kRankCount, std::string());
    const size_t prefixLength = wcslen(prefix);
    for (size_t at = texts.find(prefix); at != std::wstring::npos; at = texts.find(prefix, at + 1)) {
        const int level = _wtoi(texts.c_str() + at + prefixLength);
        const size_t open = texts.find(L'"', texts.find(L',', at));
        const size_t close = texts.find(L'"', open + 1);
        if (level < 0 || level >= kRankCount || open == std::wstring::npos || close == std::wstring::npos) continue;
        out[level] = ToUtf8(texts.c_str() + open + 1, static_cast<int>(close - open - 1));
    }
}

bool IsLanguagePak(const char* path) {
    const char* name = strrchr(path, '/');
    if (!name) name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    // DataEs.pak, DataPl.pak, etc, but not Data0.pak to Data2.pak
    return _strnicmp(name, "Data", 4) == 0 && isalpha(static_cast<unsigned char>(name[4])) &&
           _stricmp(name + 6, ".pak") == 0;
}

void OnGameMount(const char* path) {
    if (!IsLanguagePak(path)) return;

    std::string file;
    if (!GameFiles::Read(kTextsPath, file) || file.size() < 2) return;
    // UTF-16 with a byte order mark
    const std::wstring texts(reinterpret_cast<const wchar_t*>(file.data() + 2), (file.size() - 2) / sizeof(wchar_t));

    std::vector<std::string> police, criminal;
    ReadTitles(texts, L"\"MP_Rank_Police_", police);
    ReadTitles(texts, L"\"MP_Rank_Crim_", criminal);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_police = std::move(police);
    g_criminal = std::move(criminal);
}

}

namespace RankNames {

void Init() {
    GameFiles::AddMountListener(&OnGameMount);
}

std::string Title(bool police, int level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto& titles = police ? g_police : g_criminal;
    if (level < 0 || level >= static_cast<int>(titles.size())) return "";
    return titles[level];
}

}
