#include "ChapterData.h"
#include "Config.h"
#include "DiscordIpc.h"
#include "GameFiles.h"
#include "GameState.h"
#include "Log.h"
#include "MenuButtons.h"
#include "RankNames.h"
#include "SteamLobby.h"

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <ctime>
#include <string>
#include <memory>

namespace {
// Auto closes HANDLEs on destruction
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

std::string g_currentMap;
std::string g_coopChapter;

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

bool IsLobbyPhase(MatchProgress progress) {
    switch (progress) {
    case MatchProgress::LobbyPreparing:
    case MatchProgress::LobbyCreated:
    case MatchProgress::LobbyNotFull:
    case MatchProgress::LobbyFull:
    case MatchProgress::LobbyMerging:
        return true;
    default:
        return false;
    }
}

bool IsMatchPhase(MatchProgress progress) {
    return progress == MatchProgress::MatchStart || progress == MatchProgress::MatchNotFull ||
           progress == MatchProgress::MatchFull || progress == MatchProgress::NearEnd;
}

bool IsInMultiplayerLobby(const GameState& state) {
    return state.internalLevel == "lobby_room" || state.internalLevel == "mp_lobby" ||
           (state.mp.kind == SessionKind::Competitive && IsLobbyPhase(state.mp.progress));
}

bool IsMultiplayerMap(const GameState& state) {
    if (state.mp.kind == SessionKind::Competitive) return true;
    if (IsInMultiplayerLobby(state)) return true;
    if (state.internalLevel.size() >= 3 && state.internalLevel.substr(0, 3) == "mp_") return true;
    return false;
}

// The level reads as MainMenu for a moment during loading screens and while
// sitting in the campaign lobby, so remember the last map and chapter
void TrackLocation(const GameState& state) {
    if (state.mp.kind == SessionKind::Cooperative) {
        if (state.chapterName.rfind("Chapter", 0) == 0 || state.chapterName == "Prologue") {
            g_coopChapter = state.chapterName;
        }
    } else {
        g_coopChapter.clear();
    }

    if (state.mp.kind != SessionKind::Competitive || IsInMultiplayerLobby(state)) {
        g_currentMap.clear();
        return;
    }
    if (IsAtMainMenu(state)) return;

    if (state.chapterName.rfind("Map: ", 0) == 0) {
        g_currentMap = state.chapterName.substr(5);
        return;
    }

    // Unknown map thing, ex: "mp_old_town" -> "Old Town"
    std::string name = state.internalLevel.rfind("mp_", 0) == 0 ? state.internalLevel.substr(3) : state.internalLevel;
    bool upper = true;
    for (char& c : name) {
        if (c == '_') { c = ' '; upper = true; continue; }
        if (upper && c >= 'a' && c <= 'z') c = c - ('a' - 'A');
        upper = false;
    }
    g_currentMap = name;
}

std::string MakeDetails(const Config& config, const GameState& state) {
    if (!state.supportedBuild) {
        return state.starting ? "Starting the game" : "Unsupported Game Version";
    }

    if (state.mp.kind == SessionKind::Cooperative && state.mp.coopLobby) {
        if (config.showChapter && !g_coopChapter.empty()) return g_coopChapter;
        return state.mp.visibility == GameVisibility::Local ? "Campaign Lobby" : "Co-op Lobby";
    }

    if (IsAtMainMenu(state) && state.mp.kind != SessionKind::Competitive) {
        return "In Main Menu";
    }

    if (IsMultiplayerMap(state)) {
        if (IsInMultiplayerLobby(state)) return "Multiplayer Lobby";
        return g_currentMap.empty() ? "Multiplayer Match" : "Multiplayer: " + g_currentMap;
    }

    if (config.showChapter) {
        return state.chapterName;
    } else {
        return "Playing Call of Juarez: The Cartel";
    }
}

std::string MakeMultiplayerState(const GameState& state) {
    switch (state.mp.progress) {
    case MatchProgress::LobbyPreparing:   return "Setting up a match";
    case MatchProgress::LobbyCreated:
    case MatchProgress::LobbyNotFull:     return "Waiting for players";
    case MatchProgress::LobbyFull:        return "Lobby full, starting soon";
    case MatchProgress::LobbyMerging:     return "Looking for more players";
    case MatchProgress::LoadingScreen:    return "Loading into the match";
    case MatchProgress::ResultScreen:     return "Checking the results";
    case MatchProgress::ReturningToLobby: return "Returning to the lobby";
    case MatchProgress::NearEnd:          return "Final moments";
    case MatchProgress::MatchStart:
    case MatchProgress::MatchNotFull:
    case MatchProgress::MatchFull:        return "In a match";
    default:
        return IsInMultiplayerLobby(state) ? "" : "Playing a Multiplayer Match";
    }
}

// Rank stuff, ex: "Police Officer V (Lvl 8)", or just "Lvl 8" before we know the side
std::string RankText(const MultiplayerState& mp) {
    if (mp.level < 0) return "";
    const std::string level = "Lvl " + std::to_string(mp.level);
    const std::string title = mp.police ? RankNames::Title(*mp.police, mp.level) : "";
    return title.empty() ? level : title + " (" + level + ")";
}

std::string MakeState(const Config& config, const GameState& state) {
    if (!state.supportedBuild) {
        return "";
    }

    if (state.mp.kind == SessionKind::Competitive) {
        std::string text = MakeMultiplayerState(state);
        const std::string rank = config.showPlayerLevel ? RankText(state.mp) : "";
        if (!rank.empty()) text += text.empty() ? rank : " | " + rank;
        return text;
    }

    if (state.mp.kind == SessionKind::Cooperative && state.mp.coopLobby) {
        std::string text = state.mp.visibility == GameVisibility::Local ? "In the lobby" : "In the co-op lobby";
        if (config.showCharacter && !state.characterName.empty()) text += " | " + state.characterName;
        return text;
    }

    if (IsAtMainMenu(state)) {
        return "";
    }

    if (IsMultiplayerMap(state)) {
        return IsInMultiplayerLobby(state) ? "" : "Playing a Multiplayer Match";
    }

    std::string text;

    if (state.mp.kind == SessionKind::Cooperative && state.mp.coopPlayers > 1) {
        text = "Co-op";
    }

    if (config.showProgress && state.progressPercent > 0) {
        if (!text.empty()) text += " | ";
        text += std::to_string(state.progressPercent) + "% Complete";
    }

    if (config.showCharacter && !state.characterName.empty()) {
        if (!text.empty()) text += " | ";
        text += state.characterName;
    }

    return text;
}

const char* CharacterIconKey(ECharacter character) {
    switch (character) {
    case ECharacter::Ben:   return "ben";
    case ECharacter::Kim:   return "kim";
    case ECharacter::Eddie: return "eddie";
    default:                return nullptr;
    }
}

const char* VisibilityText(GameVisibility visibility) {
    switch (visibility) {
    case GameVisibility::Public:
    case GameVisibility::PublicCustom:  return "Public";
    case GameVisibility::Private:
    case GameVisibility::PrivateCustom: return "Private";
    case GameVisibility::LanCustom:     return "LAN";
    default:                            return nullptr;
    }
}

// Adds the character icon, party size and join options
// Returns the join link for the menu buttons, empty when there's nothing to join
std::string AddMultiplayerInfo(const Config& config, const GameState& state, std::int64_t sessionStart, DiscordActivity& activity) {
    const MultiplayerState& mp = state.mp;

    if (config.showCharacterIcon && config.showCharacter && state.characterId && mp.kind != SessionKind::Competitive) {
        if (const char* key = CharacterIconKey(*state.characterId)) {
            activity.smallImage = key;
            activity.smallText = state.characterName;
        }
    }

    if (mp.kind == SessionKind::None) return "";

    // Single player also runs on a co-op session, with nobody else in it
    const std::uint64_t lobby = SteamLobby::CurrentLobby();
    // Steam doesn't tell us when we leave a lobby
    // In multiplayer the game drops back to setting up a match, so the last lobby id is stale by then
    const bool lobbyOpen = mp.kind == SessionKind::Cooperative ||
                           (mp.progress != MatchProgress::Unknown && mp.progress != MatchProgress::LobbyPreparing);
    const bool online = lobby && lobbyOpen && mp.visibility != GameVisibility::LanCustom &&
                        !(mp.kind == SessionKind::Cooperative && mp.visibility == GameVisibility::Local);
    if (mp.kind == SessionKind::Cooperative && !online && mp.coopPlayers <= 1) return "";

    if (mp.kind == SessionKind::Cooperative) {
        if (const char* visibility = VisibilityText(mp.visibility)) {
            activity.largeText = config.largeImageText + " | " + visibility + " co-op";
        }
    }

    int size = mp.playerCount;
    int max = config.competitiveMaxPlayers;
    bool full = mp.progress == MatchProgress::LobbyFull || mp.progress == MatchProgress::MatchFull ||
                mp.progress == MatchProgress::ResultScreen || mp.progress == MatchProgress::NearEnd;
    if (mp.kind == SessionKind::Cooperative) {
        size = (std::max)(mp.coopPlayers, mp.playerCount);
        max = 3;
        full = size >= max;
    }
    size = (std::max)(size, 1);
    max = (std::max)(max, size);

    if (config.showPartySize || online) {
        activity.partyId = "coj-" + std::to_string(lobby ? lobby : static_cast<std::uint64_t>(sessionStart));
        activity.partySize = size;
        activity.partyMax = max;
    }

    if (!online || full) return "";

    const std::string link = SteamLobby::MakeJoinLink(config.joinLinkUrl, lobby);
        activity.joinSecret = SteamLobby::MakeJoinSecret(lobby);
    return link;
}

DWORD WINAPI WorkerThread(void*) {
    Config config;
    LoadConfig(config);
    SteamLobby::RegisterDiscordLaunch(config.discordApplicationId);

    GameStateReader reader(config);

    DiscordIpc discord;
    const auto startTimestamp = static_cast<std::int64_t>(std::time(nullptr));
    DiscordActivity lastActivity;
    bool hasLastActivity = false;

    constexpr DWORD kPollIntervalMs = 1000;
    constexpr DWORD kReconnectDelayMs = 5000;
    constexpr int kMainMenuDebounceCount = 10;
    constexpr int kLobbyClearTicks = 5;
    constexpr int kJoinDelayTicks = 3;

    std::int64_t matchStart = 0;
    bool wasInMatch = false;
    int noSessionTicks = 0;
    std::string pendingJoin;
    int readyTicks = 0;

    while (WaitForSingleObject(g_stopEvent.Get(), 0) == WAIT_TIMEOUT) {
        SteamLobby::Update();
        MenuButtons::Update();

        if (!config.discordApplicationId.empty() && !discord.IsConnected()) {
            if (!discord.Connect(config.discordApplicationId)) {
                if (WaitForSingleObject(g_stopEvent.Get(), kReconnectDelayMs) != WAIT_TIMEOUT) break;
                continue;
            }
            hasLastActivity = false;
        }

        const std::string joinSecret = discord.Pump();
        if (!joinSecret.empty()) {
            pendingJoin = joinSecret;
        }

        const GameState game = reader.ReadState();
        TrackLocation(game);

        // Discord may have just started the game for this join
        // So let's give it a few seconds to settle before handing the lobby to Steam
        readyTicks = (game.supportedBuild && !game.internalLevel.empty()) ? readyTicks + 1 : 0;
        if (!pendingJoin.empty() && readyTicks >= kJoinDelayTicks) {
            SteamLobby::JoinFromSecret(pendingJoin);
            pendingJoin.clear();
        }

        // Steam doesn't tell us when we leave a lobby
        if (game.mp.kind == SessionKind::None) {
            if (++noSessionTicks == kLobbyClearTicks) SteamLobby::ClearLobby();
        } else {
            noSessionTicks = 0;
        }

        const bool inMatch = game.mp.kind == SessionKind::Competitive && IsMatchPhase(game.mp.progress);
        if (inMatch && !wasInMatch) matchStart = static_cast<std::int64_t>(std::time(nullptr));
        wasInMatch = inMatch;

        const std::string details = MakeDetails(config, game);
        const std::string state = MakeState(config, game);

        // Hacky way to handle alt tab and loading screens
        // Basically I delay "In Main Menu" by 10 seconds (or 10 ticks ig) to ignore "transient focus loss"
        // (yes that fancy name had to be googled, otherwise no idea it existed)
        static int mainMenuCounter = 0;
        if (details == "In Main Menu") {
            mainMenuCounter++;
        } else {
            mainMenuCounter = 0;
        }

        bool debounceIgnore = (details == "In Main Menu" && mainMenuCounter < kMainMenuDebounceCount);

        DiscordActivity activity;
        activity.details = details;
        activity.state = state;
        activity.largeImage = config.largeImageKey;
        activity.largeText = config.largeImageText;
        activity.buttonLabel = config.steamButtonText;
        activity.buttonUrl = config.steamButtonUrl;
        activity.startTimestamp = config.showElapsedTime ? startTimestamp : 0;
        if (config.showElapsedTime && config.showMatchTimer && inMatch) {
            activity.startTimestamp = matchStart;
        }
        MenuButtons::SetJoinLink(AddMultiplayerInfo(config, game, startTimestamp, activity));

        if (discord.IsConnected() && !debounceIgnore && (!hasLastActivity || activity != lastActivity)) {
            if (discord.SetActivity(activity)) {
                Log("Presence updated: %s / %s", details.c_str(), state.c_str());
                lastActivity = activity;
                hasLastActivity = true;
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
        OpenLog(ModuleDirectory() + "\\CartelRichPresence.log");
        Log("CartelRichPresence ASI injected!");
        // All of this needs to be in place before the game starts up
        MenuButtons::Init();
        RankNames::Init();
        GameFiles::InstallHook();
        SteamLobby::Update();
        g_stopEvent = HandleWrapper(CreateEventA(nullptr, TRUE, FALSE, nullptr));
        HandleWrapper thread(CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr));
        // Thread closes itself when HandleWrapper goes out of scope
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_stopEvent) SetEvent(g_stopEvent.Get());
    }
    return TRUE;
}
