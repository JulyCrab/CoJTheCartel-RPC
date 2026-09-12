#include "Config.h"
#include "DiscordIpc.h"
#include "GameState.h"
#include "Log.h"

#include <Windows.h>
#include <cstddef>
#include <ctime>
#include <string>
#include <memory>

namespace {
// Auto-closes HANDLEs on destruction
class HandleWrapper {
public:
    HandleWrapper() = default;
    explicit HandleWrapper(HANDLE h) : handle_(h) {}
    ~HandleWrapper() { Close(); }
    
    HandleWrapper(const HandleWrapper&) = delete;
    HandleWrapper& operator=(const HandleWrapper&) = delete;
    
    HandleWrapper(HandleWrapper&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    HandleWrapper& operator=(HandleWrapper&& other) noexcept {
        if (this != &other) {
            Close();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    HANDLE Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    
    void Close() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }
    
private:
    HANDLE handle_ = nullptr;
};

HMODULE g_module = nullptr;
HandleWrapper g_stopEvent;

std::string ModuleDirectory() {
    constexpr size_t kMaxPath = MAX_PATH;
    char path[kMaxPath]{};
    GetModuleFileNameA(g_module, path, kMaxPath);
    std::string result = path;
    const size_t slash = result.find_last_of("\\/");
    return slash == std::string::npos ? "." : result.substr(0, slash);
}

bool IsAtMainMenu(const GameState& state) {
    return state.supportedBuild && (state.internalLevel == "MainMenu" || state.internalLevel == "Menuback" || state.chapterName == "Main Menu");
}

bool IsMultiplayerMap(const GameState& state) {
    if (state.internalLevel == "lobby_room") return true;
    if (state.internalLevel.size() >= 3 && state.internalLevel.substr(0, 3) == "mp_") return true;
    return false;
}

std::string MakeDetails(const Config& config, const GameState& state) {
    if (!state.supportedBuild) {
        return "Unsupported Game Version";
    }

    if (IsAtMainMenu(state)) {
        return "In Main Menu";
    }
    
    if (IsMultiplayerMap(state)) {
        return state.chapterName;
    }

    if (config.showChapter) {
        return state.chapterName;
    } else {
        return "Playing Call of Juarez: The Cartel";
    }
}

std::string MakeState(const Config& config, const GameState& state) {
    if (!state.supportedBuild || IsAtMainMenu(state)) {
        return "";
    }
    
    if (IsMultiplayerMap(state)) {
        if (state.internalLevel == "lobby_room") {
            return "In Multiplayer Lobby";
        }
        return "Playing a Multiplayer Match";
    }

    std::string text;
    
    if (config.showProgress && state.progressPercent > 0) {
        text += std::to_string(state.progressPercent) + "% Complete";
    }

    if (config.showCharacter && !state.characterName.empty()) {
        if (!text.empty()) text += " | ";
        text += state.characterName;
    }

    return text;
}

DWORD WINAPI WorkerThread(void*) {
    const std::string directory = ModuleDirectory();
    OpenLog(directory + "\\CartelRichPresence.log");
    Log("CartelRichPresence ASI injected!");
    
    Config config;
    LoadConfig(config);

    GameStateReader reader(config);

    DiscordIpc discord;
    const auto startTimestamp = static_cast<std::int64_t>(std::time(nullptr));
    std::string lastDetails;
    std::string lastState;
    
    constexpr DWORD kPollIntervalMs = 1000;
    constexpr DWORD kReconnectDelayMs = 5000;
    constexpr int kMainMenuDebounceCount = 10;

    while (WaitForSingleObject(g_stopEvent.Get(), 0) == WAIT_TIMEOUT) {
        if (!config.discordApplicationId.empty() && !discord.IsConnected()) {
            if (!discord.Connect(config.discordApplicationId)) {
                if (WaitForSingleObject(g_stopEvent.Get(), kReconnectDelayMs) != WAIT_TIMEOUT) break;
                continue;
            }
            lastDetails.clear();
            lastState.clear();
        }

        discord.Pump();

        const GameState game = reader.ReadState();
        const std::string details = MakeDetails(config, game);
        const std::string state = MakeState(config, game);

        // Hacky way to handle alt-tab and loading screens: 
        // Delay "In Main Menu" by 10 seconds (10 ticks) to ignore transient focus loss.
        static int mainMenuCounter = 0;
        if (details == "In Main Menu") {
            mainMenuCounter++;
        } else {
            mainMenuCounter = 0;
        }

        bool debounceIgnore = (details == "In Main Menu" && mainMenuCounter < kMainMenuDebounceCount);
        
        if (discord.IsConnected() && !debounceIgnore && (details != lastDetails || state != lastState)) {
            DiscordActivity activity;
            activity.details = details;
            activity.state = state;
            activity.largeImage = config.largeImageKey;
            activity.largeText = config.largeImageText;
            activity.buttonLabel = config.steamButtonText;
            activity.buttonUrl = config.steamButtonUrl;
            activity.startTimestamp = config.showElapsedTime ? startTimestamp : 0;
            
            if (discord.SetActivity(activity)) {
                Log("Presence updated: %s / %s", details.c_str(), state.c_str());
                lastDetails = details;
                lastState = state;
            }
        }

        if (WaitForSingleObject(g_stopEvent.Get(), kPollIntervalMs) != WAIT_TIMEOUT) break;
    }
    discord.Disconnect();
    Log("CartelRichPresence stopped.");
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        g_stopEvent = HandleWrapper(CreateEventA(nullptr, TRUE, FALSE, nullptr));
        HandleWrapper thread(CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr));
        // Thread closes itself when HandleWrapper goes out of scope
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_stopEvent) SetEvent(g_stopEvent.Get());
    }
    return TRUE;
}
