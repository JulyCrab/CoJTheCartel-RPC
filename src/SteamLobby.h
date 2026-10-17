#pragma once
#include <cstdint>
#include <string>

// Tracks the Steam lobby we're in
// Joining only does this: steam://joinlobby, the engine handles the rest itself
namespace SteamLobby {

// Registers our Steam callbacks once steam_api.dll is loaded
void Update();

// Lets Discord start the game through Steam when a friend presses Join
void RegisterDiscordLaunch(const std::string& discordAppId);

std::uint64_t CurrentLobby();
void ClearLobby();

const std::string& AppId();

// Link to docs/join.html, for friends who don't have this mod
std::string MakeJoinLink(const std::string& pageUrl, std::uint64_t lobbyId);

// "<appid>:<lobbyid>"
std::string MakeJoinSecret(std::uint64_t lobbyId);
bool JoinFromSecret(const std::string& secret);

}
