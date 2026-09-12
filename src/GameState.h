#pragma once
#include <windows.h>
#include <string>
#include <optional>
#include "Config.h"

// Forward declaration
enum class ECharacter : int;

struct GameState {
    bool supportedBuild = false;
    
    // Extracted Data
    std::string internalLevel;
    std::string chapterName;
    int progressPercent = 0;
    std::string characterName;
    std::optional<ECharacter> characterId;
};

class GameStateReader {
public:
    explicit GameStateReader(const Config& config);
    GameState ReadState();

    // Offsets found via reverse engineering - see analysis_output/*.txt
    static constexpr uintptr_t kEngineGamePointerRVA1 = 0x1D63928;
    static constexpr uintptr_t kEngineGamePointerRVA2 = 0x1D6392C;
    static constexpr uintptr_t kSessionCharacterOffset = 0x38;
    static constexpr uintptr_t kGameSaveSlotKimOffset = 0x3E0;
    static constexpr uintptr_t kGameSaveSlotEddieOffset = 0x790;

private:
    Config config_;
    HMODULE engineBase_ = nullptr;
    HMODULE gameBase_ = nullptr;
    
    void* engineGamePointer_ = nullptr;
    int iGameOffset_ = 0;
    
    void* gameSingletonPointer_ = nullptr;
    bool gameSingletonResolved_ = false;
    
    bool ResolvePointers();
    bool ResolveGameSingleton();
    std::optional<ECharacter> ReadCharacterEnum();
    
    using GetActiveLevel_t = void* (__thiscall *)(void*);
    using GetLevelName_t = const char* (__thiscall *)(void*);
    
    GetActiveLevel_t pGetActiveLevel_ = nullptr;
    GetLevelName_t pGetLevelName_ = nullptr;
};
