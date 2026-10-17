#pragma once
#include <string>

// Multiplayer rank titles ("Police Officer V", "Thug II", ...) read from the game's own text file, so they follow the game's lang
namespace RankNames {

// Call from DllMain, before GameFiles::InstallHook
void Init();

// Empty when unknown
std::string Title(bool police, int level);

}
