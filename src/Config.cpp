#include "Config.h"
#include <windows.h>

void LoadConfig(Config& config) {
    constexpr auto kDefaultAppId = "1528427585032421376";
    constexpr auto kDefaultImageKey = "coj_thecartel";
    constexpr auto kDefaultImageText = "Call of Juarez: The Cartel";
    
    config.discordApplicationId = kDefaultAppId;
    config.largeImageKey = kDefaultImageKey;
    config.largeImageText = kDefaultImageText;
}
