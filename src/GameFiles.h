#pragma once
#include <string>

// Access to the game's data through its own file system (filesystem_x86_rwdi.dll)
// This was done so we can read files out of the game's paks and add our own pak on top of them
namespace GameFiles {

// Called on the game's thread right after the game mounts a folder or pak
using MountListener = void (*)(const char* path);

// Hooks the engine's fs::add_source. Must run from DllMain, before the game mounts its data
// Also listeners have to be added before this
void InstallHook();
void AddMountListener(MountListener listener);

// Only call these from a mount listener; the file system isn't ours to use from another thread
bool Read(const char* path, std::string& out);
bool MountLast(const std::string& pakPath, bool alreadyMounted);

}
