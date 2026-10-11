# Call of Juarez: The Cartel - Discord Rich Presence

Shows what you're doing in Call of Juarez: The Cartel on Discord.

## What it does

- Shows current chapter and mission progress
- Displays your character (Ben, Kim, or Eddie)
- Shows multiplayer matches and lobbies
- Tracks playtime

## Installation

1. Download `CartelRichPresence.asi` from releases
2. Copy it to your game's directory (where the .exe is)
3. Make sure you have an ASI loader installed
4. Launch the game

## Building

Requirements:
- Visual Studio 2022 with C++ Desktop Development
- Windows SDK

Run `build.bat` or `.\build.ps1` to compile.

## How it works

Uses memory scanning and function hooking to read game state from the engine and game DLLs.

## Technical Details

- Hooks into `engine_x86_rwdi.dll` and `game_x86_rwdi.dll`
- Reads level names via exported IGame/ILevel functions
- Detects character selection through runtime hooking
- Communicates with Discord via named pipe IPC

## Files

- `src/` - C++ source code
- `analysis_output/` - Game DLL analysis results (exports, strings, RTTI)
- `build.ps1` / `build.bat` - Build scripts

## License

See LICENSE.txt
