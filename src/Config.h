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
};

void LoadConfig(Config& config);
