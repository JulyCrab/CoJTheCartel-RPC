#include "SteamLobby.h"
#include "Log.h"

#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <fstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

// Same layout as CCallbackBase in the Steamworks SDK the game ships with
// MSVC orders the vtable the way steam_api.dll expects as long as the declaration order matches
class CCallbackBase {
public:
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool ioFailure, std::uint64_t apiCall) = 0;
    virtual int GetCallbackSizeBytes() = 0;

protected:
    std::uint8_t callbackFlags_ = 0;
    int callbackId_ = 0;
};

#pragma pack(push, 8)
struct LobbyEnter_t {
    static constexpr int kCallbackId = 504;
    std::uint64_t steamIdLobby;
    std::uint32_t chatPermissions;
    bool locked;
    std::uint32_t chatRoomEnterResponse; // 1 = success
};

struct LobbyCreated_t {
    static constexpr int kCallbackId = 513;
    int result; // 1 = k_EResultOK
    std::uint64_t steamIdLobby;
};
#pragma pack(pop)

std::atomic<std::uint64_t> g_lobbyId{0};

template <typename T, bool (*Succeeded)(const T&)>
class LobbyCallback final : public CCallbackBase {
public:
    LobbyCallback() { callbackId_ = T::kCallbackId; }

    void Run(void* param) override {
        const auto* data = static_cast<const T*>(param);
        if (data && Succeeded(*data)) g_lobbyId = data->steamIdLobby;
    }
    void Run(void* param, bool, std::uint64_t) override { Run(param); }
    int GetCallbackSizeBytes() override { return sizeof(T); }
};

bool Entered(const LobbyEnter_t& data) { return data.chatRoomEnterResponse == 1; }
bool Created(const LobbyCreated_t& data) { return data.result == 1; }

LobbyCallback<LobbyEnter_t, Entered> g_lobbyEnter;
LobbyCallback<LobbyCreated_t, Created> g_lobbyCreated;
bool g_registered = false;

std::string ReadAppId() {
    char buf[32]{};
    if (GetEnvironmentVariableA("SteamAppId", buf, sizeof(buf)) && buf[0]) return buf;

    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string file = path;
    const size_t slash = file.find_last_of("\\/");
    file = (slash == std::string::npos ? std::string(".") : file.substr(0, slash)) + "\\steam_appid.txt";

    std::ifstream in(file);
    std::string id;
    in >> id;
    return id;
}

bool IsDigits(const std::string& s) {
    if (s.empty() || s.size() > 20) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// Steam keeps the logged in account id in the registry, SteamID64 = base + account id
std::string LocalSteamId() {
    DWORD accountId = 0;
    DWORD size = sizeof(accountId);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", "ActiveUser", RRF_RT_REG_DWORD,
                     nullptr, &accountId, &size) != ERROR_SUCCESS || !accountId) {
        return "";
    }
    return std::to_string(76561197960265728ULL + accountId);
}

}

namespace SteamLobby {

void Update() {
    if (g_registered) return;
    HMODULE steam = GetModuleHandleA("steam_api.dll");
    if (!steam) return;
    g_registered = true;

    using RegisterCallback_t = void(__cdecl*)(CCallbackBase*, int);
    auto registerCallback = reinterpret_cast<RegisterCallback_t>(GetProcAddress(steam, "SteamAPI_RegisterCallback"));
    if (!registerCallback) {
        Log("SteamAPI_RegisterCallback not found, joining disabled.");
        return;
    }
    registerCallback(&g_lobbyEnter, LobbyEnter_t::kCallbackId);
    registerCallback(&g_lobbyCreated, LobbyCreated_t::kCallbackId);
}

// What the official discord rpc library does in Discord_RegisterSteamGame
void RegisterDiscordLaunch(const std::string& discordAppId) {
    if (discordAppId.empty() || !IsDigits(AppId())) return;

    char steamExe[MAX_PATH]{};
    DWORD size = sizeof(steamExe);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamExe", RRF_RT_REG_SZ, nullptr, steamExe, &size) != ERROR_SUCCESS) {
        return;
    }
    for (char* c = steamExe; *c; ++c) {
        if (*c == '/') *c = '\\';
    }

    const std::string key = "Software\\Classes\\discord-" + discordAppId;
    const std::string description = "URL:Run game " + discordAppId + " protocol";
    const std::string command = "\"" + std::string(steamExe) + "\" steam://rungameid/" + AppId();

    HKEY root = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExA(root, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(description.c_str()), static_cast<DWORD>(description.size() + 1));
    RegSetValueExA(root, "URL Protocol", 0, REG_SZ, reinterpret_cast<const BYTE*>(""), 1);
    RegCloseKey(root);

    HKEY cmd = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, (key + "\\shell\\open\\command").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &cmd, nullptr) == ERROR_SUCCESS) {
        RegSetValueExA(cmd, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>(command.size() + 1));
        RegCloseKey(cmd);
    }
}

std::uint64_t CurrentLobby() {
    return g_lobbyId;
}

void ClearLobby() {
    g_lobbyId = 0;
}

const std::string& AppId() {
    static const std::string appId = ReadAppId();
    return appId;
}

std::string MakeJoinLink(const std::string& pageUrl, std::uint64_t lobbyId) {
    if (pageUrl.empty() || !lobbyId) return "";
    std::string url = pageUrl + "?lobby=" + std::to_string(lobbyId);
    const std::string steamId = LocalSteamId();
    if (!steamId.empty()) url += "&id=" + steamId;
    return url;
}

std::string MakeJoinSecret(std::uint64_t lobbyId) {
    if (!lobbyId || !IsDigits(AppId())) return "";
    return AppId() + ":" + std::to_string(lobbyId);
}

bool JoinFromSecret(const std::string& secret) {
    const size_t colon = secret.find(':');
    if (colon == std::string::npos) return false;
    const std::string appId = secret.substr(0, colon);
    const std::string lobby = secret.substr(colon + 1);

    // Never open anything but a lobby of this damn game
    if (!IsDigits(appId) || !IsDigits(lobby) || appId != AppId()) return false;

    Log("Joining a friend's lobby through Discord.");
    const std::string url = "steam://joinlobby/" + appId + "/" + lobby;
    return reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

}
