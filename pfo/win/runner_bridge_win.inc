// Windows build with our netcode (PFO_WINDOWS_OURS): what android/runner_bridge.inc is to the
// Android build. Included at the end of dllmain.cpp, after android/fakesteam_impl.inc.
//
// The Windows runner loads PFO.dll itself, hands it the real YYRunnerInterface and finds every
// extension function by its MSVC C++ name ("?name@@YAX..."). So there is nothing to rebuild here:
// this file only gives the shared sources the few helpers they expect, wraps every function the
// game calls (the same wrapper as on Android: network lock, save state hooks around pfo_update)
// and exports the wrappers under the names the runner asks for.
//
// The game's data file lists these functions under PFO.dll (scripts/windows_patch.csx): PFO's
// own, every steam_* one the game's code uses (our netcode answers them; Steamworks_x64.dll and
// steam_api64.dll are not part of this build) and ours.

#include <winhttp.h>

namespace pfo_android
{
    // ---- the runner's own internals ----
    // Snapshots (android/savestate.inc) read and write GameMaker's instance lists, random
    // generator and collision tree. libyoyo.so gives their names; ufo50.exe has none, so here
    // they are found by where they are in the one runner this was worked out for (UFO 50 1.9.5's,
    // built 2026-01-21), and only once the code at every place used has been checked. With any
    // other ufo50.exe there are no snapshots: loads and rewinds replay the session, and a match
    // is told so (command 246). PFO_NO_SNAPSHOTS=1 in the environment does the same by hand.
    namespace winrunner
    {
        constexpr uint32 c_TimeStamp = 0x6970f195, c_ImageSize = 0xa72000;
        struct Proof { uint32 rva, size; uint64 hash; };   // FNV-1a of the code there
        constexpr Proof c_Proofs[] = {
            { 0x11e740, 0x29b, 0x3beeefbbba21043dull },   // CRoom::UpdateActive (Run_Room)
            { 0x176b00, 0x325, 0x84dd7ed403fbc2a8ull },   // RebuildTree
            { 0x1765a0, 0x0ba, 0x7d042c642a65cba6ull },   // the collision tree: an instance's box
            { 0x174900, 0x12a, 0x7c0add6c70e30e64ull },   // the collision tree: insert
            { 0x1779e0, 0x093, 0xd155f8b0354e39e8ull },   // the collision tree: get
            { 0x11cd20, 0x257, 0xe5e650161f63a48full },   // CRoom::AddInstance (room_maxid)
            { 0x0f0f10, 0x383, 0x7bf9f792d4211c09ull },   // CInstance::CInstance (the creation counter)
            { 0x185860, 0x40e, 0x484071bf328e15fbull },   // UpdateActiveLists (instance_change's list)
            { 0x107c30, 0x071, 0x52151fc2879515f5ull },   // an object's list: remove
            { 0x107cb0, 0x0a3, 0x1948c66929763611ull },   // an object's list: add
            { 0x1077a0, 0x0df, 0x4159a0bd4007ea13ull },   // CObjectGM::AddInstance
            { 0x3f97a0, 0x10b, 0xf1ee1bcdca958f04ull },   // MemoryManager::Free
            { 0x201a60, 0x079, 0xf1584ac22ebc9979ull },   // random_set_seed (the seed, the state)
            { 0x0b6460, 0x0a9, 0xe3d1103db991b889ull },   // the random generator (its index)
            { 0x1d1570, 0x01b, 0x2c0e383679d9c1ceull },   // draw_enable_drawevent (Draw_Automatic)
        };
        constexpr uint32 c_RunRoom = 0xa008f8, c_RoomMaxId = 0x772c94, c_CreateCounter = 0x75c8c8,
            c_RngState = 0x74fa60, c_RngIndex = 0x74f654, c_RngSeed = 0xa1f8a4, c_DrawAutomatic = 0xa008a3,
            c_UpdateActive = 0x11e740, c_RebuildTree = 0x176b00,
            c_ChangeArray = 0xa21e10, c_ChangeCount = 0xa21e1c, c_ListRemove = 0x107c30, c_ObjectAddInstance = 0x1077a0, c_Free = 0x3f97a0;

        uint8* g_Base = nullptr;
        std::atomic<int> g_Known{-1};

        bool Check()
        {
            char off[8];
            if (GetEnvironmentVariableA("PFO_NO_SNAPSHOTS", off, sizeof(off)) > 0 && off[0] != '0')
            {
                __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "snapshots: off (PFO_NO_SNAPSHOTS)");
                return false;
            }
            auto base = reinterpret_cast<uint8*>(GetModuleHandleW(nullptr));
            auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
            auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != c_TimeStamp || nt->OptionalHeader.SizeOfImage != c_ImageSize)
            {
                __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "snapshots: off, this isn't the runner they were worked out for (built %08x, %x bytes in memory)",
                    static_cast<unsigned>(nt->FileHeader.TimeDateStamp), static_cast<unsigned>(nt->OptionalHeader.SizeOfImage));
                return false;
            }
            for (const Proof& p : c_Proofs)
            {
                uint64 h = 0xcbf29ce484222325ull;
                for (uint32 i = 0; i < p.size; i++) h = (h ^ base[p.rva + i]) * 0x100000001b3ull;
                if (h != p.hash)
                {
                    __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "snapshots: off, the runner's code at %x isn't what they were worked out for", static_cast<unsigned>(p.rva));
                    return false;
                }
            }
            if (Script_Find_Id("pfo_snapshot") < 0)
            {
                __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "snapshots: off, the game's data has no snapshot code (scripts/pfo_snapshot.gml)");
                return false;
            }
            g_Base = base;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "snapshots: on (this is the runner they were worked out for, %d places checked)", static_cast<int>(countof(c_Proofs)));
            return true;
        }
        // (first asked in the game's first step, on its thread)
        bool Known()
        {
            int k = g_Known.load();
            if (k < 0) { k = Check() ? 1 : 0; g_Known = k; }
            return k == 1;
        }

        // instance_change() moves an instance into its new object's lists at the runner's next
        // list update. In libyoyo.so that is a function of its own (ChangeInstanceTypes); in
        // ufo50.exe the compiler put it inside the list update (0x1401858ba to 0x140185998), so
        // this is that code written out: out of the old object's own list and the `with` lists of
        // it and its parents, into the new object's, and the list of changes emptied.
        void ChangeInstanceTypes()
        {
            auto at = [](void* p, int off) -> void*& { return *reinterpret_cast<void**>(static_cast<uint8*>(p) + off); };
            void** changed = *reinterpret_cast<void***>(g_Base + c_ChangeArray);
            int& count = *reinterpret_cast<int*>(g_Base + c_ChangeCount);
            auto listRemove = reinterpret_cast<bool (*)(void*, void*)>(g_Base + c_ListRemove);
            auto addInstance = reinterpret_cast<void (*)(void*, void*)>(g_Base + c_ObjectAddInstance);
            auto freeNode = reinterpret_cast<void (*)(void*, int)>(g_Base + c_Free);
            for (int k = 0; k < count; k++)
            {
                void* inst = changed[k];
                if (void* old = at(inst, 0x180))
                {
                    listRemove(static_cast<uint8*>(old) + 0x50, inst);
                    for (void* obj = old; obj; obj = at(obj, 0x8))
                        for (void* node = at(obj, 0x68); node; node = at(node, 0))
                        {
                            if (at(node, 0x10) != inst) continue;
                            void *next = at(node, 0), *prev = at(node, 8);
                            if (prev) at(prev, 0) = next; else at(obj, 0x68) = next;
                            if (next) at(next, 8) = prev; else at(obj, 0x70) = prev;
                            freeNode(node, 0);
                            --*reinterpret_cast<int*>(static_cast<uint8*>(obj) + 0x78);
                            break;
                        }
                }
                at(inst, 0x180) = nullptr;
                addInstance(at(inst, 0x88), inst);
                *reinterpret_cast<uint32*>(static_cast<uint8*>(inst) + 0xb0) &= ~0x40000u;
            }
            count = 0;
        }
        // CRoom::UpdateActive reads Run_Room itself here (no argument)
        void UpdateActive(void*) { reinterpret_cast<void (*)()>(g_Base + c_UpdateActive)(); }

        void* Find(const char* name)
        {
            if (!Known()) return nullptr;
            static const struct { const char* name; uint32 rva; } data[] = {
                { "Run_Room", c_RunRoom }, { "room_maxid", c_RoomMaxId }, { "_ZN9CInstance23ms_CurrentCreateCounterE", c_CreateCounter },
                { "state", c_RngState }, { "g_RndIndex", c_RngIndex }, { "g_nRandSeed", c_RngSeed }, { "Draw_Automatic", c_DrawAutomatic },
                { "_Z11RebuildTreeb", c_RebuildTree },
            };
            for (auto& d : data) if (!strcmp(name, d.name)) return g_Base + d.rva;
            if (!strcmp(name, "_ZN5CRoom12UpdateActiveEv")) return reinterpret_cast<void*>(&UpdateActive);
            if (!strcmp(name, "_Z19ChangeInstanceTypesv")) return reinterpret_cast<void*>(&ChangeInstanceTypes);
            return nullptr;
        }
    }
    // ---- when the game faults ----
    // The runner ends without a word when its code (or ours, called from it) faults. So the first
    // few faults are written to pfo_online.log: what, where (file + offset) and the calls that led
    // there. Nothing else changes: the fault goes on to whoever would have handled it.
    namespace wincrash
    {
        std::atomic<int> g_Logged{0};
        void Where(DWORD64 addr, char* out, size_t size)
        {
            HMODULE mod = nullptr;
            char path[MAX_PATH] = "";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(addr), &mod) && mod
                && GetModuleFileNameA(mod, path, sizeof(path)) > 0)
            {
                const char* name = strrchr(path, '\\');
                snprintf(out, size, "%s+%llx", name ? name + 1 : path, static_cast<unsigned long long>(addr - reinterpret_cast<DWORD64>(mod)));
            }
            else snprintf(out, size, "%llx", static_cast<unsigned long long>(addr));
        }
        LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
        {
            const EXCEPTION_RECORD* rec = info->ExceptionRecord;
            const DWORD code = rec->ExceptionCode;
            if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR && code != EXCEPTION_STACK_OVERFLOW
                && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO
                && code != 0xC0000409u /* fail fast */ && code != 0xC0000374u /* heap corruption */) return EXCEPTION_CONTINUE_SEARCH;
            if (g_Logged.fetch_add(1) >= 3) return EXCEPTION_CONTINUE_SEARCH;
            char at[320];
            Where(reinterpret_cast<DWORD64>(rec->ExceptionAddress), at, sizeof(at));
            if (code == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
                __android_log_print(ANDROID_LOG_ERROR, "PFOCRASH", "fault %08x at %s, %s %llx (thread %u)", static_cast<unsigned>(code), at,
                    rec->ExceptionInformation[0] == 1 ? "writing" : rec->ExceptionInformation[0] == 8 ? "running" : "reading",
                    static_cast<unsigned long long>(rec->ExceptionInformation[1]), static_cast<unsigned>(GetCurrentThreadId()));
            else
                __android_log_print(ANDROID_LOG_ERROR, "PFOCRASH", "fault %08x at %s (thread %u)", static_cast<unsigned>(code), at, static_cast<unsigned>(GetCurrentThreadId()));
            if (code == EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;   // (no room left to walk it)
            // the calls that led there, by the unwind tables every 64-bit module has
            CONTEXT ctx = *info->ContextRecord;
            std::string calls;
            for (int n = 0; n < 24 && ctx.Rip; n++)
            {
                DWORD64 base = 0;
                if (PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr))
                {
                    void* handlerData = nullptr;
                    DWORD64 frame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handlerData, &frame, nullptr);
                }
                else
                {
                    // a function with no frame: its return address is on top
                    if (IsBadReadPtr(reinterpret_cast<void*>(ctx.Rsp), 8)) break;
                    ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                    ctx.Rsp += 8;
                }
                if (!ctx.Rip) break;
                Where(ctx.Rip, at, sizeof(at));
                calls += calls.empty() ? "" : " < ";
                calls += at;
            }
            __android_log_print(ANDROID_LOG_ERROR, "PFOCRASH", "called from: %s", calls.c_str());
            return EXCEPTION_CONTINUE_SEARCH;
        }
        void Install() { AddVectoredExceptionHandler(1, &OnException); }
    }

    // the names are libyoyo.so's (android/savestate.inc asks for them by those)
    template <typename T> T Sym(const char* name) { return reinterpret_cast<T>(winrunner::Find(name)); }
    // whether this device has snapshots (on Android: always)
    bool NativeSnapshots() { return winrunner::Known(); }

    inline double Real(const RValue* v) { return REAL_RValue(v); }
    inline int Kind(const RValue* v) { return v->kind & MASK_KIND_RVALUE; }

    // POST a form, as PFO_Android.okpPost does: "<http status>\n<body>", or "0\n<error>" when
    // there was no answer. A connection that fails is tried again, twice.
    std::string HttpPostOnce(const std::wstring& host, INTERNET_PORT port, bool secure, const std::wstring& path, const std::string& body, bool& retry)
    {
        retry = true;
        std::string out = "0\nno connection";
        HINTERNET session = WinHttpOpen(L"UFO50-Windows-PFO", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return out;
        WinHttpSetTimeouts(session, 6000, 6000, 8000, 8000);
        HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
        if (request
            && WinHttpSendRequest(request, L"Content-Type: application/x-www-form-urlencoded\r\n", static_cast<DWORD>(-1),
                                  body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)
            && WinHttpReceiveResponse(request, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            std::string text;
            bool ok = true;
            char buf[8192];
            while (text.size() < 1024 * 1024)
            {
                DWORD got = 0;
                if (!WinHttpReadData(request, buf, sizeof(buf), &got)) { ok = false; break; }
                if (got == 0) break;
                text.append(buf, got);
            }
            if (ok && status != 0)
            {
                out = std::to_string(status) + "\n" + text;
                retry = false;
            }
        }
        else
        {
            out = "0\nerror " + std::to_string(GetLastError());
        }
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return out;
    }

    std::string HttpPostWin(const std::string& url, const std::string& body)
    {
        std::wstring wide = pfo_win::Wide(url.c_str());
        wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
        URL_COMPONENTS parts = {};
        parts.dwStructSize = sizeof(parts);
        parts.lpszHostName = host; parts.dwHostNameLength = countof(host);
        parts.lpszUrlPath = path; parts.dwUrlPathLength = countof(path);
        parts.lpszExtraInfo = extra; parts.dwExtraInfoLength = countof(extra);
        if (wide.empty() || !WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) return "0\nbad address";
        std::wstring full = std::wstring(path) + extra;
        if (full.empty()) full = L"/";
        std::string out;
        for (int attempt = 0; attempt < 3; attempt++)
        {
            bool retry = false;
            out = HttpPostOnce(host, parts.nPort, parts.nScheme == INTERNET_SCHEME_HTTPS, full, body, retry);
            if (!retry) break;
        }
        return out;
    }
}

#include "../android/savestate.inc"
#include "ui_win.inc"

// pfo_android_ask_code(): JOIN BY CODE in the game's lobby list; the menu thread shows the code box
static void pfo_android_ask_code(RValue& r, CInstance*, CInstance*, int, RValue*)
{
    fakesteam::g_AskCodeRequested = true;
    r.kind = VALUE_UNDEFINED;
}

// The rest of what the game's code asks Steam for (achievements, the screenshot key, shutting
// down). On Android no library answers these and they do nothing; here they are answered with
// "no", so the game folder needs no Steam library at all and the game never talks to Steam.
#define PFO_WIN_STEAM_STUB(name) static void name(RValue& r, CInstance*, CInstance*, int, RValue*) { r.kind = VALUE_REAL; r.val = 0; }
PFO_WIN_STEAM_STUB(steam_get_achievement)
PFO_WIN_STEAM_STUB(steam_set_achievement)
PFO_WIN_STEAM_STUB(steam_reset_all_stats_achievements)
PFO_WIN_STEAM_STUB(steam_send_screenshot)
PFO_WIN_STEAM_STUB(steam_shutdown)
#undef PFO_WIN_STEAM_STUB

// The same wrapper as the Android build's (android/runner_bridge.inc), without its input and
// gamepad parts: the background network pump waits while the game is in here, and pfo_update is
// where save states hook into each step.
#define PFO_WIN_NAME(name) "?" #name "@@YAXAEAURValue@@PEAVCInstance@@1HPEAU1@@Z"
#define PFO_BUILTIN(name) \
    extern "C" __declspec(dllexport) void pfo_builtin_##name(RValue& r, CInstance* s, CInstance* o, int argc, RValue* arg) __asm__(PFO_WIN_NAME(name)); \
    void pfo_builtin_##name(RValue& r, CInstance* s, CInstance* o, int argc, RValue* arg) { \
        std::lock_guard<std::recursive_mutex> netLock(fakesteam::g_NetMutex); \
        fakesteam::g_LastGameCallMs = fakesteam::NowMs(); \
        const bool isUpdate = (void*)&name == (void*)&pfo_update; \
        if (isUpdate) { pfo_win_ui::Tick(); pfo_android::g_LastAdvanceArg = argc > 0 ? YYGetBool(arg, 0) : true; pfo_android::StateBeforeUpdate(s); } \
        if (isUpdate && pfo_android::g_SnapSkipUpdate) { r.kind = VALUE_UNDEFINED; return; } \
        name(r, s, o, argc, arg); \
        if (isUpdate) pfo_android::StateAfterUpdate(s); \
    }
#include "../android/builtins.inc"
PFO_BUILTIN(pfo_android_ask_code)
PFO_BUILTIN(pfo_android_screen)
PFO_BUILTIN(pfo_runner_mark)
PFO_BUILTIN(pfo_runner_rehide)
PFO_BUILTIN(pfo_runner_settle)
PFO_BUILTIN(steam_get_achievement)
PFO_BUILTIN(steam_set_achievement)
PFO_BUILTIN(steam_reset_all_stats_achievements)
PFO_BUILTIN(steam_send_screenshot)
PFO_BUILTIN(steam_shutdown)
#undef PFO_BUILTIN

// The runner's first call into the DLL.
extern "C" __declspec(dllexport) void pfo_win_extension_initialise(const YYRunnerInterface* functions, size_t size) __asm__("?YYExtensionInitialise@@YAXPEBUYYRunnerInterface@@_K@Z");
void pfo_win_extension_initialise(const YYRunnerInterface* functions, size_t size)
{
    fakesteam::g_HttpPost = &pfo_android::HttpPostWin;
    pfo_android::wincrash::Install();
    YYExtensionInitialise(functions, size);
    __android_log_print(ANDROID_LOG_INFO, "yoyo", "PFO: extension initialised (Windows, our netcode), interface %u bytes", static_cast<unsigned>(size));
    pfo_win_ui::Start();
}
