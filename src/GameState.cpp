#include "GameState.h"
#include "ChapterData.h"
#include "Log.h"

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
        return false;
    }

    // Found via reverse engineering - see analysis_output/engine_exports.txt
    // CGame inherits from IGame, so this pointer works for both
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
    
    if (!engineGamePointer_) Log("Failed to resolve EngineGamePointer!");
    
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

// Character detection with hotkey override support

static ECharacter g_ActiveCharacter = ECharacter::Unknown;

extern "C" void OnSessionUpdate(void* pSession) {
    if (pSession) {
        int charEnum = *reinterpret_cast<int*>(static_cast<char*>(pSession) + GameStateReader::kSessionCharacterOffset);
        auto character = static_cast<ECharacter>(charEnum);
        if (character != g_ActiveCharacter && charEnum >= 0 && charEnum < 100) {
            Log("SessionUpdate: Character Enum changed to %d", charEnum);
            g_ActiveCharacter = character;
        }
    }
}

static uintptr_t g_StringAddress = 0;
static uintptr_t g_returnAddr = 0;

__declspec(naked) void SessionHook() {
    __asm {
        pushfd
        pushad
        
        // ESI contains the SessionCooperativeMD this pointer at this point in the function
        mov eax, esi
        push eax
        call OnSessionUpdate
        add esp, 4
        
        popad
        popfd
        
        // Execute original instruction: push g_StringAddress
        push g_StringAddress
        
        jmp g_returnAddr
    }
}

static uintptr_t FindString(uintptr_t modBase, uintptr_t modSize, const char* str) {
    size_t len = strlen(str);
    uintptr_t p = modBase;
    MEMORY_BASIC_INFORMATION mbi;
    
    while (p < modBase + modSize) {
        if (VirtualQuery((void*)p, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (end > modBase + modSize) end = modBase + modSize;
            
            for (uintptr_t i = (uintptr_t)mbi.BaseAddress; i < end - len; i++) {
                if (memcmp((void*)i, str, len) == 0) {
                    return i;
                }
            }
        }
        p = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return 0;
}

static uintptr_t FindPush(uintptr_t modBase, uintptr_t modSize, uintptr_t strAddr) {
    uintptr_t p = modBase;
    MEMORY_BASIC_INFORMATION mbi;
    
    while (p < modBase + modSize) {
        if (VirtualQuery((void*)p, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (end > modBase + modSize) end = modBase + modSize;
            
            for (uintptr_t i = (uintptr_t)mbi.BaseAddress; i < end - 5; i++) {
                if (*(unsigned char*)i == 0x68 && *(uintptr_t*)(i + 1) == strAddr) {
                    return i;
                }
            }
        }
        p = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return 0;
}

static bool InstallSessionHook() {
    static bool hooked = false;
    static bool failed = false;
    if (hooked) return true;
    if (failed) return false;
    
    HMODULE hEngine = GetModuleHandleA("game_x86_rwdi.dll");
    if (!hEngine) return false;
    
    uintptr_t modBase = (uintptr_t)hEngine;
    uintptr_t modSize = 0x2000000; // Good enough estimate
    
    // Find the target string in the DLL
    const char* targetStr = ", IsLobby = %s, IsTestMap = %s, Character = %s";
    uintptr_t strAddr = FindString(modBase, modSize, targetStr);
    
    if (!strAddr) {
        Log("Failed to find target string in game_x86_rwdi.dll");
        failed = true;
        return false;
    }
    
    g_StringAddress = strAddr;
    
    // Find the PUSH instruction that references it
    uintptr_t hookAddr = FindPush(modBase, modSize, strAddr);
    
    if (!hookAddr) {
        Log("Failed to find PUSH instruction for the string");
        failed = true;
        return false;
    }
    
    g_returnAddr = hookAddr + 5;
    
    // Patch in the jump to our hook
    DWORD oldProtect;
    if (VirtualProtect((void*)hookAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *(unsigned char*)hookAddr = 0xE9;
        *(uintptr_t*)(hookAddr + 1) = (uintptr_t)SessionHook - hookAddr - 5;
        VirtualProtect((void*)hookAddr, 5, oldProtect, &oldProtect);
        hooked = true;
        Log("Dynamic SessionHook installed successfully at 0x%p!", (void*)hookAddr);
        return true;
    }
    
    failed = true;
    return false;
}

std::optional<ECharacter> GameStateReader::ReadCharacterEnum() {
    // Hotkeys: F5 = Ben, F6 = Kim, F7 = Eddie, F8 = Auto
    /*
    static std::optional<ECharacter> manualOverride;
    
    if (GetAsyncKeyState(VK_F5) & 0x8000) manualOverride = ECharacter::Ben;
    if (GetAsyncKeyState(VK_F6) & 0x8000) manualOverride = ECharacter::Kim;
    if (GetAsyncKeyState(VK_F7) & 0x8000) manualOverride = ECharacter::Eddie;
    if (GetAsyncKeyState(VK_F8) & 0x8000) manualOverride = std::nullopt;
    
    if (manualOverride) {
        return manualOverride;
    }
    */

    if (!ResolveGameSingleton()) return std::nullopt;
    
    InstallSessionHook();
    
    if (g_ActiveCharacter != ECharacter::Unknown) {
        return g_ActiveCharacter;
    }
    
    // Fallback: Try reading from save slot offsets (only if hook hasn't fired)
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

GameState GameStateReader::ReadState() {
    GameState state;
    
    if (!ResolvePointers()) {
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

