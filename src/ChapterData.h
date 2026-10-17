#pragma once
#include <string>

// Call of Juarez: The Cartel chapter info stuff
struct ChapterInfo {
    std::string internalName;
    std::string friendlyName;
    int progressPercent;
};

// From game_x86_rwdi.dll RTTI: Ben=5, Kim=6, Eddie=7
enum class ECharacter : int {
    Ben   = 5,
    Kim   = 6,
    Eddie = 7,
    Unknown = -1
};

struct CharacterNameEntry {
    ECharacter enumValue;
    const char* fullName;
    const char* shortName;
};

inline constexpr CharacterNameEntry CARTEL_CHARACTERS[] = {
    { ECharacter::Ben,   "Ben McCall",   "Ben"   },
    { ECharacter::Kim,   "Kim Evans",    "Kim"   },
    { ECharacter::Eddie, "Eddie Guerra", "Eddie" },
};

inline const char* GetCharacterName(ECharacter enumValue, bool fullName = true) {
    for (const auto& entry : CARTEL_CHARACTERS) {
        if (entry.enumValue == enumValue) {
            return fullName ? entry.fullName : entry.shortName;
        }
    }
    return nullptr;
}

// Known level names
struct ChapterEntry {
    const char* internalName;
    const char* friendlyName;
    int progressPercent;
};

inline constexpr ChapterEntry CARTEL_CHAPTERS[] = {
    { "menuback", "Main Menu", 0 }, // keys are lowercase, GetChapterInfo lowercases the level name
    { "mainmenu", "Main Menu", 0 },
    { "plantation", "Chapter 1: Plantation", 6 },
    { "gangbang", "Chapter 2: Gang Bang", 13 },
    { "brothel", "Chapter 3: The Brothel", 20 },
    { "docks", "Chapter 4: The Docks", 26 },
    { "moneyinterception", "Chapter 5: Interception", 33 },
    { "convoy", "Chapter 6: Highway Convoy", 40 },
    { "jessescape", "Chapter 7: Jess's Escape", 46 },
    { "disco", "Chapter 8: The Disco", 53 },
    { "ghosttown", "Chapter 9: Ghost Town", 60 },
    { "motel", "Chapter 10: The Motel", 66 },
    { "meeting", "Chapter 11: The Meeting", 73 },
    { "fort", "Chapter 12: The Fort", 80 },
    { "funeral", "Chapter 13: The Funeral", 86 },
    { "juanchase", "Chapter 14: Juan's Chase", 93 },
    { "hacienda", "Chapter 15: Hacienda", 100 },
    { "prologue", "Prologue", 0 },
    { "mp_countryside", "Map: Countryside", 0 },
    { "mp_slums", "Map: Slums", 0 },
    { "mp_suburbs", "Map: Suburbs", 0 },
    { "mp_wasteland", "Map: Wasteland", 0 },
    { "lobby_room", "Multiplayer Lobby", 0 },
    { "mp_lobby", "Multiplayer Lobby", 0 }
};

inline ChapterInfo GetChapterInfo(const std::string& internalName) {
    std::string lowerName = internalName;
    for (char& c : lowerName) {
        if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
    }

    // Substring match in case level name has extra junk like "Level_Chapter01_Day"
    for (const auto& chapter : CARTEL_CHAPTERS) {
        if (lowerName.find(chapter.internalName) != std::string::npos) {
            ChapterInfo info;
            info.internalName = chapter.internalName;
            info.friendlyName = chapter.friendlyName;
            info.progressPercent = chapter.progressPercent;
            return info;
        }
    }
    
    // Unknown level, just capitalize it and return duh
    std::string niceName = internalName;
    if (!niceName.empty() && niceName[0] >= 'a' && niceName[0] <= 'z') {
        niceName[0] = niceName[0] - ('a' - 'A');
    }
    
    ChapterInfo unknown;
    unknown.internalName = internalName;
    unknown.friendlyName = "Playing: " + niceName;
    unknown.progressPercent = 0;
    return unknown;
}