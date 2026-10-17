#include "GameFiles.h"
#include "Log.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// engine_x86_rwdi.dll: import slot of fs::add_source
constexpr uintptr_t kEngineAddSourceSlotRVA = 0x47C6F0;

// fs::open mode 0 reads. Mode 1 creates the file in Documents, where it then overrides the game's copy, so don't use it
constexpr int kFsRead = 0;

using AddSource_t = bool(__cdecl*)(const char*, int);
using RemSource_t = bool(__cdecl*)(const char*);
using Open_t = void*(__cdecl*)(const char*, int, unsigned);
using Read_t = unsigned(__cdecl*)(void*, void*, unsigned);
using Length_t = unsigned(__cdecl*)(void*);
using Close_t = void(__cdecl*)(void*);

AddSource_t g_addSource = nullptr;
RemSource_t g_remSource = nullptr;
Open_t g_open = nullptr;
Read_t g_read = nullptr;
Length_t g_length = nullptr;
Close_t g_close = nullptr;

int g_pakFlags = 0x11;
std::vector<GameFiles::MountListener> g_listeners;

bool ResolveFileSystem() {
    HMODULE fs = GetModuleHandleA("filesystem_x86_rwdi.dll");
    if (!fs) return false;
    g_addSource = reinterpret_cast<AddSource_t>(GetProcAddress(fs, "?add_source@fs@@YA_NPBDH@Z"));
    g_remSource = reinterpret_cast<RemSource_t>(GetProcAddress(fs, "?rem_source@fs@@YA_NPBD@Z"));
    g_open = reinterpret_cast<Open_t>(GetProcAddress(fs, "?open@fs@@YAPAUSFsFile@@PBDW4TYPE@EFSMode@@I@Z"));
    g_read = reinterpret_cast<Read_t>(GetProcAddress(fs, "?read@fs@@YAIPAUSFsFile@@PAXI@Z"));
    g_length = reinterpret_cast<Length_t>(GetProcAddress(fs, "?length@fs@@YAIPAUSFsFile@@@Z"));
    g_close = reinterpret_cast<Close_t>(GetProcAddress(fs, "?close@fs@@YAXPAUSFsFile@@@Z"));
    return g_addSource && g_remSource && g_open && g_read && g_length && g_close;
}

bool __cdecl AddSourceHook(const char* path, int flags) {
    const bool result = g_addSource(path, flags);
    if (path) {
        const size_t len = strlen(path);
        if (len > 4 && _stricmp(path + len - 4, ".pak") == 0) g_pakFlags = flags;
    }
    for (GameFiles::MountListener listener : g_listeners) listener(path ? path : "");
    return result;
}

}

namespace GameFiles {

void InstallHook() {
    HMODULE engine = GetModuleHandleA("engine_x86_rwdi.dll");
    if (!engine || !ResolveFileSystem()) return;

    auto* slot = reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(engine) + kEngineAddSourceSlotRVA);
    if (*slot != reinterpret_cast<uintptr_t>(g_addSource)) {
        Log("Unknown engine build, can't read game files.");
        return;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) return;
    *slot = reinterpret_cast<uintptr_t>(&AddSourceHook);
    VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
}

void AddMountListener(MountListener listener) {
    g_listeners.push_back(listener);
}

bool Read(const char* path, std::string& out) {
    void* file = g_open(path, kFsRead, 0);
    if (!file) return false;
    out.resize(g_length(file));
    const unsigned read = out.empty() ? 0 : g_read(file, &out[0], static_cast<unsigned>(out.size()));
    g_close(file);
    return !out.empty() && read == out.size();
}

// Later sources win, so a pak meant to override game files has to go back to the end whenever the game adds another one
bool MountLast(const std::string& pakPath, bool alreadyMounted) {
    if (alreadyMounted) g_remSource(pakPath.c_str());
    return g_addSource(pakPath.c_str(), g_pakFlags);
}

}
