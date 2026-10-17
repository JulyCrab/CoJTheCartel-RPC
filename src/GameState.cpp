#include "GameState.h"
#include "ChapterData.h"
#include "Log.h"

#include <cstring>

GameStateReader::GameStateReader(const Config& config) 
    : config_(config) {
}

// Safely read a pointer without crashing if the address is invalid
static void* SafeReadPtr(void* ptr) {
    void* result = nullptr;
    __try { 
        result = *static_cast<void**>(ptr); 
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

bool GameStateReader::ResolvePointers() {
    if (engineGamePointer_) return true; // Already resolved
    if (unsupportedBuild_) return false;

    engineBase_ = GetModuleHandleA("engine_x86_rwdi.dll");
    gameBase_ = GetModuleHandleA("game_x86_rwdi.dll");
    
    if (!engineBase_ || !gameBase_) return false;

    // Resolve exported functions
    pGetActiveLevel_ = reinterpret_cast<GetActiveLevel_t>(
        GetProcAddress(engineBase_, "?GetActiveLevel@IGame@@QAEPAVILevel@@XZ"));
    pGetLevelName_ = reinterpret_cast<GetLevelName_t>(
        GetProcAddress(engineBase_, "?GetLevelName@ILevel@@QAEPBDXZ"));
    
    if (!pGetActiveLevel_ || !pGetLevelName_) {
        Log("Failed to find engine exported functions!");
        unsupportedBuild_ = true;
        return false;
    }

    // Found via reverse engineering
    // Btw, CGame inherits from IGame, so this pointer works for both
    void* ptr = SafeReadPtr(reinterpret_cast<char*>(engineBase_) + kEngineGamePointerRVA1);
    if (ptr) {
        engineGamePointer_ = reinterpret_cast<char*>(engineBase_) + kEngineGamePointerRVA1;
        iGameOffset_ = 0; // Direct pointer, no offset needed
    } else {
        ptr = SafeReadPtr(reinterpret_cast<char*>(engineBase_) + kEngineGamePointerRVA2);
        if (ptr) {
            engineGamePointer_ = reinterpret_cast<char*>(engineBase_) + kEngineGamePointerRVA2;
            iGameOffset_ = 0;
        }
    }
    
    // Stays null until the engine has finished starting up
    return engineGamePointer_ != nullptr;
}


static bool SafeCopyString(const char* ptr, char* buffer, int maxLen) {
    if (!ptr || !buffer || maxLen <= 0) return false;
    __try {
        for (int i = 0; i < maxLen - 1; i++) {
            char c = ptr[i];
            buffer[i] = c;
            if (c == '\0') break;
        }
        buffer[maxLen - 1] = '\0';
        return true;
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void* SafeGetActiveLevel(void* func_ptr, void* engine) {
    if (!func_ptr || !engine) return nullptr;
    using GetActiveLevel_t = void* (__thiscall*)(void*);
    auto func = reinterpret_cast<GetActiveLevel_t>(func_ptr);
    void* result = nullptr;
    __try {
        result = func(engine);
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

static const char* SafeGetLevelName(void* func_ptr, void* level) {
    if (!func_ptr || !level) return nullptr;
    using GetLevelName_t = const char* (__thiscall*)(void*);
    auto func = reinterpret_cast<GetLevelName_t>(func_ptr);
    const char* result = nullptr;
    __try {
        result = func(level);
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

static int SafeReadInt(void* addr) {
    int result = -1;
    __try { 
        result = *static_cast<int*>(addr); 
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

static bool SafeReadBytes(const void* addr, void* out, size_t size) {
    if (!addr) return false;
    __try {
        memcpy(out, addr, size);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
static bool SafeRead(const void* base, uintptr_t offset, T& out) {
    return SafeReadBytes(static_cast<const char*>(base) + offset, &out, sizeof(T));
}

// Reuse the engine's CGame pointer since it's the same object
bool GameStateReader::ResolveGameSingleton() {
    if (gameSingletonResolved_) return gameSingletonPointer_ != nullptr;
    gameSingletonResolved_ = true;
    
    if (engineGamePointer_) {
        gameSingletonPointer_ = engineGamePointer_;
        Log("Game singleton resolved (reusing engine CGame pointer).");
        return true;
    }
    
    Log("Game singleton not available (engine pointer not resolved).");
    return false;
}

std::optional<ECharacter> GameStateReader::ReadCharacterEnum() {
    // Our own coop status holds the character we picked
    // Every player in a coop game has one of these, so it has to be ours, not just any of them
    SessionKind kind = SessionKind::None;
    if (void* status = ReadSessionStatus(kind); status && kind == SessionKind::Cooperative) {
        int charEnum = -1;
        if (SafeRead(status, kSessionCharacterOffset, charEnum) && GetCharacterName(static_cast<ECharacter>(charEnum))) {
            lastCharacter_ = static_cast<ECharacter>(charEnum);
        }
    }
    if (lastCharacter_) return lastCharacter_;

    if (!ResolveGameSingleton()) return std::nullopt;

    // Fallback: Try reading from save slot offsets (only if no session has been seen yet)
    void* pGame = SafeReadPtr(gameSingletonPointer_);
    if (pGame) {
        int kimSlot = SafeReadInt(static_cast<char*>(pGame) + kGameSaveSlotKimOffset);
        if (kimSlot == static_cast<int>(ECharacter::Kim)) {
            return ECharacter::Kim;
        }
        int eddieSlot = SafeReadInt(static_cast<char*>(pGame) + kGameSaveSlotEddieOffset);
        if (eddieSlot == static_cast<int>(ECharacter::Eddie)) {
            return ECharacter::Eddie;
        }
    }

    return std::nullopt;
}

// The vtable check keeps us from reading a stale or unrelated object
void* GameStateReader::ReadSessionStatus(SessionKind& kind, void** sessionOut) {
    kind = SessionKind::None;
    if (!gameBase_) return nullptr;

    const auto base = reinterpret_cast<uintptr_t>(gameBase_);
    void* cgame = SafeReadPtr(reinterpret_cast<void*>(base + kGameCGamePointerRVA));
    if (!cgame) return nullptr;

    void* session = nullptr;
    if (!SafeRead(cgame, kCGameSessionOffset, session) || !session) return nullptr;

    void* status = nullptr;
    if (!SafeRead(session, kSessionStatusOffset, status) || !status) return nullptr;

    uintptr_t vtable = 0;
    if (!SafeRead(status, 0, vtable)) return nullptr;

    if (vtable == base + kStatusCompetitiveVtableRVA) {
        kind = SessionKind::Competitive;
    } else if (vtable == base + kStatusCooperativeMDVtableRVA) {
        kind = SessionKind::Cooperative;
    } else {
        return nullptr;
    }
    if (sessionOut) *sessionOut = session;
    return status;
}

MultiplayerState GameStateReader::ReadMultiplayer() {
    MultiplayerState mp;
    SessionKind kind = SessionKind::None;
    void* session = nullptr;
    void* status = ReadSessionStatus(kind, &session);
    if (!status) return mp;

    // The peer list holds everyone but us
    int peers = -1;
    if (SafeRead(session, kSessionPeerCountOffset, peers) && peers >= 0 && peers < 32) {
        mp.playerCount = peers + 1;
    }

    if (kind == SessionKind::Competitive) {
        int progress = -1;
        if (!SafeRead(status, kCompProgressOffset, progress)) return mp;
        mp.progress = static_cast<MatchProgress>(progress);

        const auto base = reinterpret_cast<uintptr_t>(gameBase_);
        // Until the profile finishes loading it holds level 1 and no experience
        unsigned short level = 0;
        unsigned int experience = 0;
        void* profile = SafeReadPtr(reinterpret_cast<void*>(base + kGameProfilePointerRVA));
        if (profile && SafeRead(profile, kProfileLevelOffset, level) && SafeRead(profile, kProfileExperienceOffset, experience) &&
            level < 100 && (level > 1 || experience > 0)) {
            mp.level = level;
        }

        void* controller = nullptr;
        void* player = nullptr;
        int team = 0;
        if (SafeRead(session, kSessionPlayerControllerOffset, controller) && controller &&
            SafeRead(controller, kControllerLogicalPlayerOffset, player) && player &&
            SafeRead(player, kLogicalPlayerTeamOffset, team) && (team == 1 || team == 2)) {
            mp.police = team == 1;
        }
    } else {
        unsigned char isLobby = 0;
        unsigned short ids[3] = {0xFFFF, 0xFFFF, 0xFFFF};
        int visibility = 0;
        if (!SafeRead(status, kCoopIsLobbyOffset, isLobby) ||
            !SafeReadBytes(static_cast<char*>(status) + kCoopPlayerIdsOffset, ids, sizeof(ids)) ||
            !SafeRead(status, kCoopVisibilityOffset, visibility)) {
            return mp;
        }
        mp.coopLobby = isLobby != 0;
        for (unsigned short id : ids) {
            if (id != 0xFFFF) mp.coopPlayers++;
        }
        if (visibility >= 0 && visibility <= static_cast<int>(GameVisibility::LanCustom)) {
            mp.visibility = static_cast<GameVisibility>(visibility);
        }
    }

    mp.kind = kind;
    return mp;
}

GameState GameStateReader::ReadState() {
    GameState state;
    
    if (!ResolvePointers()) {
        state.starting = !unsupportedBuild_;
        return state;
    }
    
    state.supportedBuild = true;

    // Keep last known level to avoid flickering when engine returns nullptr temporarily
    static std::string lastKnownLevel = "MainMenu";

    // Read current chapter/level
    void* pEngineGame = SafeReadPtr(engineGamePointer_);

    if (pEngineGame) {
        void* pIGame = static_cast<char*>(pEngineGame) + iGameOffset_;
        void* pActiveLevel = SafeGetActiveLevel(pGetActiveLevel_, pIGame);
        
        if (pActiveLevel) {
            const char* levelName = SafeGetLevelName(pGetLevelName_, pActiveLevel);
            char levelBuf[128] = {0};
            if (SafeCopyString(levelName, levelBuf, sizeof(levelBuf))) {
                lastKnownLevel = levelBuf;
            }
        }
    }

    state.internalLevel = lastKnownLevel;
    
    if (!state.internalLevel.empty()) {
        ChapterInfo info = GetChapterInfo(state.internalLevel);
        state.chapterName = info.friendlyName;
        state.progressPercent = info.progressPercent;
    } else {
        state.internalLevel = "MainMenu";
        state.chapterName = "Main Menu";
    }

    state.mp = ReadMultiplayer();

    // Read character info
    if (config_.showCharacter) {
        state.characterId = ReadCharacterEnum();
        if (state.characterId) {
            const char* name = GetCharacterName(*state.characterId);
            if (name) {
                state.characterName = name;
            }
        }
    }

    return state;
}

