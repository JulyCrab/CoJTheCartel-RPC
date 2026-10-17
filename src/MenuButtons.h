#pragma once
#include <string>

// "Copy Join Link" button in the campaign pause menu and the multiplayer menu
// The menu files are patched as the game loads them and mounted from a pak in %LOCALAPPDATA%\CartelRichPresence 
namespace MenuButtons {

// Call from DllMain, before GameFiles::InstallHook
void Init();

// Hooks the menus' button handlers once the game DLL is loaded
void Update();

// Link the buttons copy; empty when there's no lobby to join
void SetJoinLink(const std::string& link);

}
