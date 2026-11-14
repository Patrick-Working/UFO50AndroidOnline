// The Windows build's menu, messages and loading screen: what the touch overlay (PFO_Android.java)
// is on Android, made for a keyboard and a controller instead of a touch screen. F1 opens the menu;
// so does pressing both of a controller's sticks in.
//
// What the overlay shows is here too, in this form: the saves with their pictures and what is in
// them, the box that asks when another player wants to load one (with that save's picture), the
// screen that covers the game while a save loads, who is playing. What only a touch screen needs
// (the buttons on the picture and arranging them) isn't.
//
// It has no thread of its own and never takes the keyboard from the game's window. The game's
// window procedure is wrapped: while the menu is open, key presses come here and not to the game,
// and the game is handed a controller at rest (namespace pad). The menu and the messages are drawn
// in a window of ours that lies over the game's picture, lets the mouse through and is never the
// active window. Everything here runs on the thread that owns the game's window (the game's own
// thread), started from pfo_update (Tick).
//
// What is done is written to the log (tag PFOMENU), so the tests can follow it.

namespace pfo_win_ui
{
    using pfo_win::Wide;
    using pfo_win::Utf8;

    // ---- what's kept between runs: <our folder>\menu.cfg ----
    std::string g_ChosenName;        // "" = the computer's name
    bool g_AutoSavePause = false;    // save by itself when the game is paused

    std::wstring ConfigPath() { return pfo_win::DataDir() + L"\\menu.cfg"; }

    // a private lobby's code, as typed: its lobby shows in the list from then on ("" if nothing usable was typed)
    std::string AddJoinCode(const std::string& typed)
    {
        std::string k;
        for (char ch : typed) if (k.size() < 12 && isalnum(static_cast<unsigned char>(ch))) k += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        k = fakesteam::NormalizeCode(k);
        if (k.empty()) return k;
        std::string tag = fakesteam::CodeTag(k);
        std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
        auto& tags = fakesteam::g_JoinTags;
        auto& codes = fakesteam::g_JoinCodes;
        for (size_t i = 0; i < tags.size(); i++) if (tags[i] == tag) { tags.erase(tags.begin() + i); codes.erase(codes.begin() + i); break; }
        tags.push_back(tag);
        codes.push_back(k);
        if (tags.size() > 8) { tags.erase(tags.begin()); codes.erase(codes.begin()); }
        return k;
    }

    // "" = the computer's name; the network code takes it over when we're not in a lobby
    void SetPlayerName(const std::string& typed)
    {
        std::string n;
        for (char ch : typed) if (n.size() < 16 && ch >= 32 && ch < 127) n += ch;
        while (!n.empty() && n.back() == ' ') n.pop_back();
        while (!n.empty() && n.front() == ' ') n.erase(n.begin());
        g_ChosenName = n;
        std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
        fakesteam::g_PendingName = n;
        fakesteam::g_NamePending = true;
    }

    std::string ShownName()
    {
        std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
        return fakesteam::g_NamePending && !fakesteam::g_PendingName.empty() ? fakesteam::g_PendingName : fakesteam::g_NameForUi;
    }

    void SaveConfig()
    {
        std::string text = "name=" + g_ChosenName + "\nupnp=" + (fakesteam::upnp::enabled ? "1" : "0") + "\nautosave=" + (g_AutoSavePause ? "1" : "0") + "\n";
        {
            std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
            for (auto& c : fakesteam::g_JoinCodes) text += "code=" + c + "\n";
        }
        std::wstring path = ConfigPath(), fresh = path + L".new";
        FILE* f = _wfopen(fresh.c_str(), L"wb");
        if (!f) return;
        bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
        ok = fclose(f) == 0 && ok;
        if (ok) MoveFileExW(fresh.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        else DeleteFileW(fresh.c_str());
    }

    void LoadConfig()
    {
        FILE* f = _wfopen(ConfigPath().c_str(), L"rb");
        if (!f) return;
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            std::string s = line;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            std::string key = s.substr(0, eq), value = s.substr(eq + 1);
            if (key == "name") { if (!value.empty()) SetPlayerName(value); }
            else if (key == "upnp") fakesteam::upnp::enabled = value != "0";
            else if (key == "autosave") g_AutoSavePause = value == "1";
            else if (key == "code") AddJoinCode(value);
        }
        fclose(f);
    }

    // ---- the game's state, as the overlay reads it ----
    // bits 0-3 how we're connected (0 none, 1 LAN, 2 DIRECT, 3 RELAY), bit 4 in a lobby, bit 5 hosting
    // it, bit 6 in an online game, bit 7 catching up, bits 8-15 input delay
    // (nativeNetStatus in android/runner_bridge.inc)
    int NetStatus()
    {
        int v = fakesteam::g_ConnKind & 15;
        if (fakesteam::g_InLobby) v |= 16;
        if (fakesteam::g_InLobby && fakesteam::g_IsOwner) v |= 32;
        if (g_Session.m_OnlineState >= EOnlineState::Online)
        {
            v |= 64;
            if (g_Session.m_CatchupState > 0) v |= 128;
            int i = g_Session.m_ClientIndex;
            if (i >= 0 && i < countof(g_Session.m_ClientData)) v |= (std::min(255, std::max(0, g_Session.m_ClientData[i].m_InputDelay)) & 255) << 8;
        }
        return v;
    }

    // -1 = not online, -2 = online without a seat (watching), else the player we are
    // (nativeOnlineSeat in android/runner_bridge.inc)
    int OnlineSeat()
    {
        if (g_Session.m_OnlineState < EOnlineState::Online) return -1;
        int c = g_Session.m_ClientIndex;
        if (c < 0 || c >= static_cast<int>(countof(g_Session.m_ClientData))) return -2;
        int seat = g_Session.m_ClientData[c].m_PlayerData.m_PlayerIndex;
        return seat >= 0 ? seat : -2;
    }

    void Request(int what, int slot)
    {
        pfo_android::g_StateRequestSlot = slot;
        pfo_android::g_StateRequest = what;
    }

    void AnswerVote(bool accept)
    {
        std::lock_guard<std::mutex> lock(pfo_android::g_PubMutex);
        pfo_android::g_VoteAnswerRequest = accept ? 1 : 2;
        if (pfo_android::g_PubVote[0]) pfo_android::g_PubVote[6] = 1;   // answered, as far as we are concerned
    }

    constexpr int c_VoteSeconds = 15, c_ProposalSeconds = 20;   // as the overlay's
    using pfo_android::c_AutoSlot;
    using pfo_android::c_QuickSlot;

    int g_Status = 0, g_Net = 0, g_Seat = -1;
    int g_Vote[11] = {};
    int g_LastResultSeq = -1, g_LastVoteSeq = -1, g_LastRejoinSeq = -1, g_LastSeat = -1;
    int g_WaitShown = 0, g_WaitedFor = 0;
    int g_PendingWhat = 0;           // what we last asked for: 2 save, 3 load, 4 rewind
    bool g_PendingAuto = false;
    bool g_PendingQuick = false;     // that save or load was the quick one (F5 / F9)
    ULONGLONG g_NoAutoUntil = 0, g_LastOfferCheck = 0;
    std::string g_RejoinOffer, g_RejoinDeclined;

    bool Solo() { return (g_Status & 1) != 0; }
    bool OnlineGame() { return (g_Net & 64) != 0 && !Solo(); }
    bool VoteShown() { return g_Vote[0] != 0 && g_Vote[6] == 0 && g_Seat >= 0 && g_Vote[2] != g_Seat; }

    // ---- our window over the game's ----
    HWND g_Game = nullptr, g_Panel = nullptr;
    WNDPROC g_GameProc = nullptr;
    UINT g_StartMessage = 0, g_PadMessage = 0;
    HFONT g_Font = nullptr;
    int g_FontHeight = 0, g_Alpha = 0;
    std::wstring g_Drawn;            // what the window shows now
    RECT g_Place = {};

    constexpr COLORREF c_Key = RGB(255, 0, 255);   // not drawn: the game shows through
    constexpr COLORREF c_Back = RGB(12, 12, 20), c_Text = RGB(255, 255, 255), c_Dim = RGB(150, 150, 160), c_Off = RGB(96, 96, 104), c_Attention = RGB(255, 190, 40);
    constexpr COLORREF c_Progress = RGB(0x00, 0x84, 0x56);
    // the players' colours, as the game's and the overlay's
    constexpr COLORREF c_Seat[4] = { RGB(0xE0, 0x3C, 0x32), RGB(0x00, 0x6A, 0xB4), RGB(0x63, 0xB3, 0x1D), RGB(0xE8, 0xEA, 0x4A) };
    COLORREF SeatColor(int seat) { return seat < 0 ? c_Dim : seat < 4 ? c_Seat[seat] : c_Text; }

    // a controller (its code is further down: namespace pad)
    bool g_PadConnected = false;     // one is there
    bool g_PadHints = false;         // the menu was last worked with one: the hints name its buttons
    std::atomic<bool> g_PadBlocked{false};   // the menu is open: the game gets a controller at rest

    // ---- messages: one that passes, and one that stays while its reason does ----
    const wchar_t* const c_StartText = L"F1: ONLINE MENU";
    std::wstring g_Banner, g_Sticky;
    bool g_BannerAttention = false, g_StickyAttention = false;
    ULONGLONG g_BannerUntil = 0;

    void Say(const std::wstring& text, int ms, bool attention = false)
    {
        g_Banner = text;
        g_BannerAttention = attention;
        g_BannerUntil = GetTickCount64() + ms;
        __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "message: %s", Utf8(text.c_str()).c_str());
    }

    std::wstring Seats(int mask)
    {
        std::wstring b;
        for (int p = 0; p < 8; p++) if (mask & (1 << p)) b += (b.empty() ? L"P" : L" P") + std::to_wstring(p + 1);
        return b;
    }

    // "ABOUT 12 S", "ABOUT 2 MIN 5 S"
    std::wstring About(long long ms)
    {
        long long sec = std::max(1ll, (ms + 500) / 1000);
        if (sec < 60) return L"ABOUT " + std::to_wstring(sec) + L" S";
        return L"ABOUT " + std::to_wstring(sec / 60) + L" MIN " + std::to_wstring(sec % 60) + L" S";
    }

    // text in a colour; a line of the boxes below is one, the players' names are several
    struct Run { std::wstring text; COLORREF color = c_Text; };
    Run Colored(const std::wstring& text, COLORREF color) { Run r; r.text = text; r.color = color; return r; }

    // ---- the saves (<our folder>\savestates\slot<N>.pfostate, described by slot<N>.json, pictured by slot<N>.png) ----
    struct SavePlayer { std::wstring name; int seat = -1; };
    struct SaveInfo
    {
        int id = -1, game = 0;
        long long savedAt = 0, frame = 0, gameFrames = 0;
        bool online = false;
        std::wstring label, title, kind;
        std::string mix;
        std::vector<SavePlayer> players;
    };
    std::vector<SaveInfo> g_Saves;

    std::wstring SaveFile(int id, const wchar_t* ext) { return pfo_win::DataDir() + L"\\savestates\\slot" + std::to_wstring(id) + ext; }

    long long JsonNumber(const std::string& j, const char* key, long long fallback)
    {
        size_t p = j.find(std::string("\"") + key + "\":");
        if (p == std::string::npos) return fallback;
        const char* at = j.c_str() + p + strlen(key) + 3;
        if (!strncmp(at, "true", 4)) return 1;
        if (!strncmp(at, "false", 5)) return 0;
        return strtoll(at, nullptr, 10);
    }

    std::string JsonText(const std::string& j, const char* key)
    {
        size_t p = j.find(std::string("\"") + key + "\":\"");
        std::string out;
        if (p == std::string::npos) return out;
        for (p += strlen(key) + 4; p < j.size() && j[p] != '"'; p++)
        {
            if (j[p] == '\\' && p + 1 < j.size())
            {
                p++;
                if (j[p] == 'u' && p + 4 < j.size()) { unsigned code = static_cast<unsigned>(strtoul(j.substr(p + 1, 4).c_str(), nullptr, 16)); out += code >= 32 && code < 127 ? static_cast<char>(code) : '?'; p += 4; }
                else out += j[p] == 'n' || j[p] == 't' || j[p] == 'r' ? ' ' : j[p];
            }
            else out += j[p];
        }
        return out;
    }

    std::wstring Upper(std::wstring s)
    {
        for (wchar_t& c : s) if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - 32);
        return s;
    }

    // "TODAY 14:05", "NOV 3 2026 14:05"; brief (a row of the list): "TODAY 14:05", "NOV 3"
    std::wstring When(long long unixTime, bool brief = false)
    {
        time_t t = static_cast<time_t>(unixTime), nowT = time(nullptr);
        struct tm then = {}, now = {};
        if (localtime_s(&then, &t) != 0 || localtime_s(&now, &nowT) != 0) return L"";
        wchar_t buf[64];
        static const wchar_t* c_Month[] = { L"JAN", L"FEB", L"MAR", L"APR", L"MAY", L"JUN", L"JUL", L"AUG", L"SEP", L"OCT", L"NOV", L"DEC" };
        if (then.tm_year == now.tm_year && then.tm_yday == now.tm_yday) swprintf(buf, countof(buf), L"TODAY %d:%02d", then.tm_hour, then.tm_min);
        else if (brief && then.tm_year == now.tm_year) swprintf(buf, countof(buf), L"%ls %d", c_Month[then.tm_mon % 12], then.tm_mday);
        else if (brief) swprintf(buf, countof(buf), L"%ls %d %d", c_Month[then.tm_mon % 12], then.tm_mday, then.tm_year + 1900);
        else swprintf(buf, countof(buf), L"%ls %d %d %d:%02d", c_Month[then.tm_mon % 12], then.tm_mday, then.tm_year + 1900, then.tm_hour, then.tm_min);
        return buf;
    }

    std::wstring Clock(long long frames)
    {
        long long s = frames / 60, h = s / 3600, m = (s / 60) % 60;
        s %= 60;
        wchar_t buf[48];
        if (h > 0) swprintf(buf, countof(buf), L"%lld:%02lld:%02lld", h, m, s);
        else swprintf(buf, countof(buf), L"%lld:%02lld", m, s);
        return buf;
    }

    // which of this match's online saves a slot holds (0: not one of this match's)
    int MatchSave(int slot)
    {
        std::lock_guard<std::mutex> lock(pfo_android::g_PubMutex);
        for (auto& kv : pfo_android::g_PubMatchSaves) if (kv.first == slot) return kv.second;
        return 0;
    }

    bool ReadSave(int id, SaveInfo& s)
    {
        FILE* f = _wfopen(SaveFile(id, L".json").c_str(), L"rb");
        if (!f) return false;
        std::string j;
        char buf[4096];
        for (size_t n; j.size() < 256 * 1024 && (n = fread(buf, 1, sizeof(buf), f)) > 0; ) j.append(buf, n);
        fclose(f);
        s = SaveInfo();
        s.id = id;
        s.savedAt = JsonNumber(j, "savedAt", 0);
        s.frame = JsonNumber(j, "frame", 0);
        s.gameFrames = JsonNumber(j, "gameFrames", 0);
        s.game = static_cast<int>(JsonNumber(j, "game", 0));
        s.mix = JsonText(j, "mix");
        long long clients = JsonNumber(j, "clients", 1);
        long long count = std::max(clients, JsonNumber(j, "numPlayers", 1));
        s.online = JsonNumber(j, "online", clients > 1 ? 1 : 0) != 0;
        s.title = Upper(Wide(JsonText(j, "title").c_str()));
        if (s.title.empty()) s.title = s.game == 0 ? L"THE LIBRARY" : L"GAME " + std::to_wstring(s.game);
        s.kind = (count <= 1 ? std::wstring(L"SINGLE PLAYER") : std::to_wstring(count) + L" PLAYERS") + (s.online ? L"  -  ONLINE" : L"  -  OFFLINE");
        // "players":[{"name":"...","seat":0},...] (a seat of -1: watching)
        size_t at = j.find("\"players\":[");
        if (at != std::string::npos)
        {
            at += 11;
            while (at < j.size() && j[at] != ']' && s.players.size() < 8)
            {
                if (j[at] != '{') { at++; continue; }
                size_t end = at + 1;
                for (bool inText = false; end < j.size(); end++)
                {
                    if (inText) { if (j[end] == '\\') end++; else if (j[end] == '"') inText = false; }
                    else if (j[end] == '"') inText = true;
                    else if (j[end] == '}') break;
                }
                if (end >= j.size()) break;
                std::string one = j.substr(at, end - at + 1);
                SavePlayer p;
                p.name = Upper(Wide(JsonText(one, "name").c_str()));
                p.seat = static_cast<int>(JsonNumber(one, "seat", -1));
                if (!p.name.empty()) s.players.push_back(p);
                at = end + 1;
            }
        }
        s.label = (id == c_AutoSlot ? L"AUTO: " : id == c_QuickSlot ? L"QUICK: " : L"") + s.title + L"   " + When(s.savedAt, true);
        return true;
    }

    void RefreshSaves()
    {
        g_Saves.clear();
        WIN32_FIND_DATAW found = {};
        HANDLE h = FindFirstFileW((pfo_win::DataDir() + L"\\savestates\\slot*.pfostate").c_str(), &found);
        if (h == INVALID_HANDLE_VALUE) return;
        do
        {
            int id = -1; wchar_t rest[32] = {};
            if (swscanf(found.cFileName, L"slot%d.%31ls", &id, rest) != 2 || wcscmp(rest, L"pfostate") != 0 || id < 0) continue;
            SaveInfo s;
            if (ReadSave(id, s)) g_Saves.push_back(s);
        }
        while (FindNextFileW(h, &found));
        FindClose(h);
        std::sort(g_Saves.begin(), g_Saves.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.savedAt != b.savedAt ? a.savedAt > b.savedAt : a.id > b.id; });
    }

    int NextSaveId()
    {
        int max = 0;
        WIN32_FIND_DATAW found = {};
        HANDLE h = FindFirstFileW((pfo_win::DataDir() + L"\\savestates\\slot*.pfostate").c_str(), &found);
        if (h == INVALID_HANDLE_VALUE) return 1;
        do
        {
            int id = -1; wchar_t rest[32] = {};
            if (swscanf(found.cFileName, L"slot%d.%31ls", &id, rest) == 2 && wcscmp(rest, L"pfostate") == 0 && id != c_QuickSlot && id > max) max = id;
        }
        while (FindNextFileW(h, &found));
        FindClose(h);
        return max + 1;
    }

    // one save, for the boxes that show it without the list being open (read again after a second)
    std::map<int, std::pair<ULONGLONG, SaveInfo>> g_SaveCache;
    const SaveInfo* SaveOf(int id)
    {
        if (id < 0) return nullptr;
        ULONGLONG now = GetTickCount64();
        auto& e = g_SaveCache[id];
        if (e.first == 0 || now - e.first > 1000)
        {
            e.first = std::max<ULONGLONG>(1, now);
            if (!ReadSave(id, e.second)) e.second = SaveInfo();
        }
        return e.second.id == id ? &e.second : nullptr;
    }

    // how long loading it takes (ms), as the overlay reckons: a snapshot of it is at hand, or its
    // session is replayed
    int LoadEstimate(const SaveInfo& s)
    {
        {
            std::lock_guard<std::mutex> lock(pfo_android::g_PubMutex);
            for (int id : pfo_android::g_PubReadySlots) if (id == s.id) return 1200;
        }
        if (!s.mix.empty()) return 800 + pfo_android::MixEstimateMs(s.mix.c_str());
        return 800 + static_cast<int>(std::min(3600000ll, s.frame * 1000 / std::max(100, pfo_android::g_ReplayFpsPub.load())));
    }

    std::wstring Played(const SaveInfo& s, bool brief)
    {
        if (s.game == 0) return brief ? L"LIBRARY" : L"IN THE LIBRARY";
        return Clock(s.gameFrames) + (brief ? L" IN" : L" INTO THE GAME");
    }

    // ---- the saves' pictures ----
    // 384x216 PNGs, as the overlay's (a save offered in a match brings its picture along, whichever
    // kind of device took it). Windows' own GDI+ reads and writes them; the game has it loaded.
    namespace gdip
    {
        struct StartupInput { UINT32 version; void* debugCallback; BOOL noBackgroundThread; BOOL noCodecs; };
        struct BitmapData { UINT width, height; INT stride; INT format; void* scan0; UINT_PTR reserved; };
        struct RectI { INT x, y, w, h; };
        constexpr INT c_Rgb32 = 0x00022009;   // PixelFormat32bppRGB
        const CLSID c_Png = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };   // its PNG encoder

        int (WINAPI* Startup)(ULONG_PTR*, const StartupInput*, void*) = nullptr;
        int (WINAPI* FromScan0)(INT, INT, INT, INT, BYTE*, void**) = nullptr;
        int (WINAPI* FromStream)(void*, void**) = nullptr;
        int (WINAPI* SaveToFile)(void*, const WCHAR*, const CLSID*, const void*) = nullptr;
        int (WINAPI* GetWidth)(void*, UINT*) = nullptr;
        int (WINAPI* GetHeight)(void*, UINT*) = nullptr;
        int (WINAPI* LockBits)(void*, const RectI*, UINT, INT, BitmapData*) = nullptr;
        int (WINAPI* UnlockBits)(void*, BitmapData*) = nullptr;
        int (WINAPI* Dispose)(void*) = nullptr;
        HRESULT (WINAPI* StreamOnGlobal)(HGLOBAL, BOOL, void**) = nullptr;
        int g_State = 0;   // 1 ready, -1 not to be had

        template <typename T> bool Get(HMODULE lib, const char* name, T& fn)
        {
            fn = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(lib, name)));
            return fn != nullptr;
        }

        bool Ready()
        {
            if (g_State != 0) return g_State > 0;
            g_State = -1;
            HMODULE lib = LoadLibraryW(L"gdiplus.dll"), ole = LoadLibraryW(L"ole32.dll");
            bool ok = lib && ole && Get(lib, "GdiplusStartup", Startup) && Get(lib, "GdipCreateBitmapFromScan0", FromScan0) && Get(lib, "GdipCreateBitmapFromStream", FromStream)
                && Get(lib, "GdipSaveImageToFile", SaveToFile) && Get(lib, "GdipGetImageWidth", GetWidth) && Get(lib, "GdipGetImageHeight", GetHeight)
                && Get(lib, "GdipBitmapLockBits", LockBits) && Get(lib, "GdipBitmapUnlockBits", UnlockBits) && Get(lib, "GdipDisposeImage", Dispose)
                && Get(ole, "CreateStreamOnHGlobal", StreamOnGlobal);
            if (ok)
            {
                StartupInput in = { 1, nullptr, FALSE, FALSE };
                ULONG_PTR token = 0;
                ok = Startup(&token, &in, nullptr) == 0;
            }
            if (!ok) __android_log_print(ANDROID_LOG_WARN, "PFOMENU", "pictures: GDI+ can't be used: the saves have no pictures");
            g_State = ok ? 1 : -1;
            return ok;
        }

        // a picture file's pixels (0x00RRGGBB, top row first)
        bool Load(const std::wstring& path, int& w, int& h, std::vector<uint32>& px)
        {
            if (!Ready()) return false;
            // read whole first, and shared: nobody writing or replacing the file is in our way, nor we in theirs
            HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (f == INVALID_HANDLE_VALUE) return false;
            DWORD size = GetFileSize(f, nullptr), got = 0;
            HGLOBAL mem = size > 0 && size < 4 * 1024 * 1024 ? GlobalAlloc(GMEM_MOVEABLE, size) : nullptr;
            bool ok = false;
            if (mem)
            {
                void* p = GlobalLock(mem);
                ok = p && ReadFile(f, p, size, &got, nullptr) && got == size;
                if (p) GlobalUnlock(mem);
            }
            CloseHandle(f);
            void* stream = nullptr;
            if (!ok || StreamOnGlobal(mem, TRUE, &stream) != 0 || !stream)
            {
                if (mem) GlobalFree(mem);
                return false;
            }
            void* bmp = nullptr;
            UINT bw = 0, bh = 0;
            ok = FromStream(stream, &bmp) == 0 && bmp && GetWidth(bmp, &bw) == 0 && GetHeight(bmp, &bh) == 0 && bw > 0 && bh > 0 && bw <= 2048 && bh <= 2048;
            if (ok)
            {
                RectI all = { 0, 0, static_cast<INT>(bw), static_cast<INT>(bh) };
                BitmapData d = {};
                ok = LockBits(bmp, &all, 1 /* to read */, c_Rgb32, &d) == 0 && d.scan0;
                if (ok)
                {
                    px.resize(static_cast<size_t>(bw) * bh);
                    for (UINT y = 0; y < bh; y++) memcpy(&px[static_cast<size_t>(y) * bw], static_cast<const uint8*>(d.scan0) + static_cast<ptrdiff_t>(y) * d.stride, static_cast<size_t>(bw) * 4);
                    UnlockBits(bmp, &d);
                    w = static_cast<int>(bw);
                    h = static_cast<int>(bh);
                }
            }
            if (bmp) Dispose(bmp);
            (*reinterpret_cast<ULONG (WINAPI***)(void*)>(stream))[2](stream);   // IUnknown::Release (the memory goes with it)
            return ok;
        }

        bool SavePng(const std::wstring& path, int w, int h, void* bits)
        {
            if (!Ready()) return false;
            void* bmp = nullptr;
            if (FromScan0(w, h, w * 4, c_Rgb32, static_cast<BYTE*>(bits), &bmp) != 0 || !bmp) return false;
            bool ok = SaveToFile(bmp, path.c_str(), &c_Png, nullptr) == 0;
            Dispose(bmp);
            return ok;
        }
    }

    struct Picture
    {
        int w = 0, h = 0;
        std::vector<uint32> px, dim;     // as it is, and darkened (behind the loading screen's text)
        ULONGLONG stamp = 0, checkedAt = 0;
    };
    std::map<int, Picture> g_Pictures;

    // a save's picture (nullptr: it has none). Its file is looked at again after a second, so a
    // picture that arrives or changes shows.
    const Picture* PictureOf(int slot)
    {
        if (slot < 0) return nullptr;
        ULONGLONG now = GetTickCount64();
        Picture& p = g_Pictures[slot];
        if (p.checkedAt == 0 || now - p.checkedAt >= 1000)
        {
            p.checkedAt = std::max<ULONGLONG>(1, now);
            WIN32_FILE_ATTRIBUTE_DATA a = {};
            ULONGLONG stamp = 0;
            std::wstring path = SaveFile(slot, L".png");
            if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
                stamp = (((static_cast<ULONGLONG>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime) + a.nFileSizeLow) | 1;
            if (stamp != p.stamp)
            {
                p.stamp = stamp;
                p.px.clear(); p.dim.clear();
                p.w = p.h = 0;
                if (stamp != 0 && gdip::Load(path, p.w, p.h, p.px))
                {
                    // (our window doesn't draw one colour: the game shows through there)
                    for (uint32& c : p.px) { c &= 0xffffff; if (c == 0xff00ff) c = 0xfe00ff; }
                    p.dim = p.px;
                    for (uint32& c : p.dim) c = ((((c >> 16) & 255) * 140 / 255) << 16) | ((((c >> 8) & 255) * 140 / 255) << 8) | ((c & 255) * 140 / 255);
                }
                else p.w = p.h = 0;
            }
        }
        return p.w > 0 ? &p : nullptr;
    }

    void DrawPicture(HDC dc, const Picture& p, const RECT& r, bool dim)
    {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = p.w;
        bi.bmiHeader.biHeight = -p.h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, p.w, p.h, (dim ? p.dim : p.px).data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    }

    // A save was just made: the game's picture as it is on screen becomes the save's (what the game
    // draws, without its border). Taken from the game's window, which our messages aren't part of.
    void TakePicture(int slot)
    {
        constexpr int c_W = 384, c_H = 216;
        RECT c = {};
        if (slot < 0 || !g_Game || IsIconic(g_Game) || !GetClientRect(g_Game, &c) || c.right < 16 || c.bottom < 16 || !gdip::Ready()) return;
        const int cw = c.right, ch = c.bottom;
        HDC screen = GetDC(nullptr);
        HDC whole = CreateCompatibleDC(screen), shrunk = CreateCompatibleDC(screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bi.bmiHeader.biWidth = cw;
        bi.bmiHeader.biHeight = -ch;
        void* wholeBits = nullptr; void* smallBits = nullptr;
        HBITMAP wholeBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &wholeBits, nullptr, 0);
        bi.bmiHeader.biWidth = c_W;
        bi.bmiHeader.biHeight = -c_H;
        HBITMAP smallBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &smallBits, nullptr, 0);
        const char* problem = "no memory";
        if (whole && shrunk && wholeBmp && smallBmp)
        {
            HGDIOBJ old1 = SelectObject(whole, wholeBmp), old2 = SelectObject(shrunk, smallBmp);
            problem = "the window can't be read";
            if (PrintWindow(g_Game, whole, 1 /* the inside */ | 2 /* as it is shown */))
            {
                // where in the window the game draws (pfo_android_screen: x, y, w, h, the window's w, h)
                int r[6];
                for (int i = 0; i < 6; i++) r[i] = pfo_android::g_ScreenRect[i].load();
                int x = 0, y = 0, w = cw, h = ch;
                if (r[2] > 0 && r[3] > 0 && r[4] > 0 && r[5] > 0)
                {
                    x = std::max(0, std::min(cw - 1, static_cast<int>(static_cast<long long>(r[0]) * cw / r[4])));
                    y = std::max(0, std::min(ch - 1, static_cast<int>(static_cast<long long>(r[1]) * ch / r[5])));
                    w = std::max(1, std::min(cw - x, static_cast<int>(static_cast<long long>(r[2]) * cw / r[4])));
                    h = std::max(1, std::min(ch - y, static_cast<int>(static_cast<long long>(r[3]) * ch / r[5])));
                }
                SetStretchBltMode(shrunk, HALFTONE);
                SetBrushOrgEx(shrunk, 0, 0, nullptr);
                StretchBlt(shrunk, 0, 0, c_W, c_H, whole, x, y, w, h, SRCCOPY);
                GdiFlush();
                // (a window that gave nothing comes out all one colour)
                const uint32* px = static_cast<const uint32*>(smallBits);
                bool blank = true;
                for (int i = 1; i < c_W * c_H && blank; i++) blank = (px[i] & 0xffffff) == (px[0] & 0xffffff);
                std::wstring path = SaveFile(slot, L".png"), fresh = path + L".new";
                if (blank) problem = "the window gave no picture";
                else if (!gdip::SavePng(fresh, c_W, c_H, smallBits)) { problem = "it can't be written"; DeleteFileW(fresh.c_str()); }
                else if (!MoveFileExW(fresh.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { problem = "it can't be put in place"; DeleteFileW(fresh.c_str()); }
                else problem = nullptr;
            }
            SelectObject(whole, old1);
            SelectObject(shrunk, old2);
        }
        if (wholeBmp) DeleteObject(wholeBmp);
        if (smallBmp) DeleteObject(smallBmp);
        if (whole) DeleteDC(whole);
        if (shrunk) DeleteDC(shrunk);
        ReleaseDC(nullptr, screen);
        if (problem) __android_log_print(ANDROID_LOG_WARN, "PFOMENU", "picture: slot %d has none (%s)", slot, problem);
        else __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "picture: slot %d saved", slot);
        auto it = g_Pictures.find(slot);
        if (it != g_Pictures.end()) it->second.checkedAt = 0;
    }

    // ---- the menu ----
    enum { PageClosed, PageMain, PageSaves, PageConfirm, PageName, PageCode };
    enum { ActNone, ActClose, ActBack, ActSolo, ActSave, ActSaves, ActLoad, ActRewind, ActAccept, ActDecline, ActRejoin, ActForget, ActAutoSave, ActCopyCode, ActCode, ActName, ActUpnp, ActQuickSave, ActQuickLoad };
    struct Item { std::wstring text; int action = ActNone; int arg = 0; bool enabled = true; };

    // a box about one save: lines of text, its picture, the names of who was playing
    struct Card
    {
        bool on = false;
        int slot = -1;                   // the save whose picture it shows
        int pictureAfter = 0;            // how many lines come above the picture
        int playersAfter = -1;           // ...and above the players' names (-1: no names)
        std::vector<Run> lines, players;
    };

    int g_Page = PageClosed;
    bool g_TextFromGame = false;      // the code box was opened by JOIN BY CODE: leaving it closes the menu
    std::vector<Item> g_Items;
    int g_Selected = 0;
    std::string g_Typed;
    std::wstring g_Title, g_Sub, g_Hint;
    std::vector<Run> g_Players;       // main page: who is in the online game
    Card g_Details;                   // saves page: the save the choice is on
    int g_DeleteArmed = -1;
    ULONGLONG g_DeleteArmedUntil = 0, g_SavesReadAt = 0;
    int g_ConfirmAction = ActNone, g_ConfirmArg = 0, g_ConfirmEst = 0;   // the slow load or rewind PageConfirm asks about
    // the loading screen: which save is being loaded (-1: not known, or a rewind) and what was asked (3 load, 4 rewind)
    int g_CoverSlot = -1, g_CoverWhat = 0;

    void Add(const std::wstring& text, int action, int arg = 0, bool enabled = true)
    {
        Item it;
        it.text = text; it.action = action; it.arg = arg; it.enabled = enabled;
        g_Items.push_back(it);
    }

    // the invite code of the lobby we're in: a host's own; a joiner's once the game's SHARE CODE
    // worked it out (from the codes entered here)
    std::string LobbyCode()
    {
        if (!(g_Net & 16)) return std::string();
        std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
        return (g_Net & 32) ? fakesteam::g_LobbyCode : fakesteam::g_ShareCode;
    }

    std::wstring StatusLine()
    {
        static const wchar_t* c_Kind[] = { L"", L"LAN", L"DIRECT", L"RELAY" };
        if (Solo()) return L"SOLO PLAY: SAVES AND REWIND ARE ON";
        std::wstring s;
        if (g_Net & 64)
        {
            s = g_Seat >= 0 ? L"ONLINE GAME - YOU ARE P" + std::to_wstring(g_Seat + 1) : std::wstring(L"ONLINE GAME - WATCHING");
            int delay = (g_Net >> 8) & 255;
            if (delay > 0) s += L" - DELAY " + std::to_wstring(delay);
        }
        else if (g_Net & 16) s = (g_Net & 32) ? L"HOSTING A LOBBY" : L"IN A LOBBY";
        else return L"NOT ONLINE";
        int kind = g_Net & 15;
        if (kind >= 1 && kind <= 3) s += std::wstring(L" - ") + c_Kind[kind];
        if ((g_Net & 16) && !(g_Net & 64))
        {
            // the lobby's invite code, to read out or type (COPY THE INVITE CODE puts it on the clipboard)
            std::string code = LobbyCode();
            if (!code.empty()) s += L" - CODE " + Wide(code.c_str());
        }
        return s;
    }

    // who is playing the online game, each in their colour: "P1 NAME (YOU)" (the overlay's chips)
    std::vector<Run> OnlinePlayers()
    {
        std::vector<Run> out;
        std::string info;
        { std::lock_guard<std::mutex> lock(pfo_android::g_SeatMutex); info = pfo_android::g_SeatInfo; }
        // "<our client>;<client of seat 0>,...,<of seat 7>;<name of client 0>|<of client 1>|..."
        size_t a = info.find(';'), b = a == std::string::npos ? a : info.find(';', a + 1);
        if (b == std::string::npos) return out;
        int ours = atoi(info.c_str());
        std::vector<std::string> names;
        for (size_t at = b + 1; at < info.size(); )
        {
            size_t bar = info.find('|', at);
            names.push_back(info.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
            if (bar == std::string::npos) break;
            at = bar + 1;
        }
        size_t at = a + 1;
        for (int seat = 0; seat < 8 && at < b; seat++)
        {
            int client = atoi(info.c_str() + at);
            if (client >= 0)
            {
                std::wstring text = L"P" + std::to_wstring(seat + 1);
                if (client < static_cast<int>(names.size()) && !names[client].empty()) text += L" " + Upper(Wide(names[client].c_str()));
                if (client == ours) text += L" (YOU)";
                out.push_back(Colored(text, SeatColor(seat)));
            }
            size_t comma = info.find(',', at);
            if (comma == std::string::npos || comma > b) break;
            at = comma + 1;
        }
        return out;
    }

    std::wstring RejoinText()
    {
        // "<seconds ago>|<our seat>|<name of seat 0>|<seat 1>|..."
        std::vector<std::string> f;
        size_t at = 0;
        for (;;)
        {
            size_t bar = g_RejoinOffer.find('|', at);
            f.push_back(g_RejoinOffer.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
            if (bar == std::string::npos) break;
            at = bar + 1;
        }
        int seat = f.size() > 1 ? atoi(f[1].c_str()) : 0;
        std::wstring others;
        for (size_t i = 2; i < f.size(); i++) if (static_cast<int>(i) - 2 != seat && !f[i].empty()) others += (others.empty() ? L"" : L", ") + Upper(Wide(f[i].c_str()));
        return L"YOU WERE P" + std::to_wstring(seat + 1) + (others.empty() ? L"" : L" WITH " + others);
    }

    void Build()
    {
        int action = g_Selected >= 0 && g_Selected < static_cast<int>(g_Items.size()) ? g_Items[g_Selected].action : ActNone;
        int arg = action != ActNone ? g_Items[g_Selected].arg : 0;
        g_Items.clear();
        g_Sub.clear();
        g_Players.clear();
        g_Details = Card();
        const bool restoring = (g_Status & 4) != 0, inLobby = (g_Net & 16) != 0, byPad = g_PadHints;
        if (g_Page == PageMain)
        {
            g_Title = L"UFO 50 ONLINE";
            g_Sub = StatusLine();
            g_Hint = byPad ? L"D-PAD: MOVE   A: CHOOSE   B: CLOSE" : L"UP / DOWN   ENTER OR Z: CHOOSE   ESC OR X: CLOSE";
            if ((g_Net & 64) != 0 && !Solo()) g_Players = OnlinePlayers();
            if (VoteShown()) { Add(L"ACCEPT THE LOAD P" + std::to_wstring(g_Vote[2] + 1) + L" ASKED FOR", ActAccept); Add(L"DECLINE THE LOAD", ActDecline); }
            if (!g_RejoinOffer.empty()) { Add(L"REJOIN YOUR MATCH (" + RejoinText() + L")", ActRejoin); Add(L"FORGET THAT MATCH", ActForget); }
            if (Solo() || OnlineGame()) Add(OnlineGame() ? L"SAVE (EVERY PLAYER'S GAME SAVES)" : L"SAVE", ActSave, 0, (g_Status & 2) != 0);
            if (Solo()) Add(L"QUICK SAVE (F5)", ActQuickSave, 0, (g_Status & 2) != 0);
            Add(L"LOAD A SAVE...", ActSaves, 0, !restoring);
            if (!OnlineGame() && SaveOf(c_QuickSlot)) Add(L"QUICK LOAD (F9)", ActQuickLoad, 0, !restoring);
            if (Solo())
            {
                static const int c_Seconds[3] = { 15, 30, 60 };
                for (int i = 0; i < 3; i++)
                {
                    int est = pfo_android::g_RewindEst[i].load();
                    Add(L"REWIND " + std::to_wstring(c_Seconds[i]) + L" S   " + (est >= 0 ? About(est) + L" TO LOAD" : std::wstring(L"NOTHING THAT FAR BACK YET")), ActRewind, c_Seconds[i], est >= 0 && !restoring);
                }
            }
            else if (!OnlineGame()) Add(L"START SOLO PLAY (TURNS SAVES AND REWIND ON)", ActSolo, 0, !inLobby && (g_Net & 64) == 0 && !restoring);
            Add(std::wstring(L"SAVE WHEN THE GAME IS PAUSED: ") + (g_AutoSavePause ? L"ON" : L"OFF"), ActAutoSave);
            std::string code = LobbyCode();
            if (!code.empty()) Add(L"COPY THE INVITE CODE (" + Wide(code.c_str()) + L")", ActCopyCode);
            Add(L"ENTER A JOIN CODE...", ActCode, 0, (g_Net & 64) == 0);
            Add(L"PLAYER NAME: " + Upper(Wide(ShownName().c_str())), ActName, 0, !OnlineGame());
            Add(std::wstring(L"UPNP (A HOST OPENS ITS ROUTER PORT): ") + (fakesteam::upnp::enabled ? L"ON" : L"OFF"), ActUpnp);
            Add(L"CLOSE", ActClose);
        }
        else if (g_Page == PageSaves)
        {
            ULONGLONG now = GetTickCount64();
            if (now - g_SavesReadAt > 2000) { RefreshSaves(); g_SavesReadAt = now; }
            if (g_DeleteArmed >= 0 && now > g_DeleteArmedUntil) g_DeleteArmed = -1;
            g_Title = L"LOAD A SAVE";
            g_Hint = byPad ? L"D-PAD: MOVE   A: LOAD   X: DELETE   B: BACK" : L"UP / DOWN   ENTER OR Z: LOAD   DEL: DELETE   ESC OR X: BACK";
            for (auto& s : g_Saves)
                Add(s.id != g_DeleteArmed ? s.label : byPad ? L"DELETE IT? PRESS X AGAIN" : L"DELETE IT? PRESS DEL AGAIN", ActLoad, s.id, !restoring);
            if (g_Saves.empty()) { Add(L"NO SAVES YET", ActNone, 0, false); g_Hint = byPad ? L"B: BACK" : L"ESC OR X: BACK"; }
        }
        else if (g_Page == PageConfirm)
        {
            // a load that takes a while (it replays the session up to the save): asked about first
            const bool rewind = g_ConfirmAction == ActRewind;
            g_Title = rewind ? L"REWIND " + std::to_wstring(g_ConfirmArg) + L" SECONDS?" : std::wstring(L"LOAD THIS SAVE?");
            g_Sub = About(g_ConfirmEst) + (rewind ? L" TO LOAD" : L" TO LOAD (IT REPLAYS THE SESSION)");
            g_Hint = byPad ? L"A: CHOOSE   B: BACK" : L"ENTER OR Z: CHOOSE   ESC OR X: BACK";
            Add(rewind ? L"REWIND" : L"LOAD", g_ConfirmAction, g_ConfirmArg, !restoring);
            Add(L"BACK", ActBack);
        }
        else if (g_Page == PageName || g_Page == PageCode)
        {
            g_Title = g_Page == PageName ? L"PLAYER NAME" : L"JOIN CODE";
            if (byPad) g_Hint = L"TYPE ON THE KEYBOARD   ENTER OR A: OK   B: BACK";
            else g_Hint = g_Page == PageName ? L"ENTER: OK (EMPTY: THIS COMPUTER'S NAME)   ESC: BACK   CTRL+V: PASTE" : L"TYPE THE HOST'S CODE   ENTER: OK   ESC: BACK   CTRL+V: PASTE";
            Add(L"> " + Wide(g_Typed.c_str()) + L"_", ActNone);
        }
        // the choice stays on what it was on
        int n = static_cast<int>(g_Items.size());
        for (int i = 0; i < n; i++) if (action != ActNone && g_Items[i].action == action && g_Items[i].arg == arg) g_Selected = i;
        if (g_Selected >= n) g_Selected = n - 1;
        if (g_Selected < 0) g_Selected = 0;
        for (int i = 0; i < n && !g_Items[g_Selected].enabled; i++) g_Selected = (g_Selected + 1) % n;

        // the save the choice is on, in full
        if (g_Page == PageSaves && n > 0 && g_Items[g_Selected].action == ActLoad)
            for (auto& s : g_Saves)
            {
                if (s.id != g_Items[g_Selected].arg) continue;
                g_Details.on = true;
                g_Details.slot = s.id;
                g_Details.lines.push_back(Colored(s.title, c_Text));
                g_Details.lines.push_back(Colored(s.kind + (MatchSave(s.id) > 0 ? L"  -  THIS MATCH" : L""), c_Dim));
                g_Details.playersAfter = 2;
                for (auto& p : s.players) g_Details.players.push_back(Colored(p.name, SeatColor(p.seat)));
                g_Details.lines.push_back(Colored(Played(s, false), c_Text));
                g_Details.lines.push_back(Colored(L"SAVED " + When(s.savedAt), c_Dim));
                if (!OnlineGame()) g_Details.lines.push_back(Colored(L"LOADS IN " + About(LoadEstimate(s)), c_Dim));
                break;
            }
    }

    // select: the row the choice starts on (back from a page: the row that led there)
    void Open(int page, int select = ActNone, int selectArg = 0)
    {
        if (g_Page == PageClosed && page != PageClosed) __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "menu: open (%s)", pfo_win::Utf8(StatusLine().c_str()).c_str());
        if (page == PageClosed && g_Page != PageClosed) __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "menu: closed");
        if (select == ActNone && page == PageMain) select = g_Page == PageSaves ? ActSaves : g_Page == PageName ? ActName : g_Page == PageCode ? ActCode : ActNone;
        g_Page = page;
        g_PadBlocked = page != PageClosed;
        g_Selected = 0;
        g_Items.clear();
        g_DeleteArmed = -1;
        if (page == PageSaves) g_SavesReadAt = 0;
        if (page == PageName) g_Typed = g_ChosenName;
        if (page == PageCode) g_Typed.clear();
        if (page != PageCode) g_TextFromGame = false;
        if (page == PageClosed) return;
        Build();
        for (size_t i = 0; i < g_Items.size(); i++)
            if (select != ActNone && g_Items[i].action == select && g_Items[i].arg == selectArg && g_Items[i].enabled) { g_Selected = static_cast<int>(i); Build(); break; }
    }

    void CopyInvite()
    {
        std::string code = LobbyCode();
        if (code.empty()) { std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex); code = fakesteam::g_ShareCode; }   // (the game's SHARE CODE just set it)
        if (code.empty()) { Say(L"THIS LOBBY HAS NO INVITE CODE", 2000); return; }
        std::wstring w = Wide(code.c_str());
        bool ok = false;
        if (OpenClipboard(nullptr))
        {
            EmptyClipboard();
            if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t)))
            {
                if (void* p = GlobalLock(mem))
                {
                    memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(mem);
                    ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
                }
                if (!ok) GlobalFree(mem);
            }
            CloseClipboard();
        }
        Say(ok ? L"INVITE CODE " + w + L" COPIED: PASTE IT TO YOUR FRIENDS" : L"INVITE CODE: " + w, 5000);
    }

    std::string ClipboardText()
    {
        std::string out;
        if (!OpenClipboard(nullptr)) return out;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
        {
            if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h)))
            {
                for (size_t i = 0; w[i] && i < 64; i++) if (w[i] >= 32 && w[i] < 127) out += static_cast<char>(w[i]);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return out;
    }

    void TypeChar(char c)
    {
        if (g_Page == PageCode)
        {
            c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            if (isalnum(static_cast<unsigned char>(c)) && g_Typed.size() < 12) g_Typed += c;
        }
        else if (c >= 32 && c < 127 && g_Typed.size() < 16) g_Typed += c;
    }

    void ConfirmText()
    {
        if (g_Page == PageCode)
        {
            std::string code = AddJoinCode(g_Typed);
            if (code.empty()) { Open(g_TextFromGame ? PageClosed : PageMain); return; }
            SaveConfig();
            Say(L"CODE " + Wide(code.c_str()) + L" ADDED - ITS LOBBY WILL SHOW IN THE LIST", 3000);
            Open(PageClosed);
            return;
        }
        SetPlayerName(g_Typed);
        SaveConfig();
        std::string name = g_ChosenName;
        Say(name.empty() ? std::wstring(L"NAME: THIS COMPUTER'S") : L"NAME: " + Upper(Wide(name.c_str())), 1800);
        Open(PageMain);
    }

    void TextKey(WPARAM vk)
    {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (vk == VK_RETURN) { ConfirmText(); return; }
        if (vk == VK_ESCAPE) { Open(g_TextFromGame ? PageClosed : PageMain); return; }
        if (vk == VK_BACK) { if (!g_Typed.empty()) g_Typed.pop_back(); return; }
        if (ctrl) { if (vk == 'V') for (char c : ClipboardText()) TypeChar(c); return; }
        if (vk >= 'A' && vk <= 'Z') TypeChar(static_cast<char>(shift != caps ? vk : vk + 32));
        else if (vk >= '0' && vk <= '9') TypeChar(static_cast<char>(vk));
        else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) TypeChar(static_cast<char>('0' + (vk - VK_NUMPAD0)));
        else if (vk == VK_SPACE) TypeChar(' ');
        else if (vk == VK_OEM_MINUS) TypeChar(shift ? '_' : '-');
        else if (vk == VK_OEM_PERIOD) TypeChar('.');
    }

    // back from the page that asks about a slow load: to the row it was about
    void LeaveConfirm() { Open(g_ConfirmAction == ActLoad ? PageSaves : PageMain, g_ConfirmAction, g_ConfirmArg); }

    void Activate(const Item& it)
    {
        if (!it.enabled) return;
        __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "menu: %s", Utf8(it.text.c_str()).c_str());
        const bool confirmed = g_Page == PageConfirm;
        switch (it.action)
        {
        case ActClose: Open(PageClosed); break;
        case ActBack: LeaveConfirm(); break;
        case ActSolo: Request(1, 0); Open(PageClosed); Say(L"SOLO PLAY: SAVES AND REWIND ARE ON (F1)", 3000); break;
        case ActSave:
            g_PendingWhat = 2; g_PendingAuto = false; g_PendingQuick = false;
            // online every device saves on the same frame, and the netcode picks the slot
            if (OnlineGame()) Request(2, -1); else Request(2, NextSaveId());
            Open(PageClosed);
            break;
        case ActSaves: Open(PageSaves); break;
        // the quick slot: one save, replaced by each quick save (solo play only: online, SAVE in the
        // menu saves for everyone and a load asks the others)
        case ActQuickSave:
            Open(PageClosed);
            if ((g_Status & 4) != 0) break;
            if (OnlineGame()) { Say(L"QUICK SAVE IS FOR SOLO PLAY (F1: SAVE FOR EVERY PLAYER)", 2200); break; }
            if (!Solo()) { Say(L"START SOLO PLAY TO SAVE (F1)", 2200); break; }
            if ((g_Status & 2) == 0) { Say(L"CAN'T SAVE RIGHT NOW", 1500); break; }
            g_PendingWhat = 2; g_PendingAuto = false; g_PendingQuick = true;
            Request(2, c_QuickSlot);
            break;
        case ActQuickLoad:
            if ((g_Status & 4) != 0) { Open(PageClosed); break; }
            if (OnlineGame()) { Open(PageClosed); Say(L"QUICK LOAD IS FOR SOLO PLAY (F1: LOAD A SAVE)", 2200); break; }
            g_SaveCache.erase(c_QuickSlot);
            RefreshSaves();   // LOAD's estimate (and the question for a slow load) reads the list
            if (!SaveOf(c_QuickSlot)) { Open(PageClosed); Say(L"NO QUICK SAVE YET (F5 MAKES ONE)", 2000); break; }
            {
                Item load = it;
                load.action = ActLoad; load.arg = c_QuickSlot; load.enabled = true;
                Activate(load);   // as LOAD on the save list (it asks first when the load is slow)
            }
            break;
        case ActLoad:
            if (OnlineGame() && g_Seat < 0) { Say(L"ONLY PLAYERS WITH A SEAT CAN LOAD", 1800); break; }
            if (!confirmed && !OnlineGame())
            {
                int est = 0;
                for (auto& s : g_Saves) if (s.id == it.arg) est = LoadEstimate(s);
                if (est >= 5000) { g_ConfirmAction = ActLoad; g_ConfirmArg = it.arg; g_ConfirmEst = est; Open(PageConfirm); break; }
            }
            g_PendingWhat = 3; g_PendingAuto = false; g_PendingQuick = it.arg == c_QuickSlot;
            g_CoverSlot = it.arg; g_CoverWhat = 3;
            Request(3, it.arg);
            Open(PageClosed);
            break;
        case ActRewind:
            if (!confirmed)
            {
                int est = it.arg == 15 ? pfo_android::g_RewindEst[0].load() : it.arg == 30 ? pfo_android::g_RewindEst[1].load() : pfo_android::g_RewindEst[2].load();
                if (est >= 5000) { g_ConfirmAction = ActRewind; g_ConfirmArg = it.arg; g_ConfirmEst = est; Open(PageConfirm); break; }
            }
            g_PendingWhat = 4; g_PendingAuto = false;
            g_CoverSlot = -1; g_CoverWhat = 4;
            Request(30, it.arg);
            Open(PageClosed);
            break;
        case ActAccept: AnswerVote(true); Open(PageClosed); break;
        case ActDecline: AnswerVote(false); Open(PageClosed); break;
        case ActRejoin: Request(20, 0); g_RejoinOffer.clear(); Open(PageClosed); break;
        case ActForget:
            g_RejoinDeclined = g_RejoinOffer.find('|') != std::string::npos ? g_RejoinOffer.substr(g_RejoinOffer.find('|')) : std::string();
            Request(21, 0);
            g_RejoinOffer.clear();
            Open(PageClosed);
            break;
        case ActAutoSave: g_AutoSavePause = !g_AutoSavePause; SaveConfig(); break;
        case ActCopyCode: CopyInvite(); Open(PageClosed); break;
        case ActCode: Open(PageCode); break;
        case ActName: Open(PageName); break;
        case ActUpnp: fakesteam::upnp::enabled = !fakesteam::upnp::enabled; SaveConfig(); break;
        }
    }

    // a key of the menu's (a controller's buttons come as the keys they stand for)
    void MenuKey(WPARAM vk)
    {
        if (g_Page == PageName || g_Page == PageCode) { TextKey(vk); return; }
        int n = static_cast<int>(g_Items.size());
        if (vk == VK_ESCAPE || vk == 'X' || vk == VK_BACK)
        {
            if (g_Page == PageConfirm) LeaveConfirm();
            else Open(g_Page == PageSaves ? PageMain : PageClosed);
            return;
        }
        if (n == 0) return;
        if (vk == VK_UP || vk == VK_DOWN)
        {
            g_DeleteArmed = -1;
            for (int i = 0; i < n; i++)
            {
                g_Selected = (g_Selected + (vk == VK_UP ? n - 1 : 1)) % n;
                if (g_Items[g_Selected].enabled) break;
            }
        }
        else if (vk == VK_RETURN || vk == 'Z' || vk == VK_SPACE)
        {
            Item it = g_Items[g_Selected];
            g_DeleteArmed = -1;
            Activate(it);
        }
        else if (vk == VK_DELETE && g_Page == PageSaves && g_Items[g_Selected].action == ActLoad)
        {
            int id = g_Items[g_Selected].arg;
            ULONGLONG now = GetTickCount64();
            if (g_DeleteArmed != id || now > g_DeleteArmedUntil) { g_DeleteArmed = id; g_DeleteArmedUntil = now + 2500; }
            else
            {
                for (const wchar_t* ext : { L".pfostate", L".json", L".png" }) DeleteFileW(SaveFile(id, ext).c_str());
                __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "menu: deleted save %d", id);
                g_DeleteArmed = -1;
                g_SavesReadAt = 0;
            }
        }
    }

    // ---- what changed since the last look (every 100 ms) ----
    void MaybeAutoSave(int old, int st, ULONGLONG now)
    {
        if ((old & 4) != 0 && (st & 4) == 0) g_NoAutoUntil = now + 2500;   // a load just finished
        bool pausedNow = (st & 8) != 0, pausedBefore = (old & 8) != 0;
        if (!g_AutoSavePause || !pausedNow || pausedBefore || now < g_NoAutoUntil) return;
        if ((st & 2) == 0 || (st & 4) != 0 || (g_PendingWhat == 2 && g_PendingAuto)) return;
        g_PendingAuto = true;
        g_PendingWhat = 2;
        // online everyone saves (one SAVE command; the first per pause counts)
        Request(2, (st & 1) == 0 ? -2 : c_AutoSlot);
    }

    // the box that asks about another player's load, and the screen shown while a save loads
    Card g_VoteBox, g_Cover;
    int g_CoverPct = 0, g_VotedSlot = -1;
    std::wstring g_CoverProgress;
    ULONGLONG g_CoverSince = 0;
    std::string g_VoteBoxLogged;

    void Poll()
    {
        ULONGLONG now = GetTickCount64();
        int old = g_Status;
        g_Status = pfo_android::StateStatus();
        g_Net = NetStatus();
        g_Seat = OnlineSeat();
        MaybeAutoSave(old, g_Status, now);
        const bool restoring = (g_Status & 4) != 0;

        // how the last save or load went
        int seq = (g_Status >> 24) & 0x7f, result = static_cast<signed char>((g_Status >> 16) & 0xff);
        if (g_LastResultSeq < 0) g_LastResultSeq = seq;
        else if (seq != g_LastResultSeq)
        {
            g_LastResultSeq = seq;
            if (result == 1)
            {
                Say(g_PendingAuto ? L"SAVED BY ITSELF" : g_PendingQuick ? L"QUICK SAVED (F9 LOADS IT)" : L"SAVED", 2000);
                g_PendingAuto = false;
                g_SaveCache.erase(pfo_android::g_LastSavedSlot.load());
                TakePicture(pfo_android::g_LastSavedSlot.load());
                g_SavesReadAt = 0;
            }
            else if (g_PendingAuto && g_PendingWhat == 2) g_PendingAuto = false;   // an auto save that couldn't happen: say nothing
            else if (result == 3) Say(g_PendingWhat == 4 ? L"REWOUND" : g_PendingQuick ? L"QUICK LOADED" : L"LOADED", 2000);
            else if (result == -6) Say(L"NOTHING TO REWIND THAT FAR YET", 1500);
            else if (result == -3) Say(L"LOADED - BUT IT DIDN'T CHECK OUT", 3000, true);
            else if (result == -2) Say(L"THAT SAVE CAN'T BE READ", 1800);
            else if (result == -7) Say(L"THAT SAVE'S MATCH USED PHONE-ONLY SAVES: IT CAN'T BE LOADED FROM HERE", 3500);
            else if (result == -5)
            {
                int n = pfo_android::g_OfferRefused.exchange(0);
                Say(L"THAT SAVE IS FOR " + std::to_wstring(n) + (n == 1 ? L" PLAYER" : L" PLAYERS"), 2200);
            }
            else if (result == -1) Say(g_PendingWhat == 2 ? (OnlineGame() ? L"ONLY PLAYERS WITH A SEAT CAN SAVE" : L"START SOLO PLAY TO SAVE (F1)") : L"NOT DURING AN ONLINE GAME", 2000);
            // a load that is over (or never began) leaves nothing for the next loading screen to show
            if (result != 1 && !restoring) { g_CoverSlot = -1; g_CoverWhat = 0; }
            g_PendingQuick = false;
        }

        // which player we are, when an online game starts and whenever the seats change (a game for
        // one player leaves the others watching). Not while a load replays the match: the seats it
        // passes through on the way are old news; what has changed by its end is told then.
        if (g_Seat != g_LastSeat && !restoring)
        {
            if (g_Seat != -1 && !Solo()) Say(g_Seat >= 0 ? L"YOU ARE P" + std::to_wstring(g_Seat + 1) : std::wstring(L"YOU ARE WATCHING"), 2200);
            g_LastSeat = g_Seat;
        }

        // online: a load is asked for
        if (!OnlineGame()) { memset(g_Vote, 0, sizeof(g_Vote)); g_VotedSlot = -1; }
        else
        {
            { std::lock_guard<std::mutex> lock(pfo_android::g_PubMutex); for (int i = 0; i < 11; i++) g_Vote[i] = pfo_android::g_PubVote[i]; }
            if (g_LastVoteSeq < 0) g_LastVoteSeq = g_Vote[9];
            if (g_Vote[9] != g_LastVoteSeq)
            {
                g_LastVoteSeq = g_Vote[9];
                std::wstring who = g_Vote[8] >= 0 ? L"P" + std::to_wstring(g_Vote[8] + 1) : std::wstring(L"SOMEONE");
                if (g_Vote[7] == 2) Say(who + L" SAID NO - NOT LOADED", 2500, true);
                else if (g_Vote[7] == 3) Say(who + L" CAN'T LOAD THAT SAVE", 2500, true);
                else if (g_Vote[7] == 4) Say(L"LOAD CANCELLED", 2000);
                else if (g_Vote[7] == 1)
                {
                    // everyone accepted: that save loads now, on every device, so the loading screen
                    // shows it (ours was noted when we asked: it stays if the vote's slot isn't known)
                    int s = g_VotedSlot >= 0 ? g_VotedSlot : g_Vote[5];
                    if (s >= 0 || g_Vote[8] != g_Seat) g_CoverSlot = s;
                    g_CoverWhat = 3;
                }
                g_VotedSlot = -1;
            }
            if (g_Vote[0] != 0)
            {
                g_VotedSlot = g_Vote[5];
                // no answer in time: ours is withdrawn, theirs is declined
                bool mine = g_Vote[2] == g_Seat;
                if (mine && g_Vote[10] >= c_ProposalSeconds) AnswerVote(false);
                else if (!mine && g_Vote[6] == 0 && g_Seat >= 0 && g_Vote[10] >= c_VoteSeconds) AnswerVote(false);
            }
        }

        // online: a player's connection dropped, and the match is frozen until they're back or time runs out
        int waitMask = g_Session.m_OnlineState == EOnlineState::Online ? static_cast<int>(g_WaitMask.load()) : 0;
        int waitSeconds = g_WaitSecondsLeft.load(), rejoinSeq = g_RejoinSeq.load();
        if (g_LastRejoinSeq < 0) g_LastRejoinSeq = rejoinSeq;
        if (waitMask != 0) g_WaitedFor |= waitMask;
        else if (rejoinSeq != g_LastRejoinSeq && g_WaitedFor != 0) { Say(Seats(g_WaitedFor) + L" IS BACK", 2000); g_WaitedFor = 0; }
        else if (g_WaitShown != 0)
        {
            // the wait ended without them: the match is over
            if ((g_Net & 64) == 0 && g_WaitedFor != 0) Say(Seats(g_WaitedFor) + L" DIDN'T COME BACK - MATCH ENDED", 3500, true);
            g_WaitedFor = 0;
        }
        g_LastRejoinSeq = rejoinSeq;
        g_WaitShown = waitMask;

        // a match we were in and didn't see end
        if (now - g_LastOfferCheck > 1000)
        {
            g_LastOfferCheck = now;
            std::string offer = (g_Net & 64) == 0 ? pfo_android::RejoinOffer() : std::string();
            // the match we said no to stays dismissed (the seat and the names tell which it is)
            std::string key = offer.find('|') != std::string::npos ? offer.substr(offer.find('|')) : std::string();
            if (!key.empty() && key == g_RejoinDeclined) offer.clear();
            if (offer.empty() != g_RejoinOffer.empty() && !offer.empty()) __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "a match can be rejoined");
            g_RejoinOffer = offer;
        }
        int rejoined = pfo_android::g_RejoinResult.exchange(0);
        if (rejoined == 1) Say(L"BACK IN THE MATCH", 2200);
        else if (rejoined == -1) Say(L"COULDN'T REJOIN THE MATCH", 2500, true);

        // the game asks for the code box (JOIN BY CODE) or the invite code (INVITE FRIENDS)
        if (fakesteam::g_AskCodeRequested) { fakesteam::g_AskCodeRequested = false; Open(PageCode); g_TextFromGame = true; }
        if (fakesteam::g_InviteRequested) { fakesteam::g_InviteRequested = false; CopyInvite(); }

        // another player asks to load a save: a box with the save in it (its picture came with the offer)
        g_VoteBox = Card();
        if (VoteShown())
        {
            g_VoteBox.on = true;
            g_VoteBox.slot = g_Vote[5];
            g_VoteBox.pictureAfter = 1;
            g_VoteBox.lines.push_back(Colored(L"P" + std::to_wstring(g_Vote[2] + 1) + L" WANTS TO LOAD", SeatColor(g_Vote[2])));
            if (const SaveInfo* s = SaveOf(g_Vote[5]))
            {
                g_VoteBox.lines.push_back(Colored(s->title, c_Text));
                g_VoteBox.lines.push_back(Colored(Played(*s, false) + L"  -  SAVED " + When(s->savedAt), c_Dim));
            }
            else g_VoteBox.lines.push_back(Colored(L"A SAVE", c_Text));
            g_VoteBox.lines.push_back(Colored(L"F2: ACCEPT   F3: DECLINE (" + std::to_wstring(std::max(0, c_VoteSeconds - g_Vote[10])) + L")", c_Attention));
            if (g_PadConnected) g_VoteBox.lines.push_back(Colored(L"CONTROLLER: PRESS BOTH STICKS IN, THEN CHOOSE", c_Dim));
        }
        {
            char what[64] = "";
            if (g_VoteBox.on) snprintf(what, sizeof(what), "slot %d, picture %s", g_VoteBox.slot, PictureOf(g_VoteBox.slot) ? "yes" : "no");
            if (g_VoteBoxLogged != what)
            {
                g_VoteBoxLogged = what;
                if (g_VoteBox.on) __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "vote box: shown (%s)", what);
            }
        }

        // a save is loading: the game's picture is covered until it's there
        const bool covered = g_Cover.on;
        g_Cover = Card();
        g_CoverProgress.clear();
        if (restoring)
        {
            const SaveInfo* s = SaveOf(g_CoverSlot);
            if (!covered)
            {
                g_CoverSince = now;
                __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "cover: on (slot %d, %s)", s ? s->id : -1, s && PictureOf(s->id) ? "its picture" : "no picture");
            }
            g_Cover.on = true;
            if (s)
            {
                g_Cover.slot = s->id;
                g_Cover.lines.push_back(Colored(s->title, c_Text));
                g_Cover.lines.push_back(Colored(s->kind + L"  -  " + Played(*s, true) + L"  -  " + When(s->savedAt), c_Dim));
            }
            else g_Cover.lines.push_back(Colored(g_CoverWhat == 4 ? L"REWINDING" : L"LOADING", c_Text));
            g_CoverPct = std::min(100, (g_Status >> 8) & 0xff);
            const long long took = static_cast<long long>(now - g_CoverSince);
            // (the title already says REWINDING or LOADING when there's no save to name)
            g_CoverProgress = (s ? L"LOADING  " : L"") + std::to_wstring(g_CoverPct) + L"%";
            if (g_CoverPct >= 5 && g_CoverPct < 100 && took > 1500) g_CoverProgress += L"  -  " + About(took * (100 - g_CoverPct) / g_CoverPct) + L" LEFT";
        }
        else if (covered)
        {
            __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "cover: off");
            g_CoverSlot = -1;
            g_CoverWhat = 0;
        }

        // the line that stays: the one thing that needs attention
        g_Sticky.clear();
        g_StickyAttention = false;
        if (VoteShown())
        {
            // (the box asks; with the menu open over it, a line does)
            if (g_Page != PageClosed)
            {
                g_Sticky = L"P" + std::to_wstring(g_Vote[2] + 1) + L" WANTS TO LOAD A SAVE (" + std::to_wstring(std::max(0, c_VoteSeconds - g_Vote[10])) + L")";
                g_StickyAttention = true;
            }
        }
        else if (g_Vote[0] != 0 && g_Seat >= 0)
        {
            int left = g_Vote[4] & ~g_Vote[3];
            g_Sticky = left != 0 ? L"LOAD ASKED - WAITING FOR " + Seats(left) : std::wstring(L"LOADING...");
        }
        else if (restoring) { }   // (the loading screen says how far it is)
        else if (waitMask != 0)
        {
            wchar_t left[32] = {};
            if (waitSeconds >= 0) swprintf(left, countof(left), L" - %d:%02d", waitSeconds / 60, waitSeconds % 60);
            g_Sticky = L"WAITING FOR " + Seats(waitMask) + left;
            g_StickyAttention = true;
        }
        else if (!g_RejoinOffer.empty() && g_Page == PageClosed)
        {
            g_Sticky = L"REJOIN YOUR MATCH? " + RejoinText() + L"   F2: REJOIN   F3: NO";
            g_StickyAttention = true;
        }
        if (now >= g_BannerUntil) g_Banner.clear();
        if (g_SaveCache.size() > 16) g_SaveCache.clear();
        if (g_Pictures.size() > 12) g_Pictures.clear();
        if (g_Page != PageClosed) Build();
    }

    // ---- drawing ----
    void Fill(HDC dc, const RECT& r, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        FillRect(dc, &r, b);
        DeleteObject(b);
    }

    void Box(HDC dc, const RECT& r, COLORREF edge)
    {
        Fill(dc, r, edge);
        RECT in = { r.left + 2, r.top + 2, r.right - 2, r.bottom - 2 };
        Fill(dc, in, c_Back);
    }

    int Width(HDC dc, const std::wstring& s)
    {
        SIZE size = {};
        GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &size);
        return size.cx;
    }

    void Text(HDC dc, const std::wstring& s, RECT r, COLORREF color, UINT how)
    {
        SetTextColor(dc, color);
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, how | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    // the window's measures: its size, the font's height, a line's, the gap at the edges, the widest a box gets
    struct Look { int w, h, fh, lineH, pad, limit; };

    // texts of several colours next to each other, in as many lines as it takes (no more than maxLines)
    typedef std::vector<std::vector<Run>> Flowed;
    Flowed Flow(HDC dc, const std::vector<Run>& runs, int width, int maxLines)
    {
        Flowed lines;
        const int gap = Width(dc, L"  ");
        int used = 0;
        for (auto& r : runs)
        {
            int rw = Width(dc, r.text);
            if (lines.empty() || (used > 0 && used + gap + rw > width))
            {
                if (static_cast<int>(lines.size()) >= maxLines) break;
                lines.emplace_back();
                used = 0;
            }
            used += (used > 0 ? gap : 0) + rw;
            lines.back().push_back(r);
        }
        return lines;
    }

    int FlowWidth(HDC dc, const std::vector<Run>& line)
    {
        int total = Width(dc, L"  ") * std::max(0, static_cast<int>(line.size()) - 1);
        for (auto& run : line) total += Width(dc, run.text);
        return total;
    }

    void DrawFlowLine(HDC dc, const std::vector<Run>& line, const RECT& r)
    {
        const int gap = Width(dc, L"  ");
        int x = r.left + std::max(0, (static_cast<int>(r.right - r.left) - FlowWidth(dc, line)) / 2);
        for (auto& run : line)
        {
            if (x >= r.right) break;
            RECT one = { x, r.top, r.right, r.bottom };
            Text(dc, run.text, one, run.color, DT_LEFT);
            x += Width(dc, run.text) + gap;
        }
    }

    // noPictureBox: a save without a picture gets an empty frame where it would be (the box keeps its shape)
    int CardHeight(HDC dc, const Card& c, const Look& k, int width, int picH, bool noPictureBox)
    {
        int height = static_cast<int>(c.lines.size()) * k.lineH;
        if (picH > 0 && (noPictureBox || PictureOf(c.slot))) height += picH + k.lineH / 3;
        if (c.playersAfter >= 0 && !c.players.empty()) height += static_cast<int>(Flow(dc, c.players, width, 2).size()) * k.lineH;
        return height;
    }

    // drawn down from the area's top, each line in the middle of its width; returns where it ends
    int DrawCard(HDC dc, const Card& c, const Look& k, const RECT& area, int picH, bool dim, bool noPictureBox)
    {
        const int width = area.right - area.left, n = static_cast<int>(c.lines.size());
        const Picture* pic = picH > 0 ? PictureOf(c.slot) : nullptr;
        Flowed names;
        if (c.playersAfter >= 0) names = Flow(dc, c.players, width, 2);
        int y = area.top;
        for (int i = 0; i <= n; i++)
        {
            if (i == c.pictureAfter && picH > 0 && (pic || noPictureBox))
            {
                int pw = std::min(width, pic ? picH * pic->w / std::max(1, pic->h) : picH * 16 / 9);
                RECT r = { area.left + (width - pw) / 2, y, area.left + (width - pw) / 2 + pw, y + picH };
                if (pic) DrawPicture(dc, *pic, r, dim);
                else
                {
                    Fill(dc, r, c_Off);
                    RECT in = { r.left + 1, r.top + 1, r.right - 1, r.bottom - 1 };
                    Fill(dc, in, c_Back);
                    Text(dc, L"NO PICTURE", r, c_Off, DT_CENTER);
                }
                y += picH + k.lineH / 3;
            }
            if (i == c.playersAfter)
                for (auto& line : names)
                {
                    RECT r = { area.left, y, area.right, y + k.lineH };
                    DrawFlowLine(dc, line, r);
                    y += k.lineH;
                }
            if (i < n)
            {
                RECT r = { area.left, y, area.right, y + k.lineH };
                Text(dc, c.lines[i].text, r, c.lines[i].color, DT_CENTER);
                y += k.lineH;
            }
        }
        return y;
    }

    // a save is loading: its picture (darkened), what it is, how far the load is
    void PaintCover(HDC dc, const Look& k)
    {
        const int picH = PictureOf(g_Cover.slot) ? k.h * 30 / 100 : 0;
        const int barW = std::min(k.limit, std::max(picH * 16 / 9, k.w * 45 / 100)), barH = std::max(8, k.lineH / 2);
        const int total = CardHeight(dc, g_Cover, k, k.limit, picH, false) + k.lineH / 2 + barH + k.lineH / 3 + k.lineH;
        RECT area = { k.pad, std::max(k.pad, (k.h - total) * 45 / 100), k.w - k.pad, k.h };
        int y = DrawCard(dc, g_Cover, k, area, picH, true, false) + k.lineH / 2;
        RECT bar = { (k.w - barW) / 2, y, (k.w - barW) / 2 + barW, y + barH };
        Box(dc, bar, c_Text);
        RECT done = { bar.left + 2, bar.top + 2, bar.left + 2 + (barW - 4) * g_CoverPct / 100, bar.bottom - 2 };
        if (done.right > done.left) Fill(dc, done, c_Progress);
        RECT r = { k.pad, bar.bottom + k.lineH / 3, k.w - k.pad, bar.bottom + k.lineH / 3 + k.lineH };
        Text(dc, g_CoverProgress, r, c_Dim, DT_CENTER);
    }

    // the saves: the list on the left, the one the choice is on in full on the right
    void PaintSaves(HDC dc, const Look& k)
    {
        const int n = static_cast<int>(g_Items.size());
        const int bw = std::min(k.limit, std::max(k.w * 88 / 100, 40 * k.fh)), inner = bw - 2 * k.pad;
        const int rightW = (inner - k.pad) * 55 / 100, leftW = inner - k.pad - rightW;
        const int picH = std::max(k.lineH, std::min(rightW * 9 / 16, k.h * 30 / 100));
        const int contentH = picH + k.lineH / 3 + 7 * k.lineH;   // picture, and seven lines at the most
        const int rows = std::max(3, contentH / k.lineH);
        const int bh = k.pad + k.lineH + k.lineH / 2 + contentH + k.lineH / 2 + k.lineH + k.pad;
        RECT box = { (k.w - bw) / 2, (k.h - bh) / 2, (k.w - bw) / 2 + bw, (k.h - bh) / 2 + bh };
        Box(dc, box, c_Text);
        RECT r = { box.left + k.pad, box.top + k.pad, box.right - k.pad, box.top + k.pad + k.lineH };
        Text(dc, g_Title, r, c_Text, DT_CENTER);
        if (n > rows) Text(dc, std::to_wstring(g_Selected + 1) + L" OF " + std::to_wstring(n), r, c_Dim, DT_RIGHT);
        const int top = r.bottom + k.lineH / 2;
        const int first = std::max(0, std::min(g_Selected - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++)
        {
            const int y = top + (i - first) * k.lineH;
            RECT row = { box.left + k.pad / 2, y, box.left + k.pad + leftW, y + k.lineH };
            RECT text = { box.left + k.pad, y, row.right - k.pad / 2, y + k.lineH };
            bool chosen = i == g_Selected && g_Items[i].enabled;
            if (chosen) Fill(dc, row, c_Text);
            Text(dc, g_Items[i].text, text, chosen ? c_Back : g_Items[i].enabled ? c_Text : c_Off, DT_LEFT);
        }
        RECT area = { box.right - k.pad - rightW, top, box.right - k.pad, top + contentH };
        DrawCard(dc, g_Details, k, area, picH, false, true);
        RECT hint = { box.left + k.pad, top + contentH + k.lineH / 2, box.right - k.pad, top + contentH + k.lineH / 2 + k.lineH };
        Text(dc, g_Hint, hint, c_Dim, DT_CENTER);
    }

    // every other page: a title, what there is to say under it, rows to choose from, what the keys do
    void PaintMenu(HDC dc, const Look& k)
    {
        const int n = static_cast<int>(g_Items.size());
        Flowed names;
        if (!g_Players.empty()) names = Flow(dc, g_Players, k.limit - 2 * k.pad, 2);
        const int above = (g_Sub.empty() ? 0 : 1) + static_cast<int>(names.size());
        const int rows = std::max(3, std::min(n, (k.h * 7 / 10) / k.lineH - 3 - above));
        const int first = std::max(0, std::min(g_Selected - rows / 2, n - rows));
        int widest = std::max(std::max(Width(dc, g_Title), Width(dc, g_Sub)), Width(dc, g_Hint));
        for (auto& line : names) widest = std::max(widest, FlowWidth(dc, line));
        for (auto& it : g_Items) widest = std::max(widest, Width(dc, it.text) + k.pad);
        const int bw = std::min(k.limit, widest + 2 * k.pad);
        const int bh = k.pad + k.lineH + above * k.lineH + k.lineH / 2 + std::min(n, rows) * k.lineH + k.lineH / 2 + k.lineH + k.pad;
        RECT box = { (k.w - bw) / 2, (k.h - bh) / 2, (k.w - bw) / 2 + bw, (k.h - bh) / 2 + bh };
        Box(dc, box, c_Text);
        RECT r = { box.left + k.pad, box.top + k.pad, box.right - k.pad, box.top + k.pad + k.lineH };
        Text(dc, g_Title, r, c_Text, DT_CENTER);
        if (!g_Sub.empty()) { OffsetRect(&r, 0, k.lineH); Text(dc, g_Sub, r, c_Dim, DT_CENTER); }
        for (auto& line : names) { OffsetRect(&r, 0, k.lineH); DrawFlowLine(dc, line, r); }
        OffsetRect(&r, 0, k.lineH / 2);
        for (int i = first; i < n && i < first + rows; i++)
        {
            OffsetRect(&r, 0, k.lineH);
            bool chosen = i == g_Selected && g_Items[i].enabled;
            RECT row = { box.left + k.pad / 2, r.top, box.right - k.pad / 2, r.bottom };
            if (chosen) Fill(dc, row, c_Text);
            Text(dc, g_Items[i].text, r, chosen ? c_Back : g_Items[i].enabled ? c_Text : c_Off, DT_LEFT);
        }
        OffsetRect(&r, 0, k.lineH + k.lineH / 2);
        Text(dc, g_Hint, r, c_Dim, DT_CENTER);
    }

    // another player asks to load a save
    void PaintVote(HDC dc, const Look& k)
    {
        const Picture* pic = PictureOf(g_VoteBox.slot);
        const int picH = pic ? k.h * 26 / 100 : 0;
        int widest = picH * 16 / 9;
        for (auto& line : g_VoteBox.lines) widest = std::max(widest, Width(dc, line.text));
        const int bw = std::min(k.limit, widest + 2 * k.pad);
        const int bh = CardHeight(dc, g_VoteBox, k, bw - 2 * k.pad, picH, false) + 2 * k.pad;
        RECT box = { (k.w - bw) / 2, (k.h - bh) / 2, (k.w - bw) / 2 + bw, (k.h - bh) / 2 + bh };
        Box(dc, box, c_Attention);
        RECT area = { box.left + k.pad, box.top + k.pad, box.right - k.pad, box.bottom - k.pad };
        DrawCard(dc, g_VoteBox, k, area, picH, false, false);
    }

    void Paint(HDC dc, int w, int h)
    {
        RECT all = { 0, 0, w, h };
        Fill(dc, all, g_Cover.on ? c_Back : c_Key);
        int fh = std::max(13, h / 30);
        if (!g_Font || g_FontHeight != fh)
        {
            if (g_Font) DeleteObject(g_Font);
            g_Font = CreateFontW(-fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            g_FontHeight = fh;
        }
        HGDIOBJ oldFont = SelectObject(dc, g_Font);
        SetBkMode(dc, TRANSPARENT);
        const Look k = { w, h, fh, fh * 3 / 2, fh, w - 2 * fh };
        if (g_Cover.on) PaintCover(dc, k);

        // the messages, from the bottom up
        int bottom = h - h / 12;
        auto message = [&](const std::wstring& s, bool attention)
        {
            int bw = std::min(k.limit, Width(dc, s) + 2 * k.pad);
            RECT r = { (w - bw) / 2, bottom - k.lineH - k.pad / 2, (w - bw) / 2 + bw, bottom };
            Box(dc, r, attention ? c_Attention : c_Text);
            RECT in = { r.left + k.pad / 2, r.top, r.right - k.pad / 2, r.bottom };
            Text(dc, s, in, attention ? c_Attention : c_Text, DT_CENTER);
            bottom = r.top - k.pad / 3;
        };
        if (!g_Banner.empty()) message(g_Banner, g_BannerAttention);
        if (!g_Sticky.empty()) message(g_Sticky, g_StickyAttention);

        if (g_Page == PageSaves && g_Details.on) PaintSaves(dc, k);
        else if (g_Page != PageClosed) PaintMenu(dc, k);
        else if (g_VoteBox.on) PaintVote(dc, k);
        SelectObject(dc, oldFont);
    }

    void Flat(std::wstring& s, const Card& c)
    {
        if (!c.on) { s += L"-\n"; return; }
        const Picture* p = PictureOf(c.slot);
        s += std::to_wstring(c.slot) + L"/" + std::to_wstring(p ? p->stamp : 0) + L"\n";
        for (auto& r : c.lines) s += r.text + L"\t" + std::to_wstring(r.color) + L"\n";
        for (auto& r : c.players) s += r.text + L"\t" + std::to_wstring(r.color) + L"\n";
    }

    // everything the window shows, as one string: it is drawn again only when this changes
    std::wstring Shown()
    {
        std::wstring s = g_Banner + L"\n" + (g_BannerAttention ? L"!" : L"") + g_Sticky + L"\n" + (g_StickyAttention ? L"!" : L"");
        Flat(s, g_Cover);
        if (g_Cover.on) s += std::to_wstring(g_CoverPct) + L"\n" + g_CoverProgress + L"\n";
        if (g_Page != PageClosed)
        {
            s += g_Title + L"\n" + g_Hint + L"\n" + g_Sub + L"\n" + std::to_wstring(g_Selected) + L"\n";
            for (auto& it : g_Items) s += it.text + (it.enabled ? L"\n" : L"\t\n");
            for (auto& r : g_Players) s += r.text + L"\t" + std::to_wstring(r.color) + L"\n";
            Flat(s, g_Details);
        }
        else Flat(s, g_VoteBox);
        return s;
    }

    void Show()
    {
        if (!g_Panel || !g_Game) return;
        bool want = (g_Page != PageClosed || !g_Banner.empty() || !g_Sticky.empty() || g_Cover.on || g_VoteBox.on) && IsWindowVisible(g_Game) && !IsIconic(g_Game);
        if (!want)
        {
            if (IsWindowVisible(g_Panel)) ShowWindow(g_Panel, SW_HIDE);
            g_Drawn.clear();
            return;
        }
        RECT c = {};
        POINT at = { 0, 0 };
        GetClientRect(g_Game, &c);
        ClientToScreen(g_Game, &at);
        RECT place = { at.x, at.y, at.x + c.right, at.y + c.bottom };
        bool moved = !EqualRect(&place, &g_Place);
        if (moved)
        {
            g_Place = place;
            SetWindowPos(g_Panel, nullptr, place.left, place.top, c.right, c.bottom, SWP_NOACTIVATE | SWP_NOZORDER);
        }
        // (a little of the game shows through the boxes; nothing of it through the loading screen)
        int alpha = g_Cover.on ? 255 : 238;
        if (alpha != g_Alpha)
        {
            g_Alpha = alpha;
            SetLayeredWindowAttributes(g_Panel, c_Key, static_cast<BYTE>(alpha), LWA_COLORKEY | LWA_ALPHA);
        }
        std::wstring shown = Shown();
        if (moved || shown != g_Drawn) { g_Drawn = shown; InvalidateRect(g_Panel, nullptr, FALSE); }
        if (!IsWindowVisible(g_Panel)) ShowWindow(g_Panel, SW_SHOWNOACTIVATE);
    }

    LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        switch (m)
        {
        case WM_TIMER:
            Poll();
            Show();
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT c = {};
            GetClientRect(h, &c);
            if (c.right > 0 && c.bottom > 0)
            {
                // drawn off screen first, so nothing flickers
                HDC mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
                HGDIOBJ old = SelectObject(mem, bmp);
                Paint(mem, c.right, c.bottom);
                BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_DESTROY:
            KillTimer(h, 1);
            g_Panel = nullptr;
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    void MakePanel()
    {
        if (g_Panel || !g_Game) return;
        HINSTANCE module = GetModuleHandleW(nullptr);
        WNDCLASSW wc = {};
        wc.lpfnWndProc = PanelProc;
        wc.hInstance = module;
        wc.lpszClassName = L"UFO50OnlineMenu";
        RegisterClassW(&wc);
        g_Panel = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"UFO 50 online menu", WS_POPUP,
                                  0, 0, 0, 0, g_Game, nullptr, module, nullptr);
        if (!g_Panel) { __android_log_print(ANDROID_LOG_WARN, "PFOMENU", "no menu window (error %lu)", GetLastError()); return; }
        g_Alpha = 238;
        SetLayeredWindowAttributes(g_Panel, c_Key, 238, LWA_COLORKEY | LWA_ALPHA);
        SetTimer(g_Panel, 1, 100, nullptr);
        __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "menu ready (F1)");
        Say(c_StartText, 5000);
    }

    // ---- a controller ----
    // The menu is worked with a controller as with the keys: both sticks pressed in opens and closes
    // it (the game uses neither press), the D-pad or the left stick moves, A chooses, B goes back,
    // X deletes a save.
    //
    // The game reads controllers through XInput, which it loads itself and calls through pointers
    // it keeps. Its pointer to XInputGetState is made to point at Hooked below, which asks the real
    // one and, while the menu is open, hands the game a controller at rest: the pad's equivalent of
    // the key presses that don't reach the game then. (Online the game's inputs are the netcode's,
    // made from this reading on each device: a controller at rest is all the others see of the menu.)
    // After the menu closes, what is still held stays hidden until it is let go, so the press that
    // closed the menu isn't the game's too.
    //
    // Only for the game version this was made for (the pointer's place is the runner's own, proven
    // by the code that fills it); with any other the menu is the keyboard's alone. Controllers the
    // game reads another way (DirectInput ones) don't work the menu either.
    namespace pad
    {
        struct State { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };   // XINPUT_STATE
        constexpr WORD Up = 0x0001, Down = 0x0002, L3 = 0x0040, R3 = 0x0080, A = 0x1000, B = 0x2000, X = 0x4000;
        typedef DWORD (WINAPI* GetStateFn)(DWORD, State*);

        // ufo50.exe: where it loads XInput (lea rdx, "XInputGetState"; mov rcx, [the module];
        // call GetProcAddress; mov [the pointer], rax), and the two places that code names
        constexpr uint32 c_ProofRva = 0xfffb8, c_PointerRva = 0xa22660, c_ModuleRva = 0xa22668;
        const uint8 c_Proof[27] = { 0x48, 0x8d, 0x15, 0xe1, 0x8e, 0x4d, 0x00, 0x48, 0x8b, 0x0d, 0xa2, 0x26, 0x92, 0x00,
                                    0xff, 0x15, 0xa4, 0x72, 0x40, 0x00, 0x48, 0x89, 0x05, 0x8d, 0x26, 0x92, 0x00 };

        GetStateFn g_Real = nullptr;
        void** g_Pointer = nullptr;
        int g_Hooked = 0;                // 1 done, -1 can't be
        std::atomic<bool> g_Called{false};

        // per controller; touched only inside Hooked (whichever thread the game reads controllers on)
        struct Filter { bool blocked = false, hideStick = false; WORD hide = 0; DWORD changes = 0; };
        Filter g_Filter[4];

        DWORD WINAPI Hooked(DWORD index, State* st)
        {
            GetStateFn real = g_Real;
            DWORD r = real ? real(index, st) : ERROR_DEVICE_NOT_CONNECTED;
            g_Called = true;
            if (r != ERROR_SUCCESS || index >= 4 || !st) return r;
            Filter& f = g_Filter[index];
            const Filter before = f;
            if (g_PadBlocked.load()) f.blocked = true;
            else
            {
                // the menu just closed: what is held now isn't the game's until it's let go
                if (f.blocked) { f.blocked = false; f.hide = st->buttons; f.hideStick = true; }
                f.hide &= st->buttons;
                if (f.hideStick && st->lx > -8000 && st->lx < 8000 && st->ly > -8000 && st->ly < 8000) f.hideStick = false;
            }
            // (the game may look at a reading only when its number changed: it changes when what we do to it does)
            if (f.blocked != before.blocked || f.hide != before.hide || f.hideStick != before.hideStick) f.changes++;
            if (f.blocked)
            {
                st->buttons = 0;
                st->lt = st->rt = 0;
                st->lx = st->ly = st->rx = st->ry = 0;
            }
            else
            {
                st->buttons = static_cast<WORD>(st->buttons & ~f.hide);
                if (f.hideStick) st->lx = st->ly = 0;
            }
            st->packet += f.changes;
            return r;
        }

        void Hook()
        {
            if (g_Hooked != 0) return;
            uint8* base = reinterpret_cast<uint8*>(GetModuleHandleW(nullptr));
            const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const IMAGE_NT_HEADERS64* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != pfo_android::winrunner::c_TimeStamp
                || nt->OptionalHeader.SizeOfImage != pfo_android::winrunner::c_ImageSize || memcmp(base + c_ProofRva, c_Proof, sizeof(c_Proof)) != 0)
            {
                g_Hooked = -1;
                __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "controller: not the game version this was made for: the menu is the keyboard's alone");
                return;
            }
            void** pointer = reinterpret_cast<void**>(base + c_PointerRva);
            void* fn = *pointer;
            if (!fn) return;   // the game hasn't loaded XInput yet: looked at again later
            HMODULE in = nullptr, xinput = *reinterpret_cast<HMODULE*>(base + c_ModuleRva);
            if (!xinput || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(fn), &in) || in != xinput)
            {
                g_Hooked = -1;
                __android_log_print(ANDROID_LOG_WARN, "PFOMENU", "controller: the game's XInput isn't where it is expected: the menu is the keyboard's alone");
                return;
            }
            g_Real = reinterpret_cast<GetStateFn>(fn);
            g_Pointer = pointer;
            *pointer = reinterpret_cast<void*>(&Hooked);
            g_Hooked = 1;
            __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "controller: ready (both sticks pressed in: menu)");
        }

        void Unhook()
        {
            if (g_Pointer && *g_Pointer == reinterpret_cast<void*>(&Hooked)) *g_Pointer = reinterpret_cast<void*>(g_Real);
            g_Pointer = nullptr;
        }

        int g_TestButtons = -1;          // the tests' controller (message UFO50OnlineMenuPad): its buttons; -1: not plugged in
        bool g_Present[4] = {}, g_Announced = false, g_CalledLogged = false;
        ULONGLONG g_LastLook = 0, g_LastHookTry = 0, g_NextTry[4] = {}, g_RepeatAt = 0;
        WORD g_Before = 0, g_Repeat = 0;

        void Input(WORD buttons, ULONGLONG now)
        {
            const WORD pressed = static_cast<WORD>(buttons & ~g_Before), both = L3 | R3;
            g_Before = buttons;
            if (!(buttons & g_Repeat)) g_Repeat = 0;
            bool used = false;
            if ((buttons & both) == both && (pressed & both) != 0)
            {
                g_PadHints = true;
                __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "controller: both sticks pressed in");
                Open(g_Page == PageClosed ? PageMain : PageClosed);
                if (g_Page != PageClosed) Poll();
                used = true;
            }
            else if (g_Page != PageClosed)
            {
                WPARAM key = 0;
                if (pressed & A) key = VK_RETURN;
                else if (pressed & B) key = VK_ESCAPE;
                else if (pressed & X) key = VK_DELETE;
                else if (pressed & (Up | Down))
                {
                    g_Repeat = (pressed & Up) ? Up : Down;
                    g_RepeatAt = now + 400;
                    key = g_Repeat == Up ? VK_UP : VK_DOWN;
                }
                else if (g_Repeat != 0 && now >= g_RepeatAt)
                {
                    g_RepeatAt = now + 120;
                    key = g_Repeat == Up ? VK_UP : VK_DOWN;
                }
                if (key != 0)
                {
                    g_PadHints = true;
                    MenuKey(key);
                    if (g_Page != PageClosed) Build();
                    used = true;
                }
            }
            if (used) Show();
        }

        // every step of the game
        void Tick(ULONGLONG now)
        {
            if (now - g_LastLook < 8) return;
            g_LastLook = now;
            if (g_Hooked == 0 && now - g_LastHookTry >= 500) { g_LastHookTry = now; Hook(); }
            if (g_Called.load() && !g_CalledLogged)
            {
                g_CalledLogged = true;
                __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "controller: the game reads its controllers through the menu");
            }
            WORD buttons = 0;
            // (a controller is the game's only while its window is the one in front)
            if (g_Real && GetForegroundWindow() == g_Game)
                for (DWORD i = 0; i < 4; i++)
                {
                    if (!g_Present[i] && now < g_NextTry[i]) continue;   // (asking after one that isn't there is slow)
                    State st = {};
                    g_Present[i] = g_Real(i, &st) == ERROR_SUCCESS;
                    if (!g_Present[i]) { g_NextTry[i] = now + 1000 + i * 150; continue; }
                    buttons |= st.buttons;
                    if (st.ly > 16000) buttons |= Up;
                    if (st.ly < -16000) buttons |= Down;
                }
            bool connected = g_TestButtons >= 0;
            for (bool there : g_Present) connected = connected || there;
            if (g_TestButtons >= 0) buttons |= static_cast<WORD>(g_TestButtons);
            if (connected != g_PadConnected)
            {
                g_PadConnected = connected;
                __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "controller: %s", connected ? "one is connected" : "none connected");
                if (!connected && g_PadHints) { g_PadHints = false; if (g_Page != PageClosed) { Build(); Show(); } }
            }
            if (connected && !g_Announced && g_Panel)
            {
                g_Announced = true;
                if (g_Banner.empty() || g_Banner == c_StartText) { Say(L"F1 OR BOTH STICKS PRESSED IN: ONLINE MENU", 5000); Show(); }
            }
            Input(buttons, now);
        }
    }

    // ---- the game's keys ----
    bool g_Held[256] = {};           // keys whose press went to us: the rest of that press does too
    bool g_CharOurs = false;         // so does the character a press makes

    // true: ours, the game doesn't get it
    bool OnKey(UINT m, WPARAM vk, LPARAM l)
    {
        if (m == WM_CHAR) return g_CharOurs || g_Page != PageClosed;
        if (vk >= 256) return false;
        if (m == WM_KEYUP)
        {
            bool ours = g_Held[vk];
            g_Held[vk] = false;
            return ours;
        }
        bool repeat = (l & (1 << 30)) != 0;
        bool ours = false;
        if (vk == VK_F1)
        {
            ours = true;
            g_PadHints = false;
            if (!repeat) { Open(g_Page == PageClosed ? PageMain : PageClosed); if (g_Page != PageClosed) { Poll(); } }
        }
        else if (g_Page != PageClosed)
        {
            ours = true;
            g_PadHints = false;
            MenuKey(vk);
            if (g_Page != PageClosed) Build();
        }
        else if ((vk == VK_F2 || vk == VK_F3) && (VoteShown() || !g_RejoinOffer.empty()))
        {
            ours = true;
            if (repeat) { }
            else if (VoteShown()) { __android_log_print(ANDROID_LOG_INFO, "PFOMENU", "vote: %s", vk == VK_F2 ? "accept" : "decline"); AnswerVote(vk == VK_F2); }
            else
            {
                Item it;
                it.text = vk == VK_F2 ? L"REJOIN" : L"FORGET THAT MATCH";
                it.action = vk == VK_F2 ? ActRejoin : ActForget;
                Activate(it);
            }
        }
        else if (vk == VK_F5 || vk == VK_F9)
        {
            // quick save and quick load (not repeated while the key is held)
            ours = true;
            if (!repeat)
            {
                Item q;
                q.text = vk == VK_F5 ? L"QUICK SAVE (F5)" : L"QUICK LOAD (F9)";
                q.action = vk == VK_F5 ? ActQuickSave : ActQuickLoad;
                Activate(q);
            }
        }
        else if (g_Held[vk] && repeat) ours = true;   // still held from the menu
        g_Held[vk] = ours;
        g_CharOurs = ours;
        if (ours) Show();
        return ours;
    }

    // The game is closing (its window goes whichever way out was taken): leave the lobby properly,
    // and take our port forward off the router instead of leaving it there until its lease runs
    // out. (On Android the main menu's Quit does this: pfo_android_quit.)
    void OnGameClosing()
    {
        static bool done = false;
        if (done) return;
        done = true;
        pad::Unhook();
        int ext = 0;
        {
            std::lock_guard<std::recursive_mutex> netLock(fakesteam::g_NetMutex);
            fakesteam::LeaveLobby();
            std::lock_guard<std::mutex> l(fakesteam::upnp::m);
            if (fakesteam::upnp::mapped) { ext = fakesteam::upnp::extPort; fakesteam::upnp::mapped = false; fakesteam::upnp::publicCand = {}; }
        }
        if (!ext) return;
        // the router gets two seconds to answer; the game doesn't hang on a slow one
        HANDLE told = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::thread([ext, told] { fakesteam::upnp::delete_mapping(ext); if (told) SetEvent(told); }).detach();
        bool ok = told && WaitForSingleObject(told, 2000) == WAIT_OBJECT_0;
        __android_log_print(ANDROID_LOG_INFO, "yoyo", "PFO LAN: UPnP: port forward %s", ok ? "taken off the router" : "left to run out (the router didn't answer in time)");
    }

    LRESULT CALLBACK GameProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        WNDPROC next = g_GameProc;
        if (m == WM_CLOSE || m == WM_DESTROY) OnGameClosing();
        if (m == g_StartMessage && g_StartMessage != 0) { MakePanel(); return 0; }
        // the tests' controller: wParam its buttons (XInput's bits), lParam 1 unplugs it
        if (m == g_PadMessage && g_PadMessage != 0) { pad::g_TestButtons = l == 1 ? -1 : static_cast<int>(w & 0xffff); return 0; }
        if ((m == WM_KEYDOWN || m == WM_KEYUP || m == WM_CHAR) && OnKey(m, w, l)) return 0;
        if (m == WM_KILLFOCUS) memset(g_Held, 0, sizeof(g_Held));   // their releases go elsewhere
        if (m == WM_NCDESTROY)
        {
            SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(next));
            g_Game = nullptr;
        }
        return CallWindowProcW(next, h, m, w, l);
    }

    BOOL CALLBACK FindGame(HWND h, LPARAM found)
    {
        DWORD pid = 0;
        wchar_t cls[64] = {};
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId() || !GetClassNameW(h, cls, countof(cls)) || wcscmp(cls, L"YYGameMakerYY") != 0) return TRUE;
        *reinterpret_cast<HWND*>(found) = h;
        return FALSE;
    }

    // every step of the game (pfo_update): wrap the game's window once it's there; then the controller
    void Tick()
    {
        static ULONGLONG lastTry = 0;
        static bool done = false;
        ULONGLONG now = GetTickCount64();
        if (done)
        {
            if (g_Game) pad::Tick(now);
            return;
        }
        if (now - lastTry < 500) return;
        lastTry = now;
        HWND game = nullptr;
        EnumWindows(FindGame, reinterpret_cast<LPARAM>(&game));
        if (!game) return;
        done = true;
        g_Game = game;
        g_StartMessage = RegisterWindowMessageW(L"UFO50OnlineMenuStart");
        g_PadMessage = RegisterWindowMessageW(L"UFO50OnlineMenuPad");
        g_GameProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(game, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GameProc)));
        if (!g_GameProc) { __android_log_print(ANDROID_LOG_WARN, "PFOMENU", "the game's window can't be wrapped (error %lu): no menu", GetLastError()); g_Game = nullptr; return; }
        // our window is made on the thread that owns the game's
        PostMessageW(game, g_StartMessage, 0, 0);
    }

    // the extension is loaded
    void Start()
    {
        LoadConfig();
    }
}
