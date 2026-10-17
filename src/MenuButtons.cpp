#include "MenuButtons.h"
#include "GameFiles.h"
#include "Log.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace {

constexpr const char* kButtonId = "B_CopyJoinLink";

// Both menus inherit these from IUIElement, so their slots point at the game's import stubs for them
// Side note: OnRender runs every frame the menu is drawn
constexpr size_t kOnShowSlot = 188;
constexpr uintptr_t kOnShowStubRVA = 0x7B878C;
constexpr size_t kOnRenderSlot = 201;
constexpr uintptr_t kOnRenderStubRVA = 0x7B8834;

struct Menu {
    const char* path;      // name the engine opens the file by
    const char* pakEntry;  // name inside the game's paks
    const char* anchorId;  // the button is inserted right before this element
    const char* position;
    const char* width;
    const char* height;
    const char* visual;
    uintptr_t vtableRVA;
    uintptr_t onPressSlotRVA;
    uintptr_t onPressRVA;
};

const Menu kMenus[] = {
    // Campaign pause menu (MenuInGameMD), right-hand column above Developer Menu List
    { "data\\menu\\scr\\MenuInGame.xui", "Data/Menu/Scr/MenuInGame.xui", "B_DeveloperMenuList",
      "904.000061,524.000000,0.000000", "317.000122", "32.000000", "MDButtonHintRightV",
      0x99B984, 0x99BEC0, 0x2BE9E0 },
    // Multiplayer menu (MenuLobby), under the "Your Progress" / Rank thing
    { "data\\menu\\scr\\MenuLobby.xui", "Data/Menu/Scr/MenuLobby.xui", "G_Level",
      "890.000000,200.000000,0.000000", "302.000000", "38.000000", "MDButtonV",
      0x9A2CAC, 0x9A31E8, 0x2D0FD0 },
};
constexpr size_t kMenuCount = sizeof(kMenus) / sizeof(kMenus[0]);

std::string g_pakPath;
bool g_mounted = false;
bool g_failed = false;

// ttl::string_base<char> as the engine lays it out
struct TtlString {
    const char* data;
    size_t length;
    size_t capacity;
};

using OnPress_t = int(__thiscall*)(void*, void*, int);
using MenuCallback_t = void(__thiscall*)(void*);
using GetId_t = const char* const*(__thiscall*)(void*);
using FindElement_t = void*(__thiscall*)(void*, const TtlString*, bool);
using SetVisible_t = void(__thiscall*)(void*, bool);

OnPress_t g_originalOnPress[kMenuCount] = {};
MenuCallback_t g_originalOnShow[kMenuCount] = {};
MenuCallback_t g_originalOnRender[kMenuCount] = {};
GetId_t g_getId = nullptr;
FindElement_t g_findElement = nullptr;
SetVisible_t g_setVisible = nullptr;

std::mutex g_linkMutex;
std::string g_joinLink;
std::atomic<bool> g_joinable{false};

// What each menu's button was last set to, only touched on the game's thread
void* g_menu[kMenuCount] = {};
bool g_buttonVisible[kMenuCount] = {};

std::string JoinLink() {
    std::lock_guard<std::mutex> lock(g_linkMutex);
    return g_joinLink;
}

bool AddButton(const Menu& menu, std::string& xui) {
    // The anchor's own opening tag sits just before its <Properties>
    const size_t id = xui.find(std::string("<Id>") + menu.anchorId + "</Id>");
    if (id == std::string::npos) return false;
    const size_t properties = xui.rfind("<Properties>", id);
    if (properties == std::string::npos || properties == 0) return false;
    const size_t element = xui.rfind('<', properties - 1);
    if (element == std::string::npos) return false;

    const std::string eol = xui.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string button =
        "<XuiNavButton>" + eol + "<Properties>" + eol +
        "<Id>" + kButtonId + "</Id>" + eol +
        "<Width>" + menu.width + "</Width>" + eol +
        "<Height>" + menu.height + "</Height>" + eol +
        "<Position>" + menu.position + "</Position>" + eol +
        "<Visual>" + menu.visual + "</Visual>" + eol +
        "<Text>Copy Join Link</Text>" + eol +
        "</Properties>" + eol + "</XuiNavButton>" + eol;
    xui.insert(element, button);
    return true;
}

std::uint32_t Crc32(const std::string& data) {
    std::uint32_t crc = 0xFFFFFFFF;
    for (unsigned char c : data) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
    }
    return ~crc;
}

void Put16(std::string& s, std::uint16_t v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>(v >> 8); }
void Put32(std::string& s, std::uint32_t v) { Put16(s, v & 0xFFFF); Put16(s, v >> 16); }

// Stored (uncompressed) deflate blocks, so we don't need a compression library
std::string Deflate(const std::string& data) {
    std::string out;
    size_t pos = 0;
    do {
        const size_t chunk = (std::min<size_t>)(data.size() - pos, 0xFFFF);
        out += static_cast<char>(pos + chunk == data.size() ? 1 : 0); // last block
        Put16(out, static_cast<std::uint16_t>(chunk));
        Put16(out, static_cast<std::uint16_t>(~chunk));
        out.append(data, pos, chunk);
        pos += chunk;
    } while (pos < data.size());
    return out;
}

// The game's paks are plain zip files (average techland game, like Dying Light, Dying Light 2, and so on)
std::string MakePak(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string zip;
    std::string central;
    for (const auto& [name, data] : files) {
        const std::string deflated = Deflate(data);
        const std::uint32_t crc = Crc32(data);
        const auto fileHeader = [&](std::string& s) {
            Put16(s, 20);   // version needed
            Put16(s, 0);    // flags
            Put16(s, 8);    // deflate
            Put16(s, 0);    // time
            Put16(s, 0x21); // date, 1980-01-01
            Put32(s, crc);
            Put32(s, static_cast<std::uint32_t>(deflated.size()));
            Put32(s, static_cast<std::uint32_t>(data.size()));
            Put16(s, static_cast<std::uint16_t>(name.size()));
            Put16(s, 0);    // extra length
        };

        const std::uint32_t localOffset = static_cast<std::uint32_t>(zip.size());
        Put32(zip, 0x04034B50);
        fileHeader(zip);
        zip += name;
        zip += deflated;

        Put32(central, 0x02014B50);
        Put16(central, 20); // version made by
        fileHeader(central);
        Put16(central, 0);  // comment length
        Put16(central, 0);  // disk
        Put16(central, 0);  // internal attributes
        Put32(central, 0);  // external attributes
        Put32(central, localOffset);
        central += name;
    }

    const std::uint32_t centralOffset = static_cast<std::uint32_t>(zip.size());
    const std::uint16_t count = static_cast<std::uint16_t>(files.size());
    zip += central;
    Put32(zip, 0x06054B50);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, count);
    Put16(zip, count);
    Put32(zip, static_cast<std::uint32_t>(central.size()));
    Put32(zip, centralOffset);
    Put16(zip, 0);
    return zip;
}

bool WriteWholeFile(const std::string& path, const std::string& data) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(file);
    return ok;
}

// Fails quietly until the game's own paks are mounted and the menus can be read
bool BuildPak() {
    std::vector<std::pair<std::string, std::string>> files;
    for (const Menu& menu : kMenus) {
        std::string xui;
        if (!GameFiles::Read(menu.path, xui)) return false;
        if (AddButton(menu, xui)) {
            files.emplace_back(menu.pakEntry, std::move(xui));
        } else {
            Log("Unexpected layout in %s, skipping the Copy Join Link button.", menu.pakEntry);
        }
    }
    if (files.empty()) {
        g_failed = true;
        return false;
    }

    char localAppData[MAX_PATH]{};
    GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH);
    const std::string dir = std::string(localAppData) + "\\CartelRichPresence";
    CreateDirectoryA(dir.c_str(), nullptr);
    g_pakPath = dir + "\\Menus.pak";

    if (!WriteWholeFile(g_pakPath, MakePak(files))) {
        Log("Couldn't write %s, Copy Join Link buttons disabled.", g_pakPath.c_str());
        g_failed = true;
        return false;
    }
    return true;
}

void OnGameMount(const char*) {
    if (g_failed) return;
    if (!g_mounted && !BuildPak()) return;
    g_mounted = GameFiles::MountLast(g_pakPath, g_mounted);
    if (!g_mounted) {
        Log("Couldn't mount %s, Copy Join Link buttons disabled.", g_pakPath.c_str());
        g_failed = true;
    }
}

bool CopyToClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    bool ok = false;
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1)) {
        memcpy(GlobalLock(mem), text.c_str(), text.size() + 1);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_TEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

void CopyJoinLink() {
    const std::string link = JoinLink();
    if (!link.empty() && CopyToClipboard(link)) {
        Log("Copied join link.");
        MessageBeep(MB_OK);
    } else {
        MessageBeep(MB_ICONWARNING);
    }
}

// The engine calls run on the game's own objects, so a bad pointer from an unexpected game build must not take the game down with it
bool IsCopyButton(void* button) {
    __try {
        const char* const* id = g_getId(button);
        return id && *id && strcmp(*id, kButtonId) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SetButtonVisible(void* menu, bool visible) {
    const TtlString id{kButtonId, strlen(kButtonId), strlen(kButtonId)};
    __try {
        if (void* button = g_findElement(menu, &id, true)) g_setVisible(button, visible);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Called every frame, so it only touches the menu when something changed
void UpdateButton(size_t index, void* menu, bool force) {
    const bool joinable = g_joinable;
    if (!force && menu == g_menu[index] && joinable == g_buttonVisible[index]) return;
    if (!g_findElement || !g_setVisible) return;
    SetButtonVisible(menu, joinable);
    g_menu[index] = menu;
    g_buttonVisible[index] = joinable;
}

template <size_t Index>
int __fastcall OnPressHook(void* menu, void*, void* button, int arg) {
    if (button && IsCopyButton(button)) {
        CopyJoinLink();
        return 0;
    }
    return g_originalOnPress[Index](menu, button, arg);
}

template <size_t Index>
void __fastcall OnShowHook(void* menu, void*) {
    g_originalOnShow[Index](menu);
    UpdateButton(Index, menu, true);
}

template <size_t Index>
void __fastcall OnRenderHook(void* menu, void*) {
    g_originalOnRender[Index](menu);
    UpdateButton(Index, menu, false);
}

using OnPressHook_t = int(__fastcall*)(void*, void*, void*, int);
using MenuCallbackHook_t = void(__fastcall*)(void*, void*);
const OnPressHook_t kPressHooks[kMenuCount] = { &OnPressHook<0>, &OnPressHook<1> };
const MenuCallbackHook_t kShowHooks[kMenuCount] = { &OnShowHook<0>, &OnShowHook<1> };
const MenuCallbackHook_t kRenderHooks[kMenuCount] = { &OnRenderHook<0>, &OnRenderHook<1> };

// Swaps a function pointer in place, but only when it still holds what this game build is expected to have there
template <typename T>
bool Hook(uintptr_t* slot, uintptr_t expected, T& original, uintptr_t replacement) {
    if (*slot != expected) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) return false;
    original = reinterpret_cast<T>(*slot);
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
    return true;
}

}

namespace MenuButtons {

void Init() {
    GameFiles::AddMountListener(&OnGameMount);
}

void Update() {
    static bool done = false;
    if (done) return;
    HMODULE game = GetModuleHandleA("game_x86_rwdi.dll");
    HMODULE engine = GetModuleHandleA("engine_x86_rwdi.dll");
    if (!game || !engine) return;
    done = true;

    g_getId = reinterpret_cast<GetId_t>(GetProcAddress(engine, "?GetId@IUIElement@@QAEABV?$string_base@D@ttl@@XZ"));
    g_findElement = reinterpret_cast<FindElement_t>(GetProcAddress(engine, "?FindElement@IUIElement@@QAEPAV1@ABV?$string_base@D@ttl@@_N@Z"));
    g_setVisible = reinterpret_cast<SetVisible_t>(GetProcAddress(engine, "?SetVisible@IUIElement@@QAEX_N@Z"));
    if (!g_getId) return;

    const auto base = reinterpret_cast<uintptr_t>(game);
    for (size_t i = 0; i < kMenuCount; ++i) {
        const Menu& menu = kMenus[i];
        if (!Hook(reinterpret_cast<uintptr_t*>(base + menu.onPressSlotRVA), base + menu.onPressRVA,
                  g_originalOnPress[i], reinterpret_cast<uintptr_t>(kPressHooks[i]))) {
            Log("Unknown game build, no Copy Join Link button in %s.", menu.pakEntry);
            continue;
        }
        // Without these the button just stays visible all the time
        auto* vtable = reinterpret_cast<uintptr_t*>(base + menu.vtableRVA);
        Hook(vtable + kOnShowSlot, base + kOnShowStubRVA, g_originalOnShow[i], reinterpret_cast<uintptr_t>(kShowHooks[i]));
        Hook(vtable + kOnRenderSlot, base + kOnRenderStubRVA, g_originalOnRender[i], reinterpret_cast<uintptr_t>(kRenderHooks[i]));
    }
}

void SetJoinLink(const std::string& link) {
    std::lock_guard<std::mutex> lock(g_linkMutex);
    g_joinLink = link;
    g_joinable = !link.empty();
}

}
