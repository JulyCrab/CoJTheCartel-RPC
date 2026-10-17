#pragma once
#include <string>

struct Config {
    // Basic Discord Info
    std::string discordApplicationId;
    std::string largeImageKey;
    std::string largeImageText;
    std::string steamButtonUrl;
    std::string steamButtonText;

    // Display Toggles
    bool showChapter = true;
    bool showCharacter = true;
    bool showPlayerLevel = true;
    bool showElapsedTime = true;
    bool showProgress = true;
    bool showCharacterIcon = true; // needs "ben", "kim" and "eddie" art assets on the Discord app

    // Multiplayer
    bool showPartySize = true;
    bool showMatchTimer = true;
    int competitiveMaxPlayers = 12; //  max amount of players in the multiplayer mode (not coop!!)

    // Join game link for ppl who do not have the .asi file
    std::string joinLinkUrl = "https://julycrab.github.io/CoJTheCartel-RPC/join.html";
};

void LoadConfig(Config& config);
