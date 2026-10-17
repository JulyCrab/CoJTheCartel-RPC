#pragma once
#include <windows.h>
#include <string>
#include <optional>
#include "Config.h"

// Forward declaration
enum class ECharacter : int;

enum class SessionKind {
    None,
    Competitive, // multiplayer
    Cooperative, // campaign, also used when playing alone
};

// StatusCompetitive::m_Progress
enum class MatchProgress : int {
    Unknown          = -1,
    LobbyPreparing   = 0x1FFFF,
    LobbyCreated     = 0x2FFFF,
    LobbyNotFull     = 0x3FFFF,
    LobbyFull        = 0x4FFFF,
    LobbyMerging     = 0xAFFFF,
    LoadingScreen    = 0xBFFFF,
    ResultScreen     = 0xCFFFF,
    ReturningToLobby = 0xDFFFF,
    MatchStart       = 0x1FFFFF,
    MatchNotFull     = 0x2FFFFF,
    MatchFull        = 0x3FFFFF,
    NearEnd          = 0xFFFFFFF,
};

// EGameVisibilityMD
enum class GameVisibility : int {
    Local = 0, Public = 1, Private = 2, PublicCustom = 3, PrivateCustom = 4, LanCustom = 5,
};

struct MultiplayerState {
    SessionKind kind = SessionKind::None;
    MatchProgress progress = MatchProgress::Unknown;
    int playerCount = 0;

    // Multiplayer only, -1 / nullopt when unknown
    int level = -1;
    std::optional<bool> police;

    // Co-op only
    bool coopLobby = false;
    int coopPlayers = 0;
    GameVisibility visibility = GameVisibility::Local;
};

struct GameState {
    bool supportedBuild = false;
    bool starting = false; // engine not up yet, not an unsupported build (an issue from an older version)

    // Extracted Data
    std::string internalLevel;
    std::string chapterName;
    int progressPercent = 0;
    std::string characterName;
    std::optional<ECharacter> characterId;

    MultiplayerState mp;
};

class GameStateReader {
public:
    explicit GameStateReader(const Config& config);
    GameState ReadState();

    // Offsets found via reverse engineering the game
    static constexpr uintptr_t kEngineGamePointerRVA1 = 0x1D63928;
    static constexpr uintptr_t kEngineGamePointerRVA2 = 0x1D6392C;
    static constexpr uintptr_t kSessionCharacterOffset = 0x38;
    static constexpr uintptr_t kGameSaveSlotKimOffset = 0x3E0;
    static constexpr uintptr_t kGameSaveSlotEddieOffset = 0x790;

    // game_x86_rwdi.dll: CGame -> session -> Sessions::Status
    static constexpr uintptr_t kGameCGamePointerRVA = 0xC204E8;
    static constexpr uintptr_t kCGameSessionOffset = 0x160;
    static constexpr uintptr_t kSessionStatusOffset = 0x50;
    static constexpr uintptr_t kSessionPeerCountOffset = 0x74;
    static constexpr uintptr_t kStatusCompetitiveVtableRVA = 0xA21944;
    static constexpr uintptr_t kStatusCooperativeMDVtableRVA = 0xA21E94;

    // Multiplayer profile and the local player's side (see MenuLobby's progress panel)
    static constexpr uintptr_t kGameProfilePointerRVA = 0xC202C4;
    static constexpr uintptr_t kProfileExperienceOffset = 0x3C;
    static constexpr uintptr_t kProfileLevelOffset = 0x48;
    static constexpr uintptr_t kSessionPlayerControllerOffset = 0x24;
    static constexpr uintptr_t kControllerLogicalPlayerOffset = 0x58C;
    static constexpr uintptr_t kLogicalPlayerTeamOffset = 0x470; // 1 = police

    static constexpr uintptr_t kCompProgressOffset = 0x38;
    static constexpr uintptr_t kCoopIsLobbyOffset = 0x34;
    static constexpr uintptr_t kCoopPlayerIdsOffset = 0x3C; // Ben, Kim, Eddie; 0xFFFF = empty slot
    static constexpr uintptr_t kCoopVisibilityOffset = 0x48;

private:
    Config config_;
    HMODULE engineBase_ = nullptr;
    HMODULE gameBase_ = nullptr;

    void* engineGamePointer_ = nullptr;
    int iGameOffset_ = 0;
    bool unsupportedBuild_ = false;

    void* gameSingletonPointer_ = nullptr;
    bool gameSingletonResolved_ = false;
    std::optional<ECharacter> lastCharacter_;

    bool ResolvePointers();
    bool ResolveGameSingleton();
    std::optional<ECharacter> ReadCharacterEnum();
    void* ReadSessionStatus(SessionKind& kind, void** sessionOut = nullptr);
    MultiplayerState ReadMultiplayer();

    using GetActiveLevel_t = void* (__thiscall *)(void*);
    using GetLevelName_t = const char* (__thiscall *)(void*);

    GetActiveLevel_t pGetActiveLevel_ = nullptr;
    GetLevelName_t pGetLevelName_ = nullptr;
};
