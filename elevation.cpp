// Manual elevation (m_floor) control for the Highfleet shipworks, Steam 1.163.
// Loaded by Highfleet-Modloader (exports `version` and `init`).
//
//   U   raise the floor of the part you are holding
//   P   lower the floor of the part you are holding
//   \   give the part back to the vanilla auto-elevation solver
//
// An on-screen text overlay shows the held part, its floor and its mounted children.
// Settings: Modloader/config/elevation.ini  (key=value per line, see ReadConfig).
//
// Offsets/addresses come from reverse engineering of the 1.163 dump:
//   DAT_1403c0d88  held-part Body*          DAT_1403ef368  editor-active flag
//   Body+0x9b0     m_floor_type (bitmask)   Body+0x9b8     m_floor (int)
//   Body+0x350     vector<Slot*>            Slot+0xb8 bit 4 = socket, Slot+0x58 = connected Body*
//   m_floor_type bit 0x01 = solver recomputes floor, 0x10 = solver forces floor 3
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

static const uintptr_t kImageBase = 0x140000000ull;
static const uintptr_t kHeldPart = 0x1403c0d88ull;
static const uintptr_t kEditorActive = 0x1403ef368ull;
static const size_t kOffOid = 0x190, kOffOidLen = 0x1a0;
static const size_t kOffFloorType = 0x9b0, kOffFloor = 0x9b8;
static const size_t kOffSlotsBegin = 0x350, kOffSlotsEnd = 0x358; // vector<Slot*>
static const size_t kSlotFlags = 0xb8, kSlotBody = 0x58;          // Slot fields
static const uint64_t kAutoBits = 0x11;

// --- settings (Modloader/config/elevation.ini) ---------------------------------------------
static int g_minFloor = 0;
static int g_maxFloor = 3;         // max_floor
static bool g_syncChildren = true; // sync_children: mounted modules follow their mount
static int g_childOffset = 0;      // child_offset: child floor = mount floor + this
static bool g_overlay = true;      // overlay

static FILE *g_log = nullptr;
static std::map<uintptr_t, uint64_t> g_origType; // part -> m_floor_type before we touched it

static void Log(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void ReadConfig() {
    FILE *f = fopen("Modloader/config/elevation.ini", "r");
    if (!f) {
        // write a documented default so the user can edit it
        CreateDirectoryA("Modloader/config", nullptr);
        f = fopen("Modloader/config/elevation.ini", "w");
        if (f) {
            fputs("# Highfleet manual elevation settings (restart the game after editing)\n"
                  "max_floor=3\n"
                  "# 1: mounted modules (guns etc.) are moved together with the part you edit\n"
                  "sync_children=1\n"
                  "# floor of a mounted module relative to its mount (0 = same floor, 1 = one above)\n"
                  "child_offset=0\n"
                  "# 1: show the on-screen text readout in the editor\n"
                  "overlay=1\n", f);
            fclose(f);
        }
        return;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || !strchr(line, '=')) continue;
        char *eq = strchr(line, '=');
        *eq = 0;
        int v = atoi(eq + 1);
        if (!strcmp(line, "max_floor")) g_maxFloor = v;
        else if (!strcmp(line, "sync_children")) g_syncChildren = v != 0;
        else if (!strcmp(line, "child_offset")) g_childOffset = v;
        else if (!strcmp(line, "overlay")) g_overlay = v != 0;
    }
    fclose(f);
}

static bool Readable(uintptr_t addr, size_t len) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)addr, &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    // image data sections are usually PAGE_WRITECOPY, heap is PAGE_READWRITE
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    return addr + len <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static bool EditorActive() { return *(volatile uint8_t *)kEditorActive != 0; }

static bool LooksLikeBody(uintptr_t p) {
    if (!p || !Readable(p, kOffFloor + 4)) return false;
    uint64_t oidLen = *(uint64_t *)(p + kOffOidLen);
    return oidLen != 0 && oidLen <= 64; // a part always has an m_oid
}

// Returns the held part if it looks like a valid Body, else 0.
static uintptr_t HeldPart() {
    // both globals are inside the game image (mapped once the base check in init() passed)
    if (!EditorActive()) return 0;
    uintptr_t p = *(volatile uintptr_t *)kHeldPart;
    return LooksLikeBody(p) ? p : 0;
}

static std::string Oid(uintptr_t p) {
    uint64_t len = *(uint64_t *)(p + kOffOidLen);
    uint64_t cap = *(uint64_t *)(p + kOffOidLen + 8);
    const char *s = cap >= 16 ? *(const char **)(p + kOffOid) : (const char *)(p + kOffOid);
    if (cap >= 16 && !Readable((uintptr_t)s, len)) return "?";
    return std::string(s, len > 40 ? 40 : len);
}

// Calls fn(child, isSocket) for every part linked to `parent` through its slot list. Non-socket
// slots (flag 0x4 clear) are mounted modules, the ones the vanilla floor solver bumps together
// with the mount.
template <class Fn> static void ForEachLinked(uintptr_t parent, Fn fn) {
    if (!Readable(parent + kOffSlotsBegin, 16)) return;
    uintptr_t b = *(uintptr_t *)(parent + kOffSlotsBegin);
    uintptr_t e = *(uintptr_t *)(parent + kOffSlotsEnd);
    if (!b || e < b || (e - b) / 8 > 64) return;
    for (uintptr_t it = b; it < e; it += 8) {
        if (!Readable(it, 8)) return;
        uintptr_t slot = *(uintptr_t *)it;
        if (!slot || !Readable(slot, kSlotBody + 8)) continue;
        bool socket = *(uint8_t *)(slot + kSlotFlags) & 4;
        uintptr_t child = *(uintptr_t *)(slot + kSlotBody);
        if (!child || child == parent || !LooksLikeBody(child)) continue;
        fn(child, socket);
    }
}

static int Clamp(int v) { return v < g_minFloor ? g_minFloor : v > g_maxFloor ? g_maxFloor : v; }

static void Adjust(int delta) {
    uintptr_t p = HeldPart();
    if (!p) return;
    uint64_t *type = (uint64_t *)(p + kOffFloorType);
    int *floor = (int *)(p + kOffFloor);
    if (!g_origType.count(p)) g_origType[p] = *type;
    int before = *floor;
    int next = Clamp(before + delta);
    *type &= ~kAutoBits;
    *floor = next;
    Log("part %p %s: floor %d -> %d, floor_type %llx -> %llx", (void *)p, Oid(p).c_str(), before,
        next, (unsigned long long)g_origType[p], (unsigned long long)*type);
    if (g_syncChildren) {
        ForEachLinked(p, [&](uintptr_t child, bool socket) {
            if (socket) return;
            int *cf = (int *)(child + kOffFloor);
            int target = Clamp(next + g_childOffset);
            Log("  mounted %p %s: floor %d -> %d", (void *)child, Oid(child).c_str(), *cf, target);
            *cf = target;
        });
    }
}

// Put the held part back under the vanilla solver: auto bit on, floors reset to 0 so the game
// recomputes them the next time the ship is edited (drop the part to trigger that).
static void Restore() {
    uintptr_t p = HeldPart();
    if (!p) return;
    uint64_t *type = (uint64_t *)(p + kOffFloorType);
    int *floor = (int *)(p + kOffFloor);
    g_origType.erase(p);
    *type |= 0x1;
    *type &= ~0x10ull; // the "force floor 3" bit would pin it again
    int before = *floor;
    *floor = 0;
    Log("part %p %s: back to auto (floor %d -> 0), floor_type=%llx", (void *)p, Oid(p).c_str(), before,
        (unsigned long long)*type);
    ForEachLinked(p, [&](uintptr_t child, bool socket) {
        if (socket) return;
        *(int *)(child + kOffFloor) = 0;
        Log("  mounted %p %s reset to floor 0", (void *)child, Oid(child).c_str());
    });
}

static DWORD WINAPI Worker(LPVOID) {
    bool prevDown = false, prevUp = false, prevReset = false;
    for (;;) {
        Sleep(15);
        bool down = GetAsyncKeyState('P') & 0x8000;
        bool up = GetAsyncKeyState('U') & 0x8000;
        bool reset = GetAsyncKeyState(VK_OEM_5) & 0x8000; // backslash
        if (down && !prevDown) Adjust(-1);
        if (up && !prevUp) Adjust(+1);
        if (reset && !prevReset) Restore();
        prevDown = down; prevUp = up; prevReset = reset;
    }
    return 0;
}

// --- on-screen text overlay ------------------------------------------------------------------
static std::string g_status;

static void BuildStatus() {
    std::string s = "ELEVATION  U up  P down  \\ auto\n";
    uintptr_t p = HeldPart();
    if (!p) {
        s += "holding: -";
    } else {
        uint64_t type = *(uint64_t *)(p + kOffFloorType);
        int floor = *(int *)(p + kOffFloor);
        char b[160];
        snprintf(b, sizeof b, "holding: %s  floor=%d  type=0x%llx (%s)", Oid(p).c_str(), floor,
                 (unsigned long long)type, (type & kAutoBits) ? "auto" : "manual");
        s += b;
        ForEachLinked(p, [&](uintptr_t c, bool socket) {
            snprintf(b, sizeof b, "\n  %s %s  floor=%d", socket ? "neighbour" : "mounted  ",
                     Oid(c).c_str(), *(int *)(c + kOffFloor));
            s += b;
        });
    }
    g_status = s;
}

static HFONT MakeFont() {
    return CreateFontA(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY,
                       FIXED_PITCH | FF_MODERN, "Consolas");
}

static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER: {
        if (!EditorActive()) { ShowWindow(h, SW_HIDE); return 0; }
        BuildStatus();
        HDC dc = GetDC(h);
        HFONT f = MakeFont();
        HGDIOBJ old = SelectObject(dc, f);
        RECT r = {0, 0, 10, 10};
        DrawTextA(dc, g_status.c_str(), -1, &r, DT_CALCRECT);
        SelectObject(dc, old); DeleteObject(f); ReleaseDC(h, dc);
        SetWindowPos(h, HWND_TOPMOST, 12, 12, r.right + 8, r.bottom + 6, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT r; GetClientRect(h, &r);
        HBRUSH b = CreateSolidBrush(RGB(255, 0, 255)); // colour key = transparent
        FillRect((HDC)w, &r, b); DeleteObject(b);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        HFONT f = MakeFont();
        HGDIOBJ old = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        RECT r = {2, 2, 2000, 2000};
        SetTextColor(dc, RGB(10, 10, 10));
        DrawTextA(dc, g_status.c_str(), -1, &r, 0);
        OffsetRect(&r, -2, -2);
        SetTextColor(dc, RGB(120, 255, 120));
        DrawTextA(dc, g_status.c_str(), -1, &r, 0);
        SelectObject(dc, old); DeleteObject(f);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI OverlayThread(LPVOID) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "HFElevationOverlay";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    RegisterClassA(&wc);
    HWND h = CreateWindowExA(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                 WS_EX_NOACTIVATE,
                             wc.lpszClassName, "", WS_POPUP, 12, 12, 400, 60, nullptr, nullptr,
                             wc.hInstance, nullptr);
    if (!h) { Log("overlay window failed: %lu", GetLastError()); return 1; }
    SetLayeredWindowAttributes(h, RGB(255, 0, 255), 0, LWA_COLORKEY);
    SetTimer(h, 1, 100, nullptr);
    Log("overlay created");
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    return 0;
}

// The modloader calls version() first but still calls init() when it returns false, so init() must
// refuse to run on any other build (every address in this mod is specific to Steam 1.163).
static bool g_versionOk = false;

extern "C" __declspec(dllexport) bool version(const char *v) {
    g_versionOk = v && strcmp(v, "Steam 1.163") == 0;
    return g_versionOk;
}

extern "C" __declspec(dllexport) bool init() {
    CreateDirectoryA("Modloader", nullptr);
    g_log = fopen("Modloader/elevation.log", "w");
    if (!g_versionOk) {
        Log("unsupported game version (needs Steam 1.163): mod disabled");
        return false;
    }
    if ((uintptr_t)GetModuleHandleA(nullptr) != kImageBase) {
        Log("unexpected image base, not enabling");
        return false;
    }
    ReadConfig();
    Log("elevation mod loaded: U raise, P lower, \\ auto | max_floor=%d sync_children=%d child_offset=%d overlay=%d",
        g_maxFloor, g_syncChildren, g_childOffset, g_overlay);
    CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
    if (g_overlay) CloseHandle(CreateThread(nullptr, 0, OverlayThread, nullptr, 0, nullptr));
    return true;
}
