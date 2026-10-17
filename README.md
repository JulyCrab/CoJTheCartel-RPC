# Call of Juarez: The Cartel - Discord Rich Presence

Shows what you're doing in Call of Juarez: The Cartel on Discord.

## What it does

Campaign:
- Current chapter and story progress
- Your character (Ben, Kim or Eddie), with their portrait on Discord
- Co-op lobbies and how many players are in your game

Multiplayer:
- Lobby status (setting up, waiting for players, lobby full)
- Current map, loading, in match, final moments and results
- Your rank and level, e.g. "Police Officer V (Lvl 8)"
- A timer that restarts with every match

## Joining friends

When you're in an online lobby, friends can join you in two ways:

- **Discord's Join button.** Works for friends who also run this mod. It also shows up when you invite someone from Discord chat.
- **Copy Join Link.** A button added to the pause menu and the multiplayer menu. It copies a link you can paste anywhere; whoever opens it joins your lobby through Steam, with or without the mod.

## Installation

1. Download `CartelRichPresence.asi` from releases
2. Copy it to your game's directory (where the .exe is)
3. Make sure you have an ASI loader installed
4. Launch the game

This doesn't change any game files. It writes a log next to the .asi and a save of the older log.

## Building

Requirements:
- Visual Studio 2022 with C++ Desktop Development
- Windows SDK

Run `build.bat` or `.\build.ps1` to compile. (I'd strongly suggest running the .bat file for ease of use)

## How it works

Reads the game state from `engine_x86_rwdi.dll` and `game_x86_rwdi.dll` and then it talks to Discord over the local IPC pipe.

- Level names come from the engine's exported IGame/ILevel functions
- Session, lobby and match state come from the game's session objects
- Steam lobby callbacks provide the lobby to join
- The menu buttons are added by patching the game's menu files as they load
- Rank titles are read from the game's text files, so they follow the game's lang thing, for example if you have 

# Invites & Multiplayer

To invite people through discord's integrated thing, both of you must have the .asi installed and have previously runned the game with the .asi at least once!

For people who don't have the .asi, you can first host a lobby, and then a button will appear as "Copy Join Link", it'll copy into your clipboard and then send it to your friend! 

That's all, it's made to be very simple! :P

## Files

- `src/`, this is the C++ source code
- `docs/join.html`, this is the page behind the join links, forwards to Steam & the game
- `build.ps1` / `build.bat`, these are the Build scripts

## License

See LICENSE.txt
