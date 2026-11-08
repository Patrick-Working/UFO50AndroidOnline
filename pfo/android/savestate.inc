// ---- save states: recorded play, restored by fast replay ----
// Included by runner_bridge.inc (same translation unit as dllmain.cpp: g_Session is PFO's session).
//
// A save state is PFO's own replay of the session up to the frame you saved at: the session's
// starting point (seed, shared save files) and every input change since. PFO sessions are
// deterministic, so replaying it lands on exactly the same game state. Loading replays it with
// drawing off and the game speed raised (thousands of steps a second), then hands control back.
// The checksum PFO keeps for every frame is saved too, and compared once the replay arrives.
//
// Single player plays as a PFO session of one (StartSolo), which records like an online one.
//
// Files: Android/data/<pkg>/files/savestates/slot<N>.pfostate.
// Testing: /sdcard/Android/data/<pkg>/files/ss_cmd holding "solo", "save <n>" or "load <n>".
#include <atomic>
#include <ctime>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <sys/stat.h>
#include <dirent.h>

namespace pfo_android
{
    constexpr uint32 c_StateMagic = 0x53534650u;     // PFSS
    constexpr uint32 c_StateVersion = 1;
    constexpr double c_RestoreSpeed = 100.0;         // speed multiplier while replaying (runner caps it)

    struct StateHeader
    {
        uint32 magic = c_StateMagic;
        uint32 version = c_StateVersion;
        uint32 replayVersion = c_ReplayVersion;
        uint32 frame = 0;              // the frame the state was saved at
        uint32 lastFrameWritten = 0;   // the replay writer's last chunk frame (to keep writing after a load)
        uint32 checksum = 0;           // PFO's checksum of that frame
        uint32 replaySize = 0;         // bytes of replay that follow the header
        int64 savedAt = 0;             // unix time
    };

    struct Restore
    {
        bool active = false;
        std::vector<uint8> data;       // the replay being played (PlayReplay reads it in place)
        StateHeader header;
        int64 startMs = 0;
        int slot = 0;
    };
    Restore g_Restore;

    // Frames (in this session's timeline) where a capture had to switch every instance on to read
    // it. That changes the game a little, so replays do the same at the same frames ("touch").
    // Saved after each state's replay; a load carries on the list it came from.
    std::vector<uint32> g_TouchFrames;
    std::vector<uint32> g_ReplayTouch;     // the list a replay is following
    size_t g_ReplayTouchNext = 0;
    constexpr uint32 c_TouchMagic = 0x31484354u;   // TCH1
    bool g_LastAdvanceArg = true;       // what the game passed to pfo_update (false: it's holding frames)

    // requests from the overlay (UI thread), run on the game thread at the next pfo_update
    std::atomic<int> g_StateRequest{0};   // 0 none, 1 solo, 2 save, 3 load
    std::atomic<int> g_StateRequestSlot{0};
    std::atomic<int> g_StateResult{0};    // last result for the overlay: 1 saved, 2 loaded, 3 verified,
                                          // -1 can't save here, -2 no such state, -3 checksum mismatch
    std::atomic<int> g_StateResultSeq{0}; // counts results, so the overlay sees two saves in a row
    void SetResult(int r) { g_StateResult = r; g_StateResultSeq++; }

    int g_FnDrawEnable = -1, g_FnAudioGain = -1, g_FnAudioStopAll = -1;

    void CallBuiltin(int& fn, const char* name, CInstance* self, int argc, RValue* args)
    {
        if (fn < 0) Code_Function_Find(name, &fn);
        if (fn < 0) return;
        RValue result = {};
        Script_Perform(fn, self, self, argc, &result, args);
    }

    // While fast-forwarding, the game's Draw events still have to run: games do game logic in them
    // (UFO 50's sound effects are played, and their state kept, in oAudioHandler's Draw event), and
    // without them a replay plays out differently. What's slow is showing each frame (it waits for
    // the screen), so only that is skipped: GameMaker's Draw_Automatic both runs the Draw events
    // (read before the room is drawn) and shows the frame (read after). It's set at the start of
    // every step and cleared during the drawing (pfo_android_screen, from oScreenHandler's last
    // Draw event).
    bool g_ShowFrames = true;
    bool* DrawAutomatic() { static bool* p = Sym<bool*>("Draw_Automatic"); return p; }
    void SetDraw(bool show, CInstance*)
    {
        g_ShowFrames = show;
        if (bool* d = DrawAutomatic()) *d = true;
    }
    void BeforeDraw()   // the start of every step
    {
        if (bool* d = DrawAutomatic()) *d = true;
    }
    void DuringDraw()   // pfo_android_screen
    {
        if (!g_ShowFrames) if (bool* d = DrawAutomatic()) *d = false;
    }

    void SetFastForward(bool on, CInstance* self)
    {
        g_Session.m_SpeedMultiplier = on ? c_RestoreSpeed : 1.0;
        g_Session.UpdateGameSpeed(self);
        SetDraw(!on, self);
        if (!on) { RValue none; CallBuiltin(g_FnAudioStopAll, "audio_stop_all", self, 0, &none); }
        RValue gain; init_real(gain, on ? 0.0 : 1.0);
        CallBuiltin(g_FnAudioGain, "audio_master_gain", self, 1, &gain);
    }

    // in the app's external folder (Android/data/<pkg>/files/savestates): easy to back up or copy
    std::string StatePath(int slot)
    {
        std::string dir = "/sdcard/Android/data/" PFO_PACKAGE "/files/savestates";
        mkdir(dir.c_str(), 0700);
        return dir + "/slot" + std::to_string(slot) + ".pfostate";
    }

    void LogBytes(const char* what, const uint8* p, size_t n)
    {
        char hex[64] = {}; int o = 0;
        for (size_t i = 0; i < n && i < 16; i++) o += snprintf(hex + o, sizeof(hex) - o, "%02x", p[i]);
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "%s: %s (replay version %08x)", what, hex, c_ReplayVersion);
    }

    bool InSoloSession() { return g_Session.m_OnlineState == EOnlineState::Online && g_Session.m_ClientCount == 1; }

    bool CanSave()
    {
        return InSoloSession() && !g_Restore.active && g_Session.m_ReplayReader.m_Buffer == nullptr
            && g_Session.m_ReplayWriter.m_Buffer != nullptr;
    }

    // ---- online save states: commands in the input stream ----
    // Online, a save or a load has to happen on every device on the same frame (a capture leaves
    // traces in the running game; a load changes it). So they travel as input commands: bits 16-23
    // of a seated player's input hold the command, 24-31 its parameter (PFO's own GML commands use
    // 1-30). Every device reads them on the same frame, and the replay records them.
    constexpr int c_CmdSave = 240, c_CmdPropose = 241, c_CmdAccept = 242, c_CmdDecline = 243;
    std::atomic<int> g_PendingCmd{0};        // command | param << 8, to go out with our next input
    uint8 g_CmdCounter = 0;                  // SAVE's parameter: tells two quick saves apart

    bool InOnlineMatch() { return g_Session.m_OnlineState == EOnlineState::Online && g_Session.m_ClientCount > 1; }
    bool HaveSeat() { auto& s = g_Session; return s.m_ClientIndex >= 0 && s.m_ClientData[s.m_ClientIndex].m_PlayerData.m_PlayerIndex >= 0; }

    bool CanSaveOnline()
    {
        return InOnlineMatch() && HaveSeat() && !g_Restore.active && g_Session.m_ReplayReader.m_Buffer == nullptr
            && g_Session.m_ReplayWriter.m_Buffer != nullptr;
    }

    // dllmain.cpp: our input as the game made it, before it's stored and sent
    unsigned int InjectCommand(unsigned int input)
    {
        int c = g_PendingCmd.load();
        if (c == 0 || (input >> 16) != 0) return input;   // the game is sending a command of its own: next frame
        g_PendingCmd = 0;
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online: sending command %d (%d) at frame %u", c & 0xff, (c >> 8) & 0xff, g_Session.m_Frame);
        return input | (static_cast<InputFlags_t>(c & 0xff) << 16) | (static_cast<InputFlags_t>((c >> 8) & 0xff) << 24);
    }

    // saves made online in this match: the match's save number -> its snapshot (our slot live; in a
    // replay, a snapshot id), and -> our save file for it (live: the same slot; replaying, the file
    // we wrote for it before, found by the match id and number)
    std::map<int, int> g_MpSlots;
    std::map<int, int> g_MpFileSlot;

    // the same on every device in the match (and for a rejoiner, which takes its old identity back)
    std::string MatchId()
    {
        uint64 h = 1469598103934665603ull;
        for (int i = 0; i < g_Session.m_ClientCount; i++)
        {
            uint64 id = g_Session.m_ClientData[i].m_SteamID;
            for (int b = 0; b < 8; b++) { h ^= (id >> (8 * b)) & 0xff; h *= 1099511628211ull; }
        }
        char t[20]; snprintf(t, sizeof(t), "%016llx", static_cast<unsigned long long>(h));
        return t;
    }

    std::vector<uint8> ReadWhole(const char* path, bool& exists);
    // our save file for this match's save number, if we have one
    int FindMatchSaveFile(const std::string& match, int index)
    {
        std::string dir = "/sdcard/Android/data/" PFO_PACKAGE "/files/savestates";
        int found = -1;
        if (DIR* d = opendir(dir.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                int id = 0; char rest[32] = {};
                if (sscanf(e->d_name, "slot%d.%31s", &id, rest) != 2 || strcmp(rest, "json")) continue;
                bool exists = false;
                auto v = ReadWhole((dir + "/" + e->d_name).c_str(), exists);
                std::string j(v.begin(), v.end());
                if (j.find("\"match\":\"" + match + "\"") != std::string::npos && j.find("\"mp\":" + std::to_string(index) + ",") != std::string::npos) found = id;
                else if (j.find("\"match\":\"" + match + "\"") != std::string::npos && j.find("\"mp\":" + std::to_string(index) + "}") != std::string::npos) found = id;
            }
            closedir(d);
        }
        return found;
    }
    int g_MpSaveCount = 0;
    int g_MpSavePending = 0;                 // a SAVE arrived this frame: save next step (as a solo save would)
    bool g_MpSavePendingAuto = false;        // ... an auto save (into our AUTO slot, replaced each time)
    constexpr int c_AutoSaveParam = 255;     // SAVE's parameter for "auto save on pause"
    constexpr int c_AutoSlot = 0;
    bool g_AutoSavedThisPause = false;       // the game's pause already got its auto save (others' are ignored)
    constexpr int c_MpReplaySnapBase = 1000000;   // snapshot ids for saves passed while replaying
    std::atomic<int> g_LastSavedSlot{-1};

    // A session starts in the library, which waits for the library's textures without loading
    // them (the title screen does that). From inside a game they've been unloaded: load them
    // first, with the game's own load_datagroup(0, textures, no music).
    void CallScript(const char* name, CInstance* self, int argc = 0, RValue* args = nullptr)
    {
        int fn = Script_Find_Id(name);
        if (fn < 0) { __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "no script %s", name); return; }
        RValue result = {};
        Script_Perform(fn, self, self, argc, &result, args);
    }
    bool GlobalGet(const char* name, RValue& out, CInstance* self);

    // A session starts in the library, so one started from inside a game (a solo session, or a
    // load replaying from the start) first leaves the game the way the game itself does
    // (scrExitToLibrary, without its saving): PFO's session start goes straight to the library and
    // would leave the game's persistent instances (and the like) behind, and the replay would
    // start somewhere the recording didn't.
    void LeaveGame(CInstance* self)
    {
        RValue v;
        int game = GlobalGet("currGame", v, self) ? static_cast<int>(YYGetReal(&v, 0)) : 0;
        if (game <= 0) return;
        int gameID = GlobalGet("currGameID", v, self) ? static_cast<int>(YYGetReal(&v, 0)) : 0;
        CallScript("muteAll", self);
        if (gameID > 0) { RValue a[3]; init_real(a[0], gameID); init_bool(a[1], true); init_bool(a[2], true); CallScript("unload_datagroup", self, 3, a); }
        if (GlobalGet("paused", v, self) && YYGetBool(&v, 0)) CallScript("scrUnpause", self);
        CallScript("scrCleanup", self);
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "left game %d before the session start", game);
    }

    void PreloadLibrary(CInstance* self)
    {
        LeaveGame(self);
        static int loadGroup = -2;
        if (loadGroup == -2) loadGroup = Script_Find_Id("load_datagroup");
        if (loadGroup < 0) return;
        RValue args[3]; init_real(args[0], 0); init_bool(args[1], true); init_bool(args[2], false);
        RValue result = {};
        Script_Perform(loadGroup, self, self, 3, &result, args);
    }

    // A PFO session with just us: no lobby, no connections, recorded like an online session.
    bool StartSolo(CInstance* self)
    {
        if (g_Session.m_OnlineState != EOnlineState::Offline) return false;
        PreloadLibrary(self);
        g_Session.m_ClientIndex = 0;
        g_Session.m_ClientCount = 1;
        g_Session.m_ClientData[0].m_SteamID = g_SteamUser->GetSteamID().ConvertToUint64();
        g_Session.m_OnlineState = EOnlineState::GoingOnline;
        g_TouchFrames.clear();
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "solo session started");
        return true;
    }

    // ---- what a slot shows: the game, its picture, who was playing ----
    // the session frame the current game was started on ("how long into the game")
    uint32 g_GameStartFrame = 0;
    int g_TrackedGameID = -1;
    int g_FnGlobalGet = -1;
    bool GlobalGet(const char* name, RValue& out, CInstance* self)
    {
        if (g_FnGlobalGet < 0) Code_Function_Find("variable_global_get", &g_FnGlobalGet);
        if (g_FnGlobalGet < 0) return false;
        RValue arg; YYCreateString(&arg, name);
        out = {};
        Script_Perform(g_FnGlobalGet, self, self, 1, &out, &arg);
        return true;
    }

    std::string JsonStr(const char* s)
    {
        std::string r = "\"";
        for (const char* p = s ? s : ""; *p; p++)
        {
            if (*p == '"' || *p == '\\') r += '\\';
            if (static_cast<unsigned char>(*p) >= 0x20) r += *p;
        }
        return r + "\"";
    }

    std::string ValueString(RValue& v)
    {
        int kind = KIND_RValue(&v);
        if (kind == VALUE_STRING) return YYGetString(&v, 0);
        if (kind == VALUE_REAL || kind == VALUE_INT32 || kind == VALUE_INT64) return std::to_string(static_cast<long long>(YYGetReal(&v, 0)));
        return "";
    }

    // ---- snapshots (the game side is scripts/pfo_snapshot.gml) ----
    // pfo_snapshot(command, id) -> its result as text
    uint32 *g_RngState = nullptr, *g_RngIndex = nullptr, *g_RngSeed = nullptr;
    bool RngPointers()
    {
        if (!g_RngState) { g_RngState = Sym<uint32*>("state"); g_RngIndex = Sym<uint32*>("g_RndIndex"); g_RngSeed = Sym<uint32*>("g_nRandSeed"); }
        return g_RngState && g_RngIndex && g_RngSeed;
    }

    // Our game-side code must leave no trace on the game: GameMaker's random generator is put back
    // afterwards (some built-ins, sorting for one, draw random numbers).
    std::string Snap(const char* cmd, int id, CInstance* self)
    {
        static int fn = -2;
        if (fn == -2) fn = Script_Find_Id("pfo_snapshot");
        if (fn < 0) return "no script";
        uint32 rng[16] = {}, rndIndex = 0, rndSeed = 0;
        bool haveRng = RngPointers();
        if (haveRng) { memcpy(rng, g_RngState, sizeof(rng)); rndIndex = *g_RngIndex; rndSeed = *g_RngSeed; }
        RValue args[2]; YYCreateString(&args[0], cmd); init_real(args[1], id);
        RValue result = {};
        Script_Perform(fn, self, self, 2, &result, args);
        bool restoring = !strcmp(cmd, "restore");   // a restore sets the generator itself (natively, after)
        if (haveRng && !restoring)
        {
            bool moved = memcmp(rng, g_RngState, sizeof(rng)) != 0 || rndIndex != *g_RngIndex;
            memcpy(g_RngState, rng, sizeof(rng)); *g_RngIndex = rndIndex; *g_RngSeed = rndSeed;
            if (moved) __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "snapshot %s drew random numbers (put back)", cmd);
        }
        return ValueString(result);
    }

    // test commands: snap (capture + fingerprint), unsnap (restore, may take two steps), hash
    int g_SnapTest = 0;          // 1: restoring (waiting for the room change)
    void SnapTest(const char* what, CInstance* self)
    {
        int64 t0 = fakesteam::NowMs();
        std::string r = Snap(what, 99, self);
        int64 t1 = fakesteam::NowMs();
        std::string h = Snap("hash", 0, self);
        // development: remember everything item by item at capture; list what differs after a restore
        if (!strcmp(what, "capture")) Snap("keep", 0, self);
        else if (r == "0") Snap("diff", 0, self);
        int64 t2 = fakesteam::NowMs();
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "snapshot %s -> %s in %lld ms; fingerprint %s (%lld ms)", what, r.c_str(),
            static_cast<long long>(t1 - t0), h.c_str(), static_cast<long long>(t2 - t1));
        g_SnapTest = (!strcmp(what, "restore") && r == "1") ? 1 : 0;
    }

    // slot<N>.json next to the state: read by the overlay for the slot card
    std::string MatchId();
    void WriteSlotInfo(int slot, const StateHeader& h, CInstance* self, int mpIndex = 0)
    {
        // currGame: the game's number in the collection (titles are indexed by it);
        // currGameID: its internal id (the library's icons are indexed by that)
        int game = 0, icon = 0, numPlayers = 1;
        std::string title;
        RValue v;
        if (GlobalGet("currGame", v, self)) game = static_cast<int>(YYGetReal(&v, 0));
        if (GlobalGet("currGameID", v, self)) icon = static_cast<int>(YYGetReal(&v, 0));
        if (GlobalGet("numPlayers", v, self)) numPlayers = static_cast<int>(YYGetReal(&v, 0));
        if (game > 0 && GlobalGet("mGameTitle", v, self) && KIND_RValue(&v) == VALUE_ARRAY && game < YYArrayGetLength(&v))
        {
            RValue t = {};
            GET_RValue(&t, &v, nullptr, game, false, false);
            title = ValueString(t);
        }
        // who's in the session: PFO's client names (a solo session has just us)
        std::string players = "[";
        int clients = g_Session.m_ClientCount;
        RValue names = {};
        bool haveNames = clients > 1 && GlobalGet("onlineClientNames", names, self) && KIND_RValue(&names) == VALUE_ARRAY;
        for (int c = 0; c < clients; c++)
        {
            std::string n;
            if (c == g_Session.m_ClientIndex) n = fakesteam::g_MyName;
            else if (haveNames && c < YYArrayGetLength(&names)) { RValue e = {}; GET_RValue(&e, &names, nullptr, c, false, false); n = ValueString(e); }
            int seat = -1;
            for (int p = 0; p < c_PlayerLimit; p++) if (g_Session.m_PlayerIndexToClientIndexMap[p] == c) { seat = p; break; }
            players += std::string(c ? "," : "") + "{\"name\":" + JsonStr(n.c_str()) + ",\"seat\":" + std::to_string(seat) + "}";
        }
        players += "]";
        std::string json = "{\"version\":1,\"slot\":" + std::to_string(slot) + ",\"savedAt\":" + std::to_string(h.savedAt)
            + ",\"frame\":" + std::to_string(h.frame) + ",\"gameFrames\":" + std::to_string(icon > 0 ? h.frame - g_GameStartFrame : 0)
            + ",\"game\":" + std::to_string(game) + ",\"icon\":" + std::to_string(icon) + ",\"title\":" + JsonStr(title.c_str())
            + ",\"online\":" + (clients > 1 ? "true" : "false")
            + ",\"numPlayers\":" + std::to_string(numPlayers) + ",\"clients\":" + std::to_string(clients)
            + ",\"players\":" + players + (mpIndex > 0 ? ",\"match\":\"" + MatchId() + "\",\"mp\":" + std::to_string(mpIndex) : std::string()) + "}";
        std::string path = StatePath(slot);
        path = path.substr(0, path.size() - 9) + ".json";   // slot<N>.pfostate -> slot<N>.json
        if (FILE* f = fopen(path.c_str(), "w")) { fputs(json.c_str(), f); fclose(f); }
    }

    // where the game draws its picture (pfo_android_screen, patched into oScreenHandler): the
    // overlay crops its screenshot for a slot to this
    std::atomic<int> g_ScreenRect[6] = { {0}, {0}, {0}, {0}, {0}, {0} };   // x, y, w, h, window w, window h

    void SnapCapture(int id, CInstance* self, bool mp = false);   // snapshots, below

    void SnapCapture(int id, CInstance* self, bool mp);
    bool SaveCore(int slot, CInstance* self, int mpIndex);

    bool Save(int slot, CInstance* self)
    {
        if (!CanSave()) { SetResult(-1); return false; }
        return SaveCore(slot, self, 0);
    }

    bool SaveCore(int slot, CInstance* self, int mpIndex)
    {
        auto& s = g_Session;
        StateHeader h;
        h.frame = s.m_Frame;
        h.lastFrameWritten = s.m_ReplayLastFrameWritten;
        auto& me = s.m_ClientData[s.m_ClientIndex];
        h.checksum = me.m_ChecksumData[s.m_Frame & (countof(me.m_ChecksumData) - 1)].m_Checksum;
        h.replaySize = static_cast<uint32>(s.m_ReplayWriter.m_Offset);
        h.savedAt = static_cast<int64>(time(nullptr));
        std::string path = StatePath(slot), part = path + ".part";
        FILE* f = fopen(part.c_str(), "wb");
        if (!f) { SetResult(-1); return false; }
        LogBytes("save bytes", s.m_ReplayWriter.m_Buffer, h.replaySize);
        bool ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(s.m_ReplayWriter.m_Buffer, 1, h.replaySize, f) == h.replaySize;
        uint32 tcount = static_cast<uint32>(g_TouchFrames.size());
        ok = ok && fwrite(&c_TouchMagic, 4, 1, f) == 1 && fwrite(&tcount, 4, 1, f) == 1
            && (tcount == 0 || fwrite(g_TouchFrames.data(), 4, tcount, f) == tcount);
        ok = fclose(f) == 0 && ok;
        if (ok) ok = rename(part.c_str(), path.c_str()) == 0;
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "save slot %d: frame %u, %u bytes, checksum %08x: %s",
            slot, h.frame, h.replaySize, h.checksum, ok ? "ok" : "FAILED");
        if (ok) WriteSlotInfo(slot, h, self, mpIndex);
        if (ok) SnapCapture(slot, self, mpIndex > 0);
        if (ok) g_LastSavedSlot = slot;
        SetResult(ok ? 1 : -1);
        return ok;
    }

    bool SnapLoadBegin(int id, CInstance* self);
    void RejoinAfterReplay(CInstance* self);
    bool RejoinReplayOk(bool match);   // false: a rejoin whose replay didn't check out (it's given up)
    bool g_ForceReplay = false;   // testing: the next load replays even if its snapshot is here

    bool Load(int slot, CInstance* self)
    {
        if (g_Restore.active) return false;
        if (g_Session.m_OnlineState != EOnlineState::Offline && !InSoloSession()) { SetResult(-1); return false; }
        if (!g_ForceReplay && SnapLoadBegin(slot, self)) return true;
        g_ForceReplay = false;   // instant, if this run of the app has its snapshot
        FILE* f = fopen(StatePath(slot).c_str(), "rb");
        if (!f) { SetResult(-2); return false; }
        StateHeader h;
        bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == c_StateMagic && h.version == c_StateVersion
            && h.replayVersion == c_ReplayVersion && h.replaySize > 0;
        std::vector<uint8> data;
        if (ok) { data.resize(h.replaySize); ok = fread(data.data(), 1, h.replaySize, f) == h.replaySize; }
        std::vector<uint32> touches;
        uint32 tmagic = 0, tcount = 0;
        if (ok && fread(&tmagic, 4, 1, f) == 1 && tmagic == c_TouchMagic && fread(&tcount, 4, 1, f) == 1 && tcount < 1000000)
        {
            touches.resize(tcount);
            if (tcount && fread(touches.data(), 4, tcount, f) != tcount) touches.clear();
        }
        fclose(f);
        if (!ok) { SetResult(-2); __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "load slot %d: not a usable state", slot); return false; }

        LogBytes("load bytes", data.data(), data.size());
        g_Restore.data = std::move(data);
        g_Restore.header = h;
        g_Restore.slot = slot;
        g_Restore.active = true;
        g_Restore.startMs = fakesteam::NowMs();
        PreloadLibrary(self);

        g_ReplayChecksumsChecked = 0;
        g_ReplayFirstMismatchFrame = 0;
        g_ReplayTouch = touches;
        g_ReplayTouchNext = 0;
        g_TouchFrames = touches;   // the timeline we're going back to
        g_Session.PlayReplay(g_Restore.data.data(), static_cast<int>(g_Restore.data.size()), 0, self);
        SetFastForward(true, self);
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "load slot %d: replaying %u frames", slot, h.frame);
        return true;
    }

    // The replay has reached the saved frame: carry on live, recording from where the state left off.
    void FinishRestore(CInstance* self)
    {
        auto& s = g_Session;
        auto& h = g_Restore.header;
        auto& me = s.m_ClientData[s.m_ClientIndex];
        uint32 checksum = me.m_ChecksumData[s.m_Frame & (countof(me.m_ChecksumData) - 1)].m_Checksum;
        // on track all the way: the replay's own checksums (one every 255 frames) and the final one
        bool match = checksum == h.checksum && s.m_Frame == h.frame && g_ReplayFirstMismatchFrame == 0;

        reset(s.m_ReplayReader, nullptr, 0);
        reset(s.m_ReplayWriter, g_ReplayBuffer, narrow_cast<int>(c_ReplayBufferSize - 1u));
        memcpy(g_ReplayBuffer, g_Restore.data.data(), g_Restore.data.size());
        s.m_ReplayWriter.m_Offset = static_cast<int>(g_Restore.data.size());
        s.m_ReplayLastFrameWritten = h.lastFrameWritten;

        // During the replay the save files lived in memory; write ours back so progress from here
        // on is saved to disk as usual. (Files with no extension are PFO's session files.)
        for (auto& fd : s.m_FileData)
        {
            if (fd.m_Filename == nullptr || fd.m_OwnerClientIndex != s.m_ClientIndex || strchr(fd.m_Filename, '.') == nullptr) continue;
            if (fd.m_Status == EFileStatus::VirtualExists && fd.m_Data != nullptr)
            {
                int buffer = CreateBuffer(fd.m_Size, eBuffer_Format_Fixed, 1);
                BufferWriteContent(buffer, 0, fd.m_Data, fd.m_Size, false, false);
                RValue args[2]; init_buffer(args[0], buffer); YYCreateString(&args[1], fd.m_Filename);
                RValue result = {};
                Script_Perform(g_gml_buffer_save, self, self, 2, &result, args);
                Script_Perform(g_gml_buffer_delete, self, self, 1, &result, &args[0]);
            }
            else if (fd.m_Status == EFileStatus::VirtualDoesNotExist)
            {
                RValue arg; YYCreateString(&arg, fd.m_Filename);
                RValue result = {};
                Script_Perform(g_gml_file_delete, self, self, 1, &result, &arg);
            }
            else continue;
            if (fd.m_Data != nullptr) { YYFree(fd.m_Data); fd.m_Data = nullptr; }
            fd.m_Size = 0;
            fd.m_Status = EFileStatus::Physical;
        }

        SetFastForward(false, self);
        g_Restore.active = false;
        g_Restore.data.clear();
        g_Restore.data.shrink_to_fit();
        int64 ms = fakesteam::NowMs() - g_Restore.startMs;
        __android_log_print(match ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "PFOSTATE",
            "load slot %d: reached frame %u in %lld ms (%.0f frames/s), checksum %08x vs saved %08x, %d replay checksums%s%s: %s",
            g_Restore.slot, s.m_Frame, static_cast<long long>(ms), ms > 0 ? s.m_Frame * 1000.0 / ms : 0.0,
            checksum, h.checksum, g_ReplayChecksumsChecked, g_ReplayFirstMismatchFrame ? ", first mismatch at frame " : "",
            g_ReplayFirstMismatchFrame ? std::to_string(g_ReplayFirstMismatchFrame).c_str() : "", match ? "verified" : "MISMATCH");
        SetResult(match ? 3 : -3);
        if (!RejoinReplayOk(match)) return;
        RejoinAfterReplay(self);
    }

    // ---- snapshots: instant loads -----------------------------------------------------------
    // Alongside each save (in this run of the app) a snapshot is kept in memory: the game's own
    // state (pfo_snapshot.gml), PFO's session, GameMaker's random generator and our save files on
    // disk. For the 120 frames after the save, the checksum PFO computes for every frame is noted.
    // Loading puts the snapshot back, plays those 120 frames flat out comparing every checksum,
    // then puts the snapshot back once more and hands over. Any difference, or no snapshot (an
    // older save, after a restart), and the load replays the whole session instead.
    constexpr int c_SnapVerifyFrames = 120;
    constexpr size_t c_SnapKeep = 6;          // newest snapshots kept

    struct SnapFile
    {
        int index = 0, owner = 0;
        std::string name;
        EFileStatus status = EFileStatus::None;
        bool hasData = false, onDisk = false, ours = false;
        std::vector<uint8> data, disk;
    };

    struct NativeSnap
    {
        int id = 0;
        bool ready = false;
        bool mp = false;                      // an online save: loaded forward, without the 120-frame check
        uint32 frame = 0;
        int64 takenMs = 0;
        std::vector<uint8> session;           // PFO's Session, byte for byte
        std::vector<SnapFile> files;
        uint32 rng[16] = {}, rndIndex = 0, rndSeed = 0;
        std::vector<uint32> checksums;        // frames frame+1 .. frame+120
        std::vector<uint32> touches;          // the timeline's touches up to frame (incl. this capture's)
        std::vector<uint32> touchesAfter;     // and in the 120 frames after (for the check)
        std::vector<uint8> replay;            // the session's replay up to frame+120
        int offsetAtFrame = 0;
        uint32 lastWrittenAtFrame = 0;
    };
    std::map<int, NativeSnap> g_Snaps;
    // development: files/snapdebug makes a snapshot remember the whole game state one frame after
    // it was taken ("keep"), and a load compare against it at the same frame ("diff", logcat PFOSNAP)
    bool SnapDebug() { return access("/sdcard/Android/data/" PFO_PACKAGE "/files/snapdebug", F_OK) == 0; }
    int SnapDebugFrame()   // which checked frame to compare (0 = the first), from the file
    {
        int k = 0;
        if (FILE* f = fopen("/sdcard/Android/data/" PFO_PACKAGE "/files/snapdebug", "r")) { if (fscanf(f, "%d", &k) != 1) k = 0; fclose(f); }
        return k;
    }

    // GameMaker's own instance bookkeeping, which GML can't see (runner 2024.14, 64-bit; offsets
    // from the disassembly of libyoyo). The order of these lists is game state: events run per
    // object in its list's order, `with` walks the second list, and re-activating or creating an
    // instance appends it at the end.
    namespace gmi
    {
        template <typename T> T At(const void* p, int off) { return *reinterpret_cast<const T*>(static_cast<const uint8*>(p) + off); }
        constexpr int RoomActive = 0x88, RoomActiveCount = 0x98, RoomInactive = 0xa0, RoomInactiveCount = 0xb0;
        constexpr int InstCreate = 0x80, InstObject = 0x88, InstFlags = 0xb0, InstId = 0xb4, InstObjectIndex = 0xb8, InstNext = 0x198, InstDepth = 0x1f0;
        constexpr int ObjParent = 0x8, ObjParentIndex = 0x8c, ObjList = 0x50, ObjListCount = 0x60, ObjWithList = 0x68, ObjWithListCount = 0x78;   // nodes: next, prev, instance
        void** g_RunRoom = nullptr; int* g_RoomMaxId = nullptr; int64* g_CreateCounter = nullptr;
        bool Pointers()
        {
            if (!g_RunRoom) { g_RunRoom = Sym<void**>("Run_Room"); g_RoomMaxId = Sym<int*>("room_maxid"); g_CreateCounter = Sym<int64*>("_ZN9CInstance23ms_CurrentCreateCounterE"); }
            return g_RunRoom && *g_RunRoom && g_RoomMaxId && g_CreateCounter;
        }
        void Inst(std::string& o, const void* i)
        {
            char b[96];
            snprintf(b, sizeof(b), "%d:%d:%x:%lld:%g ", At<int>(i, InstId), At<int>(i, InstObjectIndex), At<uint32>(i, InstFlags),
                static_cast<long long>(At<int64>(i, InstCreate)), static_cast<double>(At<float>(i, InstDepth)));
            o += b;
        }
        void NodeList(std::string& o, const void* obj, int off)
        {
            int n = 0;
            for (const void* node = At<const void*>(obj, off); node && n < 100000; node = At<const void*>(node, 0), n++)
                if (const void* i = At<const void*>(node, 0x10)) { char b[16]; snprintf(b, sizeof(b), "%d ", At<int>(i, InstId)); o += b; }
        }
        // one line per list: the room's active and inactive lists (id:object:flags:created:depth),
        // then for each object with instances its own list and its `with` list (ids)
        std::string Dump()
        {
            if (!Pointers()) return "no runner symbols\n";
            const void* room = *g_RunRoom;
            std::string o;
            char b[160];
            snprintf(b, sizeof(b), "room_maxid %d create_counter %lld active %d inactive %d\n", *g_RoomMaxId, static_cast<long long>(*g_CreateCounter),
                At<int>(room, RoomActiveCount), At<int>(room, RoomInactiveCount));
            o += b;
            std::map<const void*, int> objects;   // with their parents; named by an instance's object index (parents: -1)
            for (int list : { RoomActive, RoomInactive })
            {
                o += list == RoomActive ? "active " : "inactive ";
                int n = 0;
                for (const void* i = At<const void*>(room, list); i && n < 100000; i = At<const void*>(i, InstNext), n++)
                {
                    Inst(o, i);
                    const void* own = At<const void*>(i, InstObject);
                    objects[own] = At<int>(i, InstObjectIndex);
                    int depth = 0;
                    for (const void* obj = At<const void*>(own, ObjParent); obj && depth < 32; obj = At<const void*>(obj, ObjParent), depth++)
                        objects.emplace(obj, -1);
                }
                o += "\n";
            }
            std::map<std::string, std::string> lines;   // in a stable order: by object index, parents by their first child
            for (auto& [obj, index] : objects)
            {
                std::string l;
                snprintf(b, sizeof(b), "object %d parent %d own %d: ", index, At<int>(obj, ObjParentIndex), At<int>(obj, ObjListCount)); l += b; NodeList(l, obj, ObjList);
                snprintf(b, sizeof(b), "| with %d: ", At<int>(obj, ObjWithListCount)); l += b; NodeList(l, obj, ObjWithList);
                char key[32]; snprintf(key, sizeof(key), "%08d %p", index < 0 ? 99999999 : index, obj);
                lines[key] = l + "\n";
            }
            for (auto& [k, l] : lines) o += l;
            return o;
        }
        template <typename T> T& Ref(void* p, int off) { return *reinterpret_cast<T*>(static_cast<uint8*>(p) + off); }
        constexpr uint32 FlagOff = 2;   // switched off (1 is "destroyed"); the lists catch up at the next list update

        template <typename F> void ForEachInstance(F f)   // both of the room's lists
        {
            if (!Pointers()) return;
            for (int list : { RoomActive, RoomInactive })
            {
                int n = 0;
                for (void* i = At<void*>(*g_RunRoom, list); i && n < 100000; i = At<void*>(i, InstNext), n++) f(i, list == RoomInactive);
            }
        }

        // Reading the game needs everything switched on: GML does that with instance_activate_all(),
        // which only clears each instance's off bit (the lists move later). Switching back off from
        // GML can't reach instances still waiting in the switched-off list, so the off bits are put
        // back here instead, exactly as they were: reading leaves no trace.

        // What a snapshot keeps of it: every instance's off bit, which list it's in and where, when it
        // was made (relative to the runner's event counter: an instance made during the current event
        // doesn't run it), and each object's two instance lists in order.
        struct State
        {
            struct Inst { int id; bool off; int64 created; };
            std::vector<Inst> active, inactive;   // in list order
            std::map<void*, std::pair<std::vector<int>, std::vector<int>>> objects;   // own list, `with` list
            int64 counter = 0;
            int roomMaxId = 0;
            int activeCount = 0;   // instance_count
        };
        void NodeIds(void* obj, int off, std::vector<int>& out)
        {
            int n = 0;
            for (void* node = At<void*>(obj, off); node && n < 100000; node = At<void*>(node, 0), n++)
                if (void* i = At<void*>(node, 0x10)) out.push_back(At<int>(i, InstId));
        }
        State Capture()
        {
            State st;
            if (!Pointers()) return st;
            st.counter = *g_CreateCounter;
            st.roomMaxId = *g_RoomMaxId;
            st.activeCount = At<int>(*g_RunRoom, RoomActiveCount);
            ForEachInstance([&](void* i, bool inactive)
            {
                if (At<uint32>(i, InstFlags) & 1) return;   // destroyed, gone at the end of the step
                (inactive ? st.inactive : st.active).push_back({ At<int>(i, InstId), (At<uint32>(i, InstFlags) & FlagOff) != 0, At<int64>(i, InstCreate) });
                int depth = 0;
                for (void* obj = At<void*>(i, InstObject); obj && depth < 32; obj = At<void*>(obj, ObjParent), depth++)
                    if (!st.objects.count(obj)) { auto& o = st.objects[obj]; NodeIds(obj, ObjList, o.first); NodeIds(obj, ObjWithList, o.second); }
            });
            return st;
        }

        // put a room list (instances) or an object's list (nodes) in this order: the ones named first,
        // the rest after them as they were
        void RelinkRoom(void* room, int list, const std::vector<void*>& order)
        {
            std::set<void*> named(order.begin(), order.end());
            std::vector<void*> all = order;
            int n = 0;
            for (void* i = At<void*>(room, list); i && n < 100000; i = At<void*>(i, InstNext), n++) if (!named.count(i)) all.push_back(i);
            void* prev = nullptr;
            for (void* i : all) { Ref<void*>(i, InstNext + 8) = prev; if (prev) Ref<void*>(prev, InstNext) = i; prev = i; }
            if (prev) Ref<void*>(prev, InstNext) = nullptr;
            Ref<void*>(room, list) = all.empty() ? nullptr : all.front();
            Ref<void*>(room, list + 8) = prev;
        }
        void RelinkObject(void* obj, int list, const std::vector<int>& ids)
        {
            std::map<int, size_t> rank;
            for (size_t k = 0; k < ids.size(); k++) rank.emplace(ids[k], k);
            std::vector<void*> nodes;
            int n = 0;
            for (void* node = At<void*>(obj, list); node && n < 100000; node = At<void*>(node, 0), n++) nodes.push_back(node);
            auto key = [&](void* node) { void* i = At<void*>(node, 0x10); auto it = i ? rank.find(At<int>(i, InstId)) : rank.end(); return it == rank.end() ? ids.size() : it->second; };
            std::stable_sort(nodes.begin(), nodes.end(), [&](void* a, void* b) { return key(a) < key(b); });
            void* prev = nullptr;
            for (void* node : nodes) { Ref<void*>(node, 8) = prev; if (prev) Ref<void*>(prev, 0) = node; prev = node; }
            if (prev) Ref<void*>(prev, 0) = nullptr;
            Ref<void*>(obj, list) = nodes.empty() ? nullptr : nodes.front();
            Ref<void*>(obj, list + 8) = prev;
        }

        // libPFO runs inside the runner's animation pass over the room's instance list (PFO updates in
        // oController's Animation End event), which walks on from the instance that came after
        // oController when it got there. The ones after it in the snapshot are the ones the live game
        // animated after the snapshot was taken: so before a restore, the instance the snapshot has
        // there is put there, and the pass carries on through the restored list as the live one did.
        int SuccessorId(const State& st, int selfId)
        {
            for (size_t k = 0; k + 1 < st.active.size(); k++) if (st.active[k].id == selfId) return st.active[k + 1].id;
            return -1;
        }
        bool ArrangeSuccessor(const State& st, void* self)
        {
            if (!Pointers()) return false;
            int want = SuccessorId(st, At<int>(self, InstId));
            if (want < 0) return false;
            void* room = *g_RunRoom;
            void* inst = nullptr;
            int n = 0;
            for (void* i = At<void*>(room, RoomActive); i && n < 100000; i = At<void*>(i, InstNext), n++) if (At<int>(i, InstId) == want) { inst = i; break; }
            if (!inst) return false;
            if (At<void*>(self, InstNext) == inst) return true;
            // out of its place...
            void* prev = At<void*>(inst, InstNext + 8);
            void* next = At<void*>(inst, InstNext);
            if (prev) Ref<void*>(prev, InstNext) = next; else Ref<void*>(room, RoomActive) = next;
            if (next) Ref<void*>(next, InstNext + 8) = prev; else Ref<void*>(room, RoomActive + 8) = prev;
            // ...and in after self
            void* after = At<void*>(self, InstNext);
            Ref<void*>(inst, InstNext + 8) = self;
            Ref<void*>(inst, InstNext) = after;
            Ref<void*>(self, InstNext) = inst;
            if (after) Ref<void*>(after, InstNext + 8) = inst; else Ref<void*>(room, RoomActive + 8) = inst;
            return true;
        }

        // after the GML restore: the snapshot's instances (some made afresh: newId maps their old ids)
        // get back their off bits, lists, places in them and creation times
        std::string Apply(const State& st, const std::map<int, int>& newId, bool commitChanges = true)
        {
            if (!Pointers()) return "no runner symbols";
            static auto updateActive = Sym<void (*)(void*)>("_ZN5CRoom12UpdateActiveEv");
            if (!updateActive) return "no CRoom::UpdateActive";
            // instance_change() moves an instance into its new object's lists only at the next list
            // update, and until then it misses that object's events: the restore's made ones need to
            // be in them now (as they were in the live game)
            static auto changeTypes = Sym<void (*)()>("_Z19ChangeInstanceTypesv");
            if (!changeTypes) return "no ChangeInstanceTypes";
            if (commitChanges) changeTypes();
            void* room = *g_RunRoom;
            std::map<int, void*> byId;
            ForEachInstance([&](void* i, bool) { if (!(At<uint32>(i, InstFlags) & 1)) byId[At<int>(i, InstId)] = i; });
            auto now = [&](int id) { auto m = newId.find(id); return m == newId.end() ? id : m->second; };
            auto find = [&](int id) -> void* { auto it = byId.find(now(id)); return it == byId.end() ? nullptr : it->second; };
            int missing = 0;
            int64 shift = *g_CreateCounter - st.counter;
            // 1. into the right list: the runner moves an instance by its off bit
            for (int pass = 0; pass < 2; pass++)
                for (auto& r : pass == 0 ? st.active : st.inactive)
                {
                    void* i = find(r.id);
                    if (!i) { missing++; continue; }
                    auto& f = Ref<uint32>(i, InstFlags);
                    f = pass == 1 ? f | FlagOff : f & ~FlagOff;
                    Ref<int64>(i, InstCreate) = r.created + shift;
                }
            updateActive(room);
            // 2. the off bits as they were (an instance can be off but not yet moved, or the other way round)
            std::vector<void*> active, inactive;
            for (int pass = 0; pass < 2; pass++)
                for (auto& r : pass == 0 ? st.active : st.inactive)
                    if (void* i = find(r.id))
                    {
                        auto& f = Ref<uint32>(i, InstFlags);
                        f = r.off ? f | FlagOff : f & ~FlagOff;
                        (pass == 0 ? active : inactive).push_back(i);
                    }
            // 3. and in the same order
            RelinkRoom(room, RoomActive, active);
            RelinkRoom(room, RoomInactive, inactive);
            for (auto& [obj, lists] : st.objects)
            {
                auto mapped = [&](const std::vector<int>& ids) { std::vector<int> v; for (int id : ids) v.push_back(now(id)); return v; };
                RelinkObject(obj, ObjList, mapped(lists.first));
                RelinkObject(obj, ObjWithList, mapped(lists.second));
            }
            char b[96];
            snprintf(b, sizeof(b), "%zu+%zu instances, %zu objects, %d missing", st.active.size(), st.inactive.size(), st.objects.size(), missing);
            return b;
        }

        // Reading the game (GML) needs every instance where `with (all)` finds it. instance_activate_all()
        // only clears each one's off bit; an instance stays in the switched-off list (invisible to GML)
        // until the runner's next list update. So: note the runner's whole state first (Mark), let
        // the runner move them now (Settle, after instance_activate_all), and afterwards put every
        // off bit, list and order back exactly as noted (Rehide): reading leaves no trace.
        // GameMaker finds what collides by walking its collision tree, and with several candidates the
        // tree's shape decides which one a function like instance_place() returns. The live game's tree
        // has grown with its history; a restored one is built differently. So at a snapshot the tree is
        // rebuilt from the instance lists (RebuildTree, as a room start does), in
        // the live game, in its replays (a "touch" frame) and after a restore alike: with the same lists,
        // the same tree.
        void ResetCollisionTree()
        {
            static auto rebuild = Sym<void (*)(bool)>("_Z11RebuildTreeb");   // as a room start does (DeleteCollisionTrees alone leaves the objects' collision data dangling)
            if (rebuild) rebuild(true);
        }

        State g_MarkState;
        bool g_MarkValid = false;
        void Mark() { g_MarkState = Capture(); g_MarkValid = true; }
        int Settle()
        {
            if (!Pointers()) return 0;
            static auto updateActive = Sym<void (*)(void*)>("_ZN5CRoom12UpdateActiveEv");
            if (!updateActive) return 0;
            int before = At<int>(*g_RunRoom, RoomInactiveCount);
            updateActive(*g_RunRoom);
            return before - At<int>(*g_RunRoom, RoomInactiveCount);
        }
        int Rehide()
        {
            if (!g_MarkValid) return 0;
            g_MarkValid = false;
            Apply(g_MarkState, {}, false);
            return static_cast<int>(g_MarkState.inactive.size());
        }
        void DumpTo(const char* name)
        {
            std::string d = Dump();
            char path[160]; snprintf(path, sizeof(path), "/sdcard/Android/data/" PFO_PACKAGE "/files/%s", name);
            if (FILE* f = fopen(path, "w")) { fwrite(d.data(), 1, d.size(), f); fclose(f); }
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "runner lists -> %s (%zu bytes)", name, d.size());
        }
    }

    std::map<int, gmi::State> g_SnapRunner;   // per snapshot: the runner's side of its instances

    // forward: an online load (put the game state back on the current frame, leave PFO's session alone)
    // nested: an online load met while replaying (a rejoin, or a load by replay): the replay's own
    // restore carries on around it
    struct SnapLoad { int phase = 0, id = 0; int64 startMs = 0; bool forward = false, nested = false; } g_SnapLoad;
    bool g_DiffAfterFinal = false;   // phase 1 restore, 2 check, 3 restore again
    bool g_SnapSkipUpdate = false;            // this step, don't run pfo_update (waiting for a room change)
    bool g_SnapSettle = false;                // a restore finished this step: "settle" after pfo_update

    std::vector<uint8> ReadWhole(const char* path, bool& exists)
    {
        std::vector<uint8> v;
        FILE* f = fopen(path, "rb");
        exists = f != nullptr;
        if (!f) return v;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0) { v.resize(n); if (fread(v.data(), 1, n, f) != static_cast<size_t>(n)) v.clear(); }
        fclose(f);
        return v;
    }

    constexpr size_t c_MpSnapKeep = 24;      // online saves kept for loading in this match

    void SnapCapture(int id, CInstance* self, bool mp)
    {
        auto& s = g_Session;
        int64 t0 = fakesteam::NowMs();
        gmi::State runner = gmi::Capture();
        std::string r = Snap("capture", id, self);
        if (r != "0") { __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "snapshot %d: capture failed (%s)", id, r.c_str()); return; }
        Snap("touched", 0, self);
        gmi::ResetCollisionTree();
        // replays do the same here (online saves are commands in the input stream: replays meet them anyway)
        if (!mp && (g_TouchFrames.empty() || g_TouchFrames.back() != s.m_Frame)) g_TouchFrames.push_back(s.m_Frame);
        if (SnapDebug()) gmi::DumpTo(("lists_live_" + std::to_string(id) + "_0.txt").c_str());
        g_SnapRunner[id] = std::move(runner);
        NativeSnap n;
        n.id = id;
        n.frame = s.m_Frame;
        n.takenMs = t0;
        n.session.resize(sizeof(Session));
        memcpy(n.session.data(), &s, sizeof(Session));
        for (int i = 0; i < countof(s.m_FileData); i++)
        {
            auto& fd = s.m_FileData[i];
            if (fd.m_Filename == nullptr) continue;
            SnapFile f;
            f.index = i; f.owner = fd.m_OwnerClientIndex; f.name = fd.m_Filename; f.status = fd.m_Status;
            if (fd.m_Data != nullptr) { f.hasData = true; f.data.assign(static_cast<const uint8*>(fd.m_Data), static_cast<const uint8*>(fd.m_Data) + fd.m_Size); }
            // our save files as they are on disk now (the game may write them before you load)
            f.ours = fd.m_Status == EFileStatus::Physical && fd.m_OwnerClientIndex == s.m_ClientIndex && strchr(fd.m_Filename, '.') != nullptr;
            if (f.ours) f.disk = ReadWhole(fd.m_Filename, f.onDisk);
            n.files.push_back(std::move(f));
        }
        if (RngPointers()) { memcpy(n.rng, g_RngState, sizeof(n.rng)); n.rndIndex = *g_RngIndex; n.rndSeed = *g_RngSeed; }
        n.offsetAtFrame = s.m_ReplayWriter.m_Offset;
        n.lastWrittenAtFrame = s.m_ReplayLastFrameWritten;
        n.touches = g_TouchFrames;
        n.mp = mp;
        n.ready = mp;   // an online save is loaded forward: nothing to collect
        g_Snaps[id] = std::move(n);
        // keep the newest few of each kind
        for (int kind = 0; kind < 2; kind++)
        {
            size_t keep = kind ? c_MpSnapKeep : c_SnapKeep;
            while (true)
            {
                size_t count = 0;
                auto oldest = g_Snaps.end();
                for (auto it = g_Snaps.begin(); it != g_Snaps.end(); ++it)
                {
                    if (it->second.mp != (kind == 1)) continue;
                    count++;
                    if (oldest == g_Snaps.end() || it->second.takenMs < oldest->second.takenMs) oldest = it;
                }
                if (count <= keep || oldest == g_Snaps.end()) break;
                Snap("drop", oldest->first, self);
                g_Snaps.erase(oldest);
            }
        }
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "snapshot %d: taken at frame %u in %lld ms", id, g_Session.m_Frame, static_cast<long long>(fakesteam::NowMs() - t0));
    }

    // after each live frame: note checksums for snapshots still collecting their 120 frames
    void SnapCollect(CInstance* self)
    {
        auto& s = g_Session;
        for (auto it = g_Snaps.begin(); it != g_Snaps.end();)
        {
            auto& n = it->second;
            if (n.ready || n.mp) { ++it; continue; }
            if (s.m_OnlineState != EOnlineState::Online || s.m_ReplayWriter.m_Buffer == nullptr || s.m_Frame < n.frame + n.checksums.size()
                || s.m_Frame > n.frame + n.checksums.size() + 1)
            {
                // the session ended or jumped (a load): this one can't be checked any more
                if (s.m_Frame != n.frame + n.checksums.size()) { it = g_Snaps.erase(it); continue; }
                ++it; continue;
            }
            if (s.m_Frame == n.frame + n.checksums.size() + 1)
            {
                if (n.checksums.size() == static_cast<size_t>(SnapDebugFrame()) && SnapDebug()) { Snap("keep", 0, self); }
                if (n.checksums.size() < 3 && SnapDebug()) gmi::DumpTo(("lists_live_" + std::to_string(n.id) + "_" + std::to_string(n.checksums.size() + 1) + ".txt").c_str());
                auto& me = s.m_ClientData[s.m_ClientIndex];
                n.checksums.push_back(me.m_ChecksumData[s.m_Frame & (countof(me.m_ChecksumData) - 1)].m_Checksum);
                if (static_cast<int>(n.checksums.size()) == c_SnapVerifyFrames)
                {
                    for (uint32 t : g_TouchFrames) if (t > n.frame) n.touchesAfter.push_back(t);
                    n.replay.assign(s.m_ReplayWriter.m_Buffer, s.m_ReplayWriter.m_Buffer + s.m_ReplayWriter.m_Offset);
                    n.ready = true;
                    __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "snapshot %d: ready (%d checksums noted)", n.id, c_SnapVerifyFrames);
                }
            }
            ++it;
        }
    }

    // put PFO's side back: the session, its file records, the random generator, our save files;
    // then either replay the 120 noted frames (check) or carry on live recording (final)
    void SnapApplyNative(NativeSnap& n, bool final, CInstance* self)
    {
        auto& s = g_Session;
        for (auto& fd : s.m_FileData)
        {
            if (fd.m_Filename) YYFree(const_cast<char*>(fd.m_Filename));
            if (fd.m_Data) YYFree(const_cast<void*>(fd.m_Data));
        }
        // online loads go forward: the session (frame, network, inputs, replay) carries on as it is
        if (!g_SnapLoad.forward) memcpy(static_cast<void*>(&s), n.session.data(), sizeof(Session));
        for (auto& fd : s.m_FileData) { fd.m_Filename = nullptr; fd.m_Data = nullptr; fd.m_Size = 0; fd.m_Status = EFileStatus::None; }
        for (auto& f : n.files)
        {
            auto& fd = s.m_FileData[f.index];
            fd.m_Filename = YYStrDup(f.name.c_str());
            fd.m_OwnerClientIndex = f.owner;
            fd.m_Status = f.status;
            if (f.hasData)
            {
                void* p = YYAlloc(static_cast<int>(std::max<size_t>(1, f.data.size())));
                memcpy(p, f.data.data(), f.data.size());
                fd.m_Data = p;
                fd.m_Size = static_cast<int>(f.data.size());
            }
            if (f.ours)
            {
                if (f.onDisk) { if (FILE* o = fopen(f.name.c_str(), "wb")) { fwrite(f.disk.data(), 1, f.disk.size(), o); fclose(o); } }
                else remove(f.name.c_str());
            }
        }
        if (RngPointers()) { memcpy(g_RngState, n.rng, sizeof(n.rng)); *g_RngIndex = n.rndIndex; *g_RngSeed = n.rndSeed; }
        if (g_SnapLoad.forward) return;
        if (final)
        {
            reset(s.m_ReplayReader, nullptr, 0);
            reset(s.m_ReplayWriter, g_ReplayBuffer, narrow_cast<int>(c_ReplayBufferSize - 1u));
            memcpy(g_ReplayBuffer, n.replay.data(), n.offsetAtFrame);
            s.m_ReplayWriter.m_Offset = n.offsetAtFrame;
            s.m_ReplayLastFrameWritten = n.lastWrittenAtFrame;
        }
        else
        {
            // read the replay from the snapshot's frame on: the next chunk's frame delta comes first
            reset(s.m_ReplayWriter, nullptr, 0);
            reset(s.m_ReplayReader, n.replay.data(), static_cast<int>(n.replay.size()));
            if (n.offsetAtFrame < static_cast<int>(n.replay.size()))
            {
                s.m_ReplayLastFrameRead = n.lastWrittenAtFrame + n.replay[n.offsetAtFrame];
                s.m_ReplayReader.m_Offset = n.offsetAtFrame + 1;
            }
            else
            {
                s.m_ReplayLastFrameRead = n.frame + c_SnapVerifyFrames + 1000;
                s.m_ReplayReader.m_Offset = static_cast<int>(n.replay.size());
            }
        }
        g_ReplayChecksumsChecked = 0;
        g_ReplayFirstMismatchFrame = 0;
        g_TouchFrames = n.touches;
        g_ReplayTouch = final ? std::vector<uint32>() : n.touchesAfter;
        g_ReplayTouchNext = 0;
        s.m_SpeedMultiplier = final ? 1.0 : c_RestoreSpeed;
        g_FinalGameSpeedFPS = 0;   // make UpdateGameSpeed apply it
        s.UpdateGameSpeed(self);
    }

    bool SnapLoadBegin(int id, CInstance* self)
    {
        auto it = g_Snaps.find(id);
        if (it == g_Snaps.end() || !it->second.ready) return false;
        g_SnapLoad = { 1, id, fakesteam::NowMs() };
        g_Restore.active = true;          // the overlay shows the loading screen, input is blocked
        g_Restore.slot = id;
        g_Restore.header.frame = it->second.frame;
        g_Restore.startMs = g_SnapLoad.startMs;
        SnapApplyNative(it->second, false, self);
        SetFastForward(true, self);
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "load slot %d: from its snapshot", id);
        return true;
    }

    void SnapLoadFail(const char* why, CInstance* self)
    {
        int id = g_SnapLoad.id;
        if (g_SnapLoad.forward)
        {
            // online there's no replaying from the start: the other devices' checksums will tell
            __android_log_print(ANDROID_LOG_ERROR, "PFOSTATE", "online load of snapshot %d failed (%s)", id, why);
            bool nested = g_SnapLoad.nested;
            g_SnapLoad.phase = 0; g_SnapLoad.forward = false; g_SnapLoad.nested = false;
            if (nested) return;
            g_Restore.active = false;
            SetDraw(true, self);
            SetResult(-3);
            return;
        }
        __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "load slot %d: snapshot didn't check out (%s), replaying instead", id, why);
        g_SnapLoad.phase = 0;
        g_Snaps.erase(id);
        Snap("drop", id, self);
        g_Restore.active = false;
        Load(id, self);   // the replay path (it starts the session over)
    }

    // before pfo_update while loading a snapshot: put the game state back (phase 1 and 3)
    void SnapLoadBefore(CInstance* self)
    {
        g_SnapSkipUpdate = false;
        if (g_SnapLoad.phase != 1 && g_SnapLoad.phase != 3) return;
        auto it = g_Snaps.find(g_SnapLoad.id);
        if (it == g_Snaps.end()) { SnapLoadFail("gone", self); return; }
        // Just arrived in the snapshot's room: a room change lines the persistent instances up anew,
        // so line up the runner's animation pass again (gmi::ArrangeSuccessor) and restore next step.
        static bool arrived = false;
        if (arrived)
        {
            arrived = false;
            if (!gmi::ArrangeSuccessor(g_SnapRunner[g_SnapLoad.id], self))
                __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "load slot %d: can't line up the animation pass", g_SnapLoad.id);
            g_SnapSkipUpdate = true;
            return;
        }
        void* nextBefore = gmi::Pointers() ? gmi::At<void*>(self, gmi::InstNext) : nullptr;   // where the runner's animation pass carries on
        std::string r = Snap("restore", g_SnapLoad.id, self);
        if (r == "1") { g_SnapSkipUpdate = true; arrived = true; return; }   // changing room first: try again once there
        if (r != "0") { SnapLoadFail(("restore refused: " + r).c_str(), self); return; }
        {
            // the runner's side: off bits, lists and their order, creation times
            std::map<int, int> newId;   // made afresh: old id -> new
            RValue made;
            if (GlobalGet("pfo_snap_made_ids", made, self))
            {
                std::string m = ValueString(made);
                for (size_t k = 0; k < m.size();)
                {
                    int o = 0, nw = 0, used = 0;
                    if (sscanf(m.c_str() + k, "%d:%d,%n", &o, &nw, &used) < 2 || used == 0) break;
                    newId[o] = nw;
                    k += used;
                }
            }
            auto& st = g_SnapRunner[g_SnapLoad.id];
            std::string how = gmi::Apply(st, newId);
            gmi::ResetCollisionTree();
            if (gmi::Pointers() && gmi::At<void*>(self, gmi::InstNext) != nextBefore)
                __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "load slot %d: the animation pass carries on from instance %d, the snapshot has %d there",
                    g_SnapLoad.id, nextBefore ? gmi::At<int>(nextBefore, gmi::InstId) : -1, gmi::SuccessorId(st, gmi::At<int>(self, gmi::InstId)));
            // destroyed instances count in instance_count until the step ends, and PFO's checksum this
            // frame reads it: it's corrected by this much (dllmain.cpp)
            g_ChecksumInstanceFix = gmi::Pointers() ? gmi::At<int>(*gmi::g_RunRoom, gmi::RoomActiveCount) - st.activeCount : 0;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "runner state put back: %s, instance count off by %d this frame", how.c_str(), g_ChecksumInstanceFix);
        }
        if (SnapDebug() && g_SnapLoad.phase == 1) gmi::DumpTo(("lists_restored_" + std::to_string(g_SnapLoad.id) + "_0.txt").c_str());
        bool final = g_SnapLoad.phase == 3 || g_SnapLoad.forward;
        SnapApplyNative(it->second, final, self);              // again: the room change steps ran the game
        g_SnapSettle = true;
        if (!final) { g_SnapLoad.phase = 2; return; }
        if (g_SnapLoad.forward && g_SnapLoad.nested)
        {
            g_SnapLoad.phase = 0; g_SnapLoad.forward = false; g_SnapLoad.nested = false;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online load of snapshot %d: done at frame %u in %lld ms (while replaying)",
                g_SnapLoad.id, g_Session.m_Frame, static_cast<long long>(fakesteam::NowMs() - g_SnapLoad.startMs));
            return;
        }
        if (g_SnapLoad.forward)
        {
            g_SnapLoad.phase = 0; g_SnapLoad.forward = false;
            g_Restore.active = false;
            SetDraw(true, self);   // the load step hid the picture (StateAfterUpdate): show it again
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online load of snapshot %d: done at frame %u in %lld ms",
                g_SnapLoad.id, g_Session.m_Frame, static_cast<long long>(fakesteam::NowMs() - g_SnapLoad.startMs));
            if (SnapDebug()) { __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online load: diff against the save:"); Snap("diff", g_SnapLoad.id, self); }
            SetResult(3);
            return;
        }
        SetFastForward(false, self);
        g_SnapLoad.phase = 0;
        g_Restore.active = false;
        g_DiffAfterFinal = SnapDebug();   // development: compare the first live frame with the original's
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "load slot %d: from its snapshot in %lld ms, %d frames checked: verified",
            g_SnapLoad.id, static_cast<long long>(fakesteam::NowMs() - g_SnapLoad.startMs), c_SnapVerifyFrames);
        SetResult(3);
    }

    // after pfo_update while checking: every frame's checksum against the one noted at save
    void SnapLoadAfter(CInstance* self)
    {
        if (g_SnapLoad.phase != 2) return;
        auto it = g_Snaps.find(g_SnapLoad.id);
        if (it == g_Snaps.end()) { SnapLoadFail("gone", self); return; }
        auto& n = it->second;
        auto& s = g_Session;
        if (s.m_OnlineState != EOnlineState::Online) { SnapLoadFail("session ended", self); return; }
        if (s.m_Frame <= n.frame) return;
        size_t i = s.m_Frame - n.frame - 1;
        if (i >= n.checksums.size()) { SnapLoadFail("ran past the checked frames", self); return; }
        auto& me = s.m_ClientData[s.m_ClientIndex];
        uint32 c = me.m_ChecksumData[s.m_Frame & (countof(me.m_ChecksumData) - 1)].m_Checksum;
        if (i == static_cast<size_t>(SnapDebugFrame()) && SnapDebug()) { Snap("diff", 0, self); }
        if (i < 3 && SnapDebug()) gmi::DumpTo(("lists_restored_" + std::to_string(g_SnapLoad.id) + "_" + std::to_string(i + 1) + ".txt").c_str());
        if (i < 6 && SnapDebug()) __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "check frame %u: count %s, fix %d", static_cast<unsigned>(i + 1), Snap("count", 0, self).c_str(), g_ChecksumInstanceFix);
        if (c != n.checksums[i])
        {
            char why[64]; snprintf(why, sizeof(why), "frame %u of %d differs", static_cast<unsigned>(i + 1), c_SnapVerifyFrames);
            SnapLoadFail(why, self);
            return;
        }
        if (i + 1 == n.checksums.size()) g_SnapLoad.phase = 3;     // all matched: restore once more, then live
    }

    void ReadTestCommand()
    {
        static int countdown = 0;
        if (--countdown > 0) return;
        countdown = 30;
        const char* path = "/sdcard/Android/data/" PFO_PACKAGE "/files/ss_cmd";
        FILE* f = fopen(path, "r");
        if (!f) return;
        char cmd[16] = {}; int slot = 1;
        int n = fscanf(f, "%15s %d", cmd, &slot);
        fclose(f);
        remove(path);
        if (n < 1) return;
        if (!strcmp(cmd, "solo")) g_StateRequest = 1;
        else if (!strcmp(cmd, "save")) { g_StateRequestSlot = slot; g_StateRequest = 2; }
        else if (!strcmp(cmd, "load")) { g_StateRequestSlot = slot; g_StateRequest = 3; }
        else if (!strcmp(cmd, "probe")) g_StateRequest = 9;
        else if (!strcmp(cmd, "snap")) g_StateRequest = 10;
        else if (!strcmp(cmd, "unsnap")) g_StateRequest = 11;
        else if (!strcmp(cmd, "hash")) { g_StateRequestSlot = n > 1 ? slot : 0; g_StateRequest = 12; }
        else if (!strcmp(cmd, "rload")) { g_StateRequestSlot = slot; g_StateRequest = 13; }   // load by replay only
        else if (!strcmp(cmd, "replay")) { g_StateRequestSlot = slot; g_StateRequest = 16; }  // the same, keeping the snapshot
        else if (!strcmp(cmd, "where")) g_StateRequest = 14;                                  // which game, without touching it
        else if (!strcmp(cmd, "sleep")) g_StateRequest = 15;                                  // a 550 ms hitch, nothing else
        else if (!strcmp(cmd, "netdrop")) { fakesteam::g_DropUntilMs = fakesteam::NowMs() + 1000LL * slot; __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "network dropped for %d s", slot); }
        else if (!strcmp(cmd, "rejoin")) g_StateRequest = 20;                                 // rejoin the last match
        else if (!strcmp(cmd, "accept")) g_StateRequest = 17;                                 // online: answer the load vote
        else if (!strcmp(cmd, "decline")) g_StateRequest = 18;
    }

    // before pfo_update: run requests (a load swaps in the replay before this frame is processed)
    // development: files/snapdiffat holding a frame number makes live play remember the whole game
    // state at that frame and a replay compare against it there (logcat PFOSNAP diff)
    void DiffAtFrame(CInstance* self)
    {
        // the file: "<frame>" or "<first frame> <step> <count>"
        static int at = -2, step = 0, count = 1;
        static int64 checked = 0;
        int64 now = fakesteam::NowMs();
        if (now - checked > 2000)
        {
            checked = now; at = -1; step = 0; count = 1;
            if (FILE* f = fopen("/sdcard/Android/data/" PFO_PACKAGE "/files/snapdiffat", "r")) { if (fscanf(f, "%d %d %d", &at, &step, &count) < 1) at = -1; fclose(f); }
        }
        if (at <= 0 || g_Session.m_OnlineState != EOnlineState::Online) return;
        int fr = static_cast<int>(g_Session.m_Frame) - at;
        if (fr < 0 || (step > 0 ? (fr % step != 0 || fr / step >= count) : fr != 0)) return;
        bool replaying = g_Session.m_ReplayReader.m_Buffer != nullptr;
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "diff at frame %u: %s", g_Session.m_Frame, replaying ? "comparing" : "remembering");
        Snap(replaying ? "diff" : "keep", static_cast<int>(g_Session.m_Frame), self);
    }

    void ReplayTouches(CInstance* self)
    {
        if (g_Session.m_ReplayReader.m_Buffer == nullptr) return;
        while (g_ReplayTouchNext < g_ReplayTouch.size() && g_ReplayTouch[g_ReplayTouchNext] < g_Session.m_Frame) g_ReplayTouchNext++;
        if (g_ReplayTouchNext < g_ReplayTouch.size() && g_ReplayTouch[g_ReplayTouchNext] == g_Session.m_Frame)
        {
            g_ReplayTouchNext++;
            Snap("touch", 0, self);
            gmi::ResetCollisionTree();
        }
    }

    int NextSlotId()
    {
        int max = 0;
        std::string dir = "/sdcard/Android/data/" PFO_PACKAGE "/files/savestates";
        if (DIR* d = opendir(dir.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                int id = 0; char rest[32] = {};
                if (sscanf(e->d_name, "slot%d.%31s", &id, rest) == 2 && !strcmp(rest, "pfostate") && id > max) max = id;
            }
            closedir(d);
        }
        return max + 1;
    }

    // a SAVE command arrived: every device saves here. Live, into a new slot of ours; in a replay,
    // just the snapshot (a LOAD later in the replay puts it back, as it did live).
    void MpSave(int index, CInstance* self, bool autoSave)
    {
        auto& s = g_Session;
        bool live = s.m_ReplayReader.m_Buffer == nullptr && s.m_ReplayWriter.m_Buffer != nullptr;
        if (live)
        {
            int slot = autoSave ? c_AutoSlot : NextSlotId();
            if (autoSave) for (auto it = g_MpFileSlot.begin(); it != g_MpFileSlot.end();) it = it->second == c_AutoSlot ? g_MpFileSlot.erase(it) : std::next(it);
            bool ok = SaveCore(slot, self, index);
            if (ok) { g_MpSlots[index] = slot; g_MpFileSlot[index] = slot; }
            if (ok && SnapDebug()) Snap("keep", slot, self);   // development: the online load diffs against this
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online save %d: slot %d at frame %u: %s", index, slot, s.m_Frame, ok ? "ok" : "FAILED");
        }
        else
        {
            int id = c_MpReplaySnapBase + index;
            SnapCapture(id, self, true);
            g_MpSlots[index] = id;
            int file = FindMatchSaveFile(MatchId(), index);   // a rejoiner's own file for it, from before
            if (file >= 0) g_MpFileSlot[index] = file;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online save %d: snapshot only (replaying) at frame %u", index, s.m_Frame);
        }
    }

    // ---- online loads: one player proposes a save of this match, everyone with a seat accepts ----
    // The vote lives in the input stream too, so every device (and any replay) agrees on it.
    constexpr int c_CmdCannot = 244;      // this device can't load that one (it lacks the snapshot)
    constexpr int c_CmdProposeOther = 245;   // propose a save from another match (param: the offer's tag)
    // saves from other matches, offered for loading here: tag -> our slot holding it (the proposer
    // sends it to everyone first; each device keeps it as a save of its own)
    std::map<int, int> g_Offers;
    int g_ReseedPending = 0;                 // everyone accepted one: start next step
    struct Vote
    {
        bool active = false;
        bool other = false;       // a save from another match: everyone restarts the match from it
        int index = 0, proposer = -1;
        uint32 accepted = 0, required = 0, startFrame = 0;
        int64 startMs = 0;
    };
    Vote g_Vote;
    // what became of the last vote, for the overlay: 1 loaded, 2 declined, 3 couldn't, 4 withdrawn/timed out
    std::atomic<int> g_VoteOutcome{0}, g_VoteOutcomePlayer{-1}, g_VoteOutcomeSeq{0};
    bool g_VoteAnswered = false;          // we've answered the current vote

    void EndVote(int outcome, int player)
    {
        g_Vote.active = false;
        g_VoteOutcome = outcome; g_VoteOutcomePlayer = player; g_VoteOutcomeSeq++;
    }

    bool HaveOffer(int tag) { auto it = g_Offers.find(tag); return it != g_Offers.end() && access(StatePath(it->second).c_str(), F_OK) == 0; }

    bool HaveSnapshotFor(int index)
    {
        auto it = g_MpSlots.find(index);
        if (it == g_MpSlots.end()) return false;
        auto sn = g_Snaps.find(it->second);
        return sn != g_Snaps.end() && sn->second.mp;
    }

    // every device: put the save's game state back at the start of next step, carry on from there
    void StartForwardLoad(int index, CInstance* self)
    {
        int id = g_MpSlots.count(index) ? g_MpSlots[index] : -1;
        auto it = g_Snaps.find(id);
        if (it == g_Snaps.end())
        {
            __android_log_print(ANDROID_LOG_ERROR, "PFOSTATE", "online load %d: no snapshot here", index);
            SetResult(-3);
            return;
        }
        bool nested = g_Restore.active;
        g_SnapLoad = { 1, id, fakesteam::NowMs(), true, nested };
        if (!nested)
        {
            g_Restore.active = true;
            g_Restore.slot = id;
            g_Restore.header.frame = g_Session.m_Frame;
            g_Restore.startMs = g_SnapLoad.startMs;
        }
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online load %d (snapshot %d, saved at frame %u): starting at frame %u",
            index, id, it->second.frame, g_Session.m_Frame);
    }

    uint32 SeatedMask()
    {
        uint32 m = 0;
        for (int p = 0; p < c_PlayerLimit; p++)
        {
            int c = g_Session.m_PlayerIndexToClientIndexMap[p];
            if (c >= 0 && c < g_Session.m_ClientCount) m |= 1u << p;
        }
        return m;
    }

    void OnCommand(int player, int cmd, int param, CInstance* self)
    {
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "online: P%d command %d (%d) at frame %u", player + 1, cmd, param, g_Session.m_Frame);
        if (cmd == c_CmdSave)
        {
            if (param == c_AutoSaveParam)
            {
                // several players may have auto save on: the first one for this pause counts
                if (g_AutoSavedThisPause) return;
                g_AutoSavedThisPause = true;
                g_MpSavePendingAuto = true;
            }
            g_MpSavePending = ++g_MpSaveCount;
            return;
        }
        if (cmd == c_CmdPropose || cmd == c_CmdProposeOther)
        {
            if (g_Vote.active || g_SnapLoad.phase != 0) return;   // one at a time
            g_Vote = Vote();
            g_Vote.active = true; g_Vote.other = cmd == c_CmdProposeOther; g_Vote.index = param; g_Vote.proposer = player;
            g_Vote.accepted = 1u << player; g_Vote.required = SeatedMask(); g_Vote.startFrame = g_Session.m_Frame; g_Vote.startMs = fakesteam::NowMs();
            g_VoteAnswered = player == (HaveSeat() ? g_Session.m_ClientData[g_Session.m_ClientIndex].m_PlayerData.m_PlayerIndex : -9);
        }
        else if (!g_Vote.active || param != g_Vote.index) return;
        else if (cmd == c_CmdAccept) g_Vote.accepted |= 1u << player;
        else if (cmd == c_CmdDecline) { EndVote(g_Vote.proposer == player ? 4 : 2, player); return; }
        else if (cmd == c_CmdCannot) { EndVote(3, player); return; }
        if ((g_Vote.accepted & g_Vote.required) == g_Vote.required)
        {
            int index = g_Vote.index;
            bool other = g_Vote.other;
            EndVote(1, g_Vote.proposer);
            if (other) g_ReseedPending = index;   // next step, before anything else
            else StartForwardLoad(index, self);
        }
    }

    // What the overlay reads, copied out by the game thread: the overlay's thread must never wait
    // for g_NetMutex (the game thread holds it while it waits for other players' inputs).
    std::mutex g_PubMutex;
    int g_PubVote[11] = {};
    std::vector<std::pair<int, int>> g_PubMatchSaves;   // our slot -> this match's save number
    std::atomic<int> g_VoteAnswerRequest{0};             // 1 accept, 2 decline (the overlay's tap)

    void PublishVote()
    {
        int v[11];
        v[0] = g_Vote.active ? 1 : 0; v[1] = g_Vote.index; v[2] = g_Vote.proposer;
        v[3] = static_cast<int>(g_Vote.accepted); v[4] = static_cast<int>(g_Vote.required);
        v[5] = g_Vote.other ? (g_Offers.count(g_Vote.index) ? g_Offers[g_Vote.index] : -1)
             : g_MpFileSlot.count(g_Vote.index) ? g_MpFileSlot[g_Vote.index] : -1;
        v[6] = g_VoteAnswered ? 1 : 0;
        v[7] = g_VoteOutcome; v[8] = g_VoteOutcomePlayer; v[9] = g_VoteOutcomeSeq;
        v[10] = g_Vote.active ? static_cast<int>((fakesteam::NowMs() - g_Vote.startMs) / 1000) : 0;
        std::vector<std::pair<int, int>> ms;
        for (auto& kv : g_MpFileSlot) if (HaveSnapshotFor(kv.first)) ms.push_back({ kv.second, kv.first });
        std::lock_guard<std::mutex> lock(g_PubMutex);
        // an answer the overlay just gave stays answered until the game thread has seen it
        if (g_VoteAnswerRequest.load() != 0 && v[0]) v[6] = 1;
        memcpy(g_PubVote, v, sizeof(v));
        g_PubMatchSaves.swap(ms);
    }

    // ---- loading a save from another match: everyone restarts the match from it ----
    std::atomic<int> g_OfferRefused{0};   // overlay: the save is for this many players (not ours)

    // the files of a slot, whole: the state, its info, its picture
    bool ReadSlotFiles(int slot, std::vector<uint8>& state, std::string& json, std::vector<uint8>& png)
    {
        bool e1 = false, e2 = false, e3 = false;
        std::string path = StatePath(slot), base = path.substr(0, path.size() - 9);
        state = ReadWhole(path.c_str(), e1);
        auto j = ReadWhole((base + ".json").c_str(), e2);
        json.assign(j.begin(), j.end());
        png = ReadWhole((base + ".png").c_str(), e3);
        return e1 && e2 && !state.empty();
    }

    int JsonInt(const std::string& j, const char* key, int dflt)
    {
        std::string k = std::string("\"") + key + "\":";
        size_t at = j.find(k);
        return at == std::string::npos ? dflt : atoi(j.c_str() + at + k.size());
    }

    void OfferOtherSave(int slot)
    {
        std::vector<uint8> state, png;
        std::string json;
        if (!ReadSlotFiles(slot, state, json, png)) { SetResult(-2); return; }
        int clients = JsonInt(json, "clients", 1);
        if (clients != g_Session.m_ClientCount) { g_OfferRefused = clients; SetResult(-5); return; }
        int tag = 1 + static_cast<int>(fakesteam::NowMs() % 254);
        g_Offers[tag] = slot;
        // tag, then each part with its length
        std::vector<uint8> b;
        auto put = [&b](const void* p, uint32 n) { const uint8* q = static_cast<const uint8*>(p); b.insert(b.end(), reinterpret_cast<uint8*>(&n), reinterpret_cast<uint8*>(&n) + 4); b.insert(b.end(), q, q + n); };
        b.push_back(static_cast<uint8>(tag));
        put(json.data(), static_cast<uint32>(json.size()));
        put(png.data(), static_cast<uint32>(png.size()));
        put(state.data(), static_cast<uint32>(state.size()));
        auto& s = g_Session;
        for (int i = 0; i < s.m_ClientCount; i++)
            if (i != s.m_ClientIndex) fakesteam::SendRejoin(s.m_ClientData[i].m_SteamID, fakesteam::Msg::LoadOffer, b);
        g_PendingCmd = c_CmdProposeOther | (tag << 8);
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "offered slot %d (%zu bytes) as %d", slot, b.size(), tag);
    }

    // a save offered by another player: keep it as one of ours
    void TakeOffer(const std::vector<uint8>& b)
    {
        size_t at = 1;
        auto get = [&b, &at](std::vector<uint8>& out) { if (at + 4 > b.size()) return false; uint32 n; memcpy(&n, b.data() + at, 4); at += 4; if (at + n > b.size()) return false; out.assign(b.begin() + at, b.begin() + at + n); at += n; return true; };
        if (b.empty()) return;
        int tag = b[0];
        std::vector<uint8> json, png, state;
        if (!get(json) || !get(png) || !get(state)) { __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "offer %d: damaged", tag); return; }
        int slot = NextSlotId();
        std::string path = StatePath(slot), base = path.substr(0, path.size() - 9);
        std::string j(json.begin(), json.end());
        // it's ours now: fix the slot number it names
        size_t sp = j.find("\"slot\":");
        if (sp != std::string::npos) { size_t e = j.find(',', sp); if (e != std::string::npos) j = j.substr(0, sp) + "\"slot\":" + std::to_string(slot) + j.substr(e); }
        auto write = [](const std::string& p, const void* d, size_t n) { std::string part = p + ".part"; if (FILE* f = fopen(part.c_str(), "wb")) { fwrite(d, 1, n, f); fclose(f); rename(part.c_str(), p.c_str()); } };
        if (!png.empty()) write(base + ".png", png.data(), png.size());
        write(base + ".json", j.data(), j.size());
        write(path, state.data(), state.size());
        g_Offers[tag] = slot;
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "offer %d: kept as slot %d (%zu bytes)", tag, slot, state.size());
    }

    // the overlay's answer to a vote (or withdrawing a proposal of ours)
    void AnswerVote(bool accept)
    {
        if (!g_Vote.active || !HaveSeat()) return;
        g_VoteAnswered = true;
        bool can = g_Vote.other ? HaveOffer(g_Vote.index) : HaveSnapshotFor(g_Vote.index);
        int cmd = accept ? (can ? c_CmdAccept : c_CmdCannot) : c_CmdDecline;
        g_PendingCmd = cmd | (g_Vote.index << 8);
    }

    // after each new frame of a session with others (live or replayed): the seated players' commands
    void ScanCommands(CInstance* self)
    {
        static uint32 scanned = 0;
        static uint32 last[c_PlayerLimit] = {};
        auto& s = g_Session;
        if (s.m_OnlineState != EOnlineState::Online || s.m_ClientCount <= 1)
        {
            scanned = 0; g_MpSaveCount = 0; g_MpSlots.clear(); g_MpFileSlot.clear(); memset(last, 0, sizeof(last));
            g_Vote = Vote(); g_VoteAnswered = false;
            return;
        }
        if (s.m_Frame == scanned) return;
        scanned = s.m_Frame;
        {
            // the game's own pause (the same on every device, and in a replay): a new pause, a new auto save
            RValue v;
            if (!(GlobalGet("paused", v, self) && YYGetBool(&v, 0))) g_AutoSavedThisPause = false;
        }
        for (int p = 0; p < c_PlayerLimit; p++)
        {
            int c = s.m_PlayerIndexToClientIndexMap[p];
            if (c < 0 || c >= s.m_ClientCount) { last[p] = 0; continue; }
            auto& pd = s.m_ClientData[c].m_PlayerData;
            if (pd.m_TailInputFrame < s.m_Frame) continue;
            uint32 cmd = (static_cast<uint32>(pd.m_InputBuffer[s.m_Frame & (countof(pd.m_InputBuffer) - 1)]) >> 16) & 0xffff;
            // an input can be stored for several frames in a row (input delay changes): once is enough
            if (cmd == last[p]) continue;
            last[p] = cmd;
            if ((cmd & 0xff) >= c_CmdSave) OnCommand(p, cmd & 0xff, (cmd >> 8) & 0xff, self);
        }
    }

    // ---- rejoining a match after the app restarted ----
    // While in a match, files/rejoin.txt says who's in it, where they are and which of them we are.
    // An app that comes back with a fresh one (it crashed or was closed) offers to rejoin: it takes
    // its old identity back, asks a player still in the match for the match so far (its replay, up
    // to the frame they're waiting at), plays it flat out like a load, connects again and says
    // "caught up to frame R"; they start sending from R again and the match carries on. The others
    // stay frozen meanwhile (dllmain's input wait). Built for two players: with more, the ones left
    // may have received different amounts of the lost player's last inputs.
    std::string MatchFilePath() { return "/sdcard/Android/data/" PFO_PACKAGE "/files/rejoin.txt"; }
    constexpr int64 c_RejoinOfferSeconds = 100;

    void WriteMatchFile()
    {
        static int64 last = 0;
        int64 now = fakesteam::NowMs();
        if (now - last < 3000) return;
        last = now;
        auto& s = g_Session;
        std::string ids, addr, names;
        for (int i = 0; i < s.m_ClientCount; i++)
        {
            uint64 id = s.m_ClientData[i].m_SteamID;
            ids += (i ? "," : "") + std::to_string(id);
            auto a = fakesteam::g_AddressBook.find(id);
            addr += std::string(i ? "," : "") + (a != fakesteam::g_AddressBook.end() ? std::to_string(a->second.host) + ":" + std::to_string(a->second.port) : "0:0");
            auto n = fakesteam::g_Names.find(id);
            std::string nm = i == s.m_ClientIndex ? fakesteam::g_MyName : n != fakesteam::g_Names.end() ? n->second : "";
            for (auto& ch : nm) if (ch == '|' || ch == '\n') ch = ' ';
            names += (i ? "|" : "") + nm;
        }
        std::string txt = "t=" + std::to_string(time(nullptr)) + "\nme=" + std::to_string(fakesteam::g_MyId) + "\nk=" + std::to_string(s.m_ClientIndex)
            + "\nn=" + std::to_string(s.m_ClientCount) + "\nids=" + ids + "\naddr=" + addr + "\nnames=" + names + "\n";
        // over the okplay relay: its room and our mailbox key, so a relaunched app gets the same mailbox
        {
            std::lock_guard<std::mutex> l(fakesteam::relay::m);
            if (fakesteam::relay::on && !fakesteam::relay::room.empty()) txt += "relay=" + fakesteam::relay::room + "|" + fakesteam::relay::tok + "\n";
        }
        std::string path = MatchFilePath(), part = path + ".part";
        if (FILE* f = fopen(part.c_str(), "w")) { fputs(txt.c_str(), f); fclose(f); rename(part.c_str(), path.c_str()); }
    }

    struct MatchFile { int64 t = 0; uint64 me = 0; int k = -1, n = 0; std::vector<uint64> ids; std::vector<std::pair<uint32, uint16>> addr; std::string names, relayRoom, relayTok; };
    bool ReadMatchFile(MatchFile& m)
    {
        bool exists = false;
        auto v = ReadWhole(MatchFilePath().c_str(), exists);
        if (!exists || v.empty()) return false;
        std::string txt(v.begin(), v.end());
        size_t at = 0;
        while (at < txt.size())
        {
            size_t e = txt.find('\n', at);
            if (e == std::string::npos) e = txt.size();
            std::string line = txt.substr(at, e - at);
            at = e + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq), val = line.substr(eq + 1);
            if (key == "t") m.t = atoll(val.c_str());
            else if (key == "me") m.me = strtoull(val.c_str(), nullptr, 10);
            else if (key == "k") m.k = atoi(val.c_str());
            else if (key == "n") m.n = atoi(val.c_str());
            else if (key == "names") m.names = val;
            else if (key == "relay") { size_t bar = val.find('|'); if (bar != std::string::npos) { m.relayRoom = val.substr(0, bar); m.relayTok = val.substr(bar + 1); } }
            else if (key == "ids" || key == "addr")
            {
                size_t p = 0;
                while (p <= val.size())
                {
                    size_t c = val.find(',', p);
                    if (c == std::string::npos) c = val.size();
                    std::string item = val.substr(p, c - p);
                    if (key == "ids") m.ids.push_back(strtoull(item.c_str(), nullptr, 10));
                    else { size_t col = item.find(':'); m.addr.push_back({ static_cast<uint32>(strtoul(item.substr(0, col).c_str(), nullptr, 10)), static_cast<uint16>(col != std::string::npos ? atoi(item.c_str() + col + 1) : 0) }); }
                    p = c + 1;
                }
            }
        }
        return m.me != 0 && m.k >= 0 && m.n >= 2 && static_cast<int>(m.ids.size()) == m.n && static_cast<int>(m.addr.size()) == m.n;
    }

    struct Rejoin
    {
        int phase = 0;            // 1 asking for the match, 3 replaying it, 4 telling the others we're back
        MatchFile m;
        int source = -1;          // the player we asked
        HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
        bool helloSent = false;
        uint32 frame = 0;         // R: where the others are waiting
        uint32 readySent = 0;     // clients told
        int64 startMs = 0;
        bool reseed = false;      // everyone restarting from a save of another match: no "I'm back"
    } g_Rejoin;
    std::atomic<int> g_RejoinResult{0};   // overlay: 1 rejoined, -1 couldn't

    void RejoinFail(const char* why)
    {
        __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "rejoin: %s", why);
        if (g_Rejoin.conn != k_HSteamNetConnection_Invalid) g_SteamNetworkingSockets->CloseConnection(g_Rejoin.conn, 0, "rejoin failed", false);
        g_Rejoin = Rejoin();
        g_Restore.active = false;
        g_RejoinResult = -1;
        SetResult(-4);
    }

    void StartRejoin(CInstance* self)
    {
        MatchFile m;
        if (g_Session.m_OnlineState != EOnlineState::Offline || g_Rejoin.phase != 0) return;
        if (!ReadMatchFile(m)) { RejoinFail("no match to rejoin"); return; }
        fakesteam::Init();
        fakesteam::g_MyId = m.me;                      // the identity the others know us by
        fakesteam::g_Names[m.me] = fakesteam::g_MyName;
        bool relayed = !m.relayRoom.empty();
        if (relayed)
        {
            { std::lock_guard<std::mutex> l(fakesteam::relay::m); fakesteam::relay::tok = m.relayTok; }
            fakesteam::RelayStart(static_cast<uint32>(strtoul(m.relayRoom.c_str(), nullptr, 16)), 60000);
        }
        int source = -1;
        for (int i = 0; i < m.n; i++)
        {
            if (i == m.k) continue;
            ENetAddress a; a.host = m.addr[i].first; a.port = m.addr[i].second;
            // a stand-in address only meant something to the app that made it: ask the relay anew
            if (relayed && fakesteam::IsVirtual(a)) a = fakesteam::RelayAddressFor(m.ids[i]);
            if (a.port) fakesteam::g_AddressBook[m.ids[i]] = a;
            if (source < 0 && a.port) source = i;
        }
        if (source < 0) { RejoinFail("nobody to ask"); return; }
        SteamNetworkingIdentity id; id.SetSteamID64(m.ids[source]);
        HSteamNetConnection h = g_SteamNetworkingSockets->ConnectP2P(id, 0, 0, nullptr);
        if (h == k_HSteamNetConnection_Invalid) { RejoinFail("can't reach the match"); return; }
        g_Rejoin = Rejoin();
        g_Rejoin.phase = 1; g_Rejoin.m = m; g_Rejoin.source = source; g_Rejoin.conn = h; g_Rejoin.startMs = fakesteam::NowMs();
        g_Restore.active = true;                       // the overlay shows the loading screen
        g_Restore.header.frame = 0;
        g_RejoinResult = 0;
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "rejoin: as client %d of %d, asking client %d", m.k, m.n, source);
    }

    // the rejoiner, each step: ask, then take the match and play it
    void RejoinTick(CInstance* self)
    {
        if (g_Rejoin.phase != 1) return;
        if (fakesteam::NowMs() - g_Rejoin.startMs > 25000) { RejoinFail("no answer"); return; }
        SteamNetConnectionRealTimeStatus_t st;
        if (!g_Rejoin.helloSent && g_SteamNetworkingSockets->GetConnectionRealTimeStatus(g_Rejoin.conn, &st, 0, nullptr) == k_EResultOK
            && st.m_eState == k_ESteamNetworkingConnectionState_Connected)
        {
            std::vector<uint8> b = { static_cast<uint8>(g_Rejoin.m.k) };
            g_Rejoin.helloSent = fakesteam::SendRejoin(g_Rejoin.m.ids[g_Rejoin.source], fakesteam::Msg::RejoinHello, b);
        }
        auto& inbox = fakesteam::g_RejoinInbox;
        for (auto it = inbox.begin(); it != inbox.end(); ++it)
        {
            if (it->type != static_cast<uint8>(fakesteam::Msg::RejoinData)) continue;
            std::vector<uint8> body = std::move(it->body);
            inbox.erase(it);
            fakesteam::In in(body.data(), body.size());
            int k = in.u8();
            uint32 frame = in.u32(), checksum = in.u32(), lastWritten = in.u32();
            if (!in.ok || k != g_Rejoin.m.k || in.o >= body.size()) { RejoinFail("bad match data"); return; }
            g_SteamNetworkingSockets->CloseConnection(g_Rejoin.conn, 0, "replaying", false);
            g_Rejoin.conn = k_HSteamNetConnection_Invalid;
            g_Rejoin.frame = frame;
            // like a load: play the match so far flat out
            g_Restore.data.assign(body.begin() + in.o, body.end());
            g_Restore.header = StateHeader();
            g_Restore.header.frame = frame;
            g_Restore.header.checksum = checksum;
            g_Restore.header.lastFrameWritten = lastWritten;
            g_Restore.header.replaySize = static_cast<uint32>(g_Restore.data.size());
            g_Restore.slot = -1;
            g_Restore.startMs = fakesteam::NowMs();
            PreloadLibrary(self);
            g_ReplayChecksumsChecked = 0; g_ReplayFirstMismatchFrame = 0;
            g_ReplayTouch.clear(); g_ReplayTouchNext = 0; g_TouchFrames.clear();
            g_Session.PlayReplay(g_Restore.data.data(), static_cast<int>(g_Restore.data.size()), k, self);
            SetFastForward(true, self);
            g_Rejoin.phase = 3;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "rejoin: got the match (%zu bytes), replaying %u frames", g_Restore.data.size(), frame);
            return;
        }
    }

    bool RejoinReplayOk(bool match)
    {
        if (g_Rejoin.phase != 3 || match || g_Rejoin.reseed) return true;
        RejoinFail("the match didn't replay the same here");
        return false;
    }

    // network bookkeeping toward a client, as if everything up to frame r had been sent and acknowledged
    void ResyncClient(ClientData& cd, uint32 r, bool theyRestarted)
    {
        if (theyRestarted) cd.m_LastReceivedMessageNumber = 0;   // their message numbers start over
        cd.m_LastAcknowledgedInputFrame = r;
        cd.m_LastSentInputFrame = r;
        cd.m_LastSentFrame = r;
        cd.m_LastReceivedFrame = r;
        cd.m_HasReceivedUnacknowledgedInputFrames = false;
        cd.m_IsRunningTooFarBehind = false;
    }

    // the replay reached R: connect to the others again and carry on from there
    void RejoinAfterReplay(CInstance* self)
    {
        if (g_Rejoin.phase != 3) return;
        auto& s = g_Session;
        auto& m = g_Rejoin.m;
        if (s.m_OnlineState != EOnlineState::Online || s.m_Frame != g_Rejoin.frame) { RejoinFail("the replay didn't land on the frame"); return; }
        for (int i = 0; i < m.n; i++) s.m_ClientData[i].m_SteamID = m.ids[i];
        if (m.k < m.n - 1 && s.m_ListenSocket == k_HSteamListenSocket_Invalid) s.m_ListenSocket = g_SteamNetworkingSockets->CreateListenSocketP2P(0, 0, nullptr);
        for (int i = 0; i < m.n; i++)
        {
            if (i == m.k) continue;
            ResyncClient(s.m_ClientData[i], g_Rejoin.frame, false);
            if (i < m.k)
            {
                SteamNetworkingIdentity id; id.SetSteamID64(m.ids[i]);
                HSteamNetConnection h = g_SteamNetworkingSockets->ConnectP2P(id, 0, 0, nullptr);
                if (h != k_HSteamNetConnection_Invalid) s.AddConnection(s.m_ClientData[i], h);
            }
        }
        if (g_Rejoin.reseed)
        {
            // everyone did the same: the connections come back by themselves (the input wait redials)
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "load from another match: at frame %u in %lld ms, connecting", g_Rejoin.frame,
                static_cast<long long>(fakesteam::NowMs() - g_Rejoin.startMs));
            g_Rejoin = Rejoin();
            return;
        }
        g_Rejoin.phase = 4;
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "rejoin: caught up to frame %u in %lld ms, connecting", g_Rejoin.frame,
            static_cast<long long>(fakesteam::NowMs() - g_Rejoin.startMs));
    }

    // In the input wait (and after each step): a player still in the match answers a rejoiner;
    // a rejoiner tells the others it's caught up.
    // With three or more players, the ones left may have received different amounts of a lost
    // player's last inputs (some packets got through, some didn't), so some could go on a few frames
    // further than others. While someone is lost, everyone frozen sends the others its copy of that
    // player's latest inputs; each fills in what it lacked. They all reach the same frame, and a
    // rejoiner can catch up from any of them.
    void ShareLostInputs()
    {
        static int64 last = 0;
        int64 now = fakesteam::NowMs();
        if (now - last < 500) return;
        last = now;
        auto& s = g_Session;
        if (s.m_ClientCount < 3) return;
        for (int c = 0; c < s.m_ClientCount; c++)
        {
            if (c == s.m_ClientIndex || g_LostAtMs[c] == 0 || s.m_ClientData[c].m_ConnectSocket != k_HSteamNetConnection_Invalid) continue;
            auto& pd = s.m_ClientData[c].m_PlayerData;
            if (pd.m_PlayerIndex < 0 || pd.m_TailInputFrame == 0) continue;
            uint32 tail = pd.m_TailInputFrame;
            uint32 first = tail > 63 ? tail - 63 : 1;
            uint8 count = static_cast<uint8>(tail - first + 1);
            std::vector<uint8> b(6 + 4 * count);
            b[0] = static_cast<uint8>(c);
            memcpy(b.data() + 1, &first, 4);
            b[5] = count;
            for (int i = 0; i < count; i++)
            {
                InputFlags_t in = pd.m_InputBuffer[(first + i) & (countof(pd.m_InputBuffer) - 1)];
                memcpy(b.data() + 6 + 4 * i, &in, 4);
            }
            for (int o = 0; o < s.m_ClientCount; o++)
                if (o != s.m_ClientIndex && o != c && s.m_ClientData[o].m_ConnectSocket != k_HSteamNetConnection_Invalid)
                    fakesteam::SendRejoin(s.m_ClientData[o].m_SteamID, fakesteam::Msg::LostInputs, b);
        }
    }

    void TakeLostInputs(const std::vector<uint8>& b)
    {
        auto& s = g_Session;
        if (b.size() < 6) return;
        int c = b[0];
        uint32 first; memcpy(&first, b.data() + 1, 4);
        int count = b[5];
        if (c < 0 || c >= s.m_ClientCount || c == s.m_ClientIndex || b.size() < 6 + 4u * count || count == 0) return;
        auto& cd = s.m_ClientData[c];
        if (cd.m_ConnectSocket != k_HSteamNetConnection_Invalid) return;   // they're here: theirs is the word
        auto& pd = cd.m_PlayerData;
        uint32 last = first + count - 1;
        if (first > pd.m_TailInputFrame + 1 || last <= pd.m_TailInputFrame) return;   // a gap, or nothing new
        if (last - s.m_Frame >= countof(pd.m_InputBuffer) - 1) return;
        for (uint32 f = pd.m_TailInputFrame + 1; f <= last; f++)
        {
            InputFlags_t in; memcpy(&in, b.data() + 6 + 4 * (f - first), 4);
            pd.m_InputBuffer[f & (countof(pd.m_InputBuffer) - 1)] = in;
        }
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "lost client %d: filled in inputs %u..%u from another player", c, pd.m_TailInputFrame + 1, last);
        pd.m_TailInputFrame = last;
        if (cd.m_LastInputFrame < last) cd.m_LastInputFrame = last;
    }

    void RejoinServe(bool frozen)
    {
        auto& s = g_Session;
        if (s.m_OnlineState != EOnlineState::Online || s.m_ClientCount < 2) return;
        if (frozen) ShareLostInputs();
        if (frozen)
        {
            bool lost = false;
            for (int i = 0; i < s.m_ClientCount; i++) if (g_LostAtMs[i] != 0) lost = true;
            if (lost)
            {
                std::lock_guard<std::mutex> l(fakesteam::relay::m);
                if (fakesteam::relay::on) fakesteam::relay::armedUntil = std::max<int64>(fakesteam::relay::armedUntil, fakesteam::NowMs() + 5000);
            }
        }
        if (g_Rejoin.phase == 4)
        {
            uint32 all = 0;
            for (int i = 0; i < s.m_ClientCount; i++)
            {
                if (i == s.m_ClientIndex) continue;
                all |= 1u << i;
                if ((g_Rejoin.readySent & (1u << i)) || s.m_ClientData[i].m_ConnectSocket == k_HSteamNetConnection_Invalid) continue;
                std::vector<uint8> b(5);
                b[0] = static_cast<uint8>(s.m_ClientIndex);
                memcpy(b.data() + 1, &g_Rejoin.frame, 4);
                if (fakesteam::SendRejoin(s.m_ClientData[i].m_SteamID, fakesteam::Msg::RejoinReady, b)) g_Rejoin.readySent |= 1u << i;
            }
            if ((g_Rejoin.readySent & all) == all)
            {
                __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "rejoin: back in the match at frame %u", g_Rejoin.frame);
                g_Rejoin = Rejoin();
                g_RejoinResult = 1;
            }
        }
        auto& inbox = fakesteam::g_RejoinInbox;
        for (auto it = inbox.begin(); it != inbox.end();)
        {
            int c = -1;
            for (int i = 0; i < s.m_ClientCount; i++) if (s.m_ClientData[i].m_SteamID == it->from) c = i;
            if (it->type == static_cast<uint8>(fakesteam::Msg::RejoinHello))
            {
                if (!frozen) { ++it; continue; }   // answered once we're waiting for them (so R is where we stopped)
                if (c >= 0 && c != s.m_ClientIndex && !it->body.empty() && it->body[0] == c && s.m_ReplayWriter.m_Buffer != nullptr)
                {
                    uint32 r = s.m_Frame - 1;   // in the wait, m_Frame is the frame we can't run yet
                    auto& me = s.m_ClientData[s.m_ClientIndex];
                    uint32 sum = me.m_ChecksumData[r & (countof(me.m_ChecksumData) - 1)].m_Checksum;
                    uint32 lw = s.m_ReplayLastFrameWritten;
                    std::vector<uint8> b(13);
                    b[0] = static_cast<uint8>(c);
                    memcpy(b.data() + 1, &r, 4); memcpy(b.data() + 5, &sum, 4); memcpy(b.data() + 9, &lw, 4);
                    b.insert(b.end(), s.m_ReplayWriter.m_Buffer, s.m_ReplayWriter.m_Buffer + s.m_ReplayWriter.m_Offset);
                    bool ok = fakesteam::SendRejoin(it->from, fakesteam::Msg::RejoinData, b);
                    __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "rejoin: client %d asked for the match: sent %d bytes up to frame %u (%s)", c,
                        s.m_ReplayWriter.m_Offset, r, ok ? "ok" : "FAILED");
                }
                it = inbox.erase(it);
            }
            else if (it->type == static_cast<uint8>(fakesteam::Msg::LostInputs))
            {
                TakeLostInputs(it->body);
                it = inbox.erase(it);
            }
            else if (it->type == static_cast<uint8>(fakesteam::Msg::LoadOffer))
            {
                TakeOffer(it->body);
                it = inbox.erase(it);
            }
            else if (it->type == static_cast<uint8>(fakesteam::Msg::RejoinReady))
            {
                if (c >= 0 && it->body.size() >= 5 && it->body[0] == c)
                {
                    uint32 r; memcpy(&r, it->body.data() + 1, 4);
                    ResyncClient(s.m_ClientData[c], r, true);
                    g_LostAtMs[c] = 0;
                    g_RejoinSeq++;
                    __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "rejoin: client %d is back from frame %u (we're at %u)", c, r, s.m_Frame);
                }
                it = inbox.erase(it);
            }
            else ++it;
        }
    }

    // everyone accepted: play the save's replay to its frame, then connect again and carry on
    void StartReseed(int tag, CInstance* self)
    {
        auto& s = g_Session;
        int slot = g_Offers.count(tag) ? g_Offers[tag] : -1;
        std::vector<uint8> file, png; std::string json;
        if (slot < 0 || !ReadSlotFiles(slot, file, json, png) || file.size() < sizeof(StateHeader)) { __android_log_print(ANDROID_LOG_ERROR, "PFOSTATE", "load from another match: no save %d", tag); SetResult(-3); return; }
        StateHeader h; memcpy(&h, file.data(), sizeof(h));
        if (h.magic != c_StateMagic || h.replayVersion != c_ReplayVersion || sizeof(h) + h.replaySize > file.size()) { SetResult(-2); return; }
        // who we are in this match, kept across the session restart
        MatchFile m;
        m.me = fakesteam::g_MyId; m.k = s.m_ClientIndex; m.n = s.m_ClientCount;
        for (int i = 0; i < s.m_ClientCount; i++)
        {
            m.ids.push_back(s.m_ClientData[i].m_SteamID);
            auto a = fakesteam::g_AddressBook.find(s.m_ClientData[i].m_SteamID);
            m.addr.push_back(a != fakesteam::g_AddressBook.end() ? std::make_pair(a->second.host, a->second.port) : std::make_pair(0u, static_cast<uint16>(0)));
        }
        std::vector<uint32> touches;
        size_t at = sizeof(h) + h.replaySize;
        uint32 tmagic = 0, tcount = 0;
        if (at + 8 <= file.size()) { memcpy(&tmagic, file.data() + at, 4); memcpy(&tcount, file.data() + at + 4, 4); }
        if (tmagic == c_TouchMagic && at + 8 + 4ull * tcount <= file.size()) { touches.resize(tcount); if (tcount) memcpy(touches.data(), file.data() + at + 8, 4ull * tcount); }
        // a new timeline: this match's online saves start over (on every device alike)
        g_MpSaveCount = 0; g_MpSlots.clear(); g_MpFileSlot.clear(); g_Vote = Vote();
        g_Rejoin = Rejoin();
        g_Rejoin.phase = 3; g_Rejoin.m = m; g_Rejoin.frame = h.frame; g_Rejoin.reseed = true; g_Rejoin.startMs = fakesteam::NowMs();
        g_Restore.data.assign(file.begin() + sizeof(h), file.begin() + sizeof(h) + h.replaySize);
        g_Restore.header = h;
        g_Restore.slot = slot;
        g_Restore.active = true;
        g_Restore.startMs = fakesteam::NowMs();
        PreloadLibrary(self);
        g_ReplayChecksumsChecked = 0; g_ReplayFirstMismatchFrame = 0;
        g_ReplayTouch = touches; g_ReplayTouchNext = 0; g_TouchFrames = touches;
        s.PlayReplay(g_Restore.data.data(), static_cast<int>(g_Restore.data.size()), m.k, self);
        SetFastForward(true, self);
        __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "load from another match (slot %d): replaying %u frames, then reconnecting", slot, h.frame);
    }

    void StateBeforeUpdate(CInstance* self)
    {
        BeforeDraw();
        DiffAtFrame(self);
        ReplayTouches(self);
        SnapLoadBefore(self);
        if (g_SnapLoad.phase != 0) return;
        RejoinTick(self);
        if (g_MpSavePending) { int i = g_MpSavePending; bool a = g_MpSavePendingAuto; g_MpSavePending = 0; g_MpSavePendingAuto = false; MpSave(i, self, a); }
        if (int a = g_VoteAnswerRequest.exchange(0)) AnswerVote(a == 1);
        if (g_ReseedPending) { int t = g_ReseedPending; g_ReseedPending = 0; StartReseed(t, self); return; }
        ReadTestCommand();
        int req = g_StateRequest.exchange(0);
        if (req == 1) StartSolo(self);
        else if (req == 2 && InOnlineMatch())
        {
            // online: a SAVE command, so every device saves on the same frame (slot -2: an auto save)
            if (!CanSaveOnline()) SetResult(-1);
            else if (g_StateRequestSlot == -2) g_PendingCmd = c_CmdSave | (c_AutoSaveParam << 8);
            else { if (++g_CmdCounter == c_AutoSaveParam) g_CmdCounter = 1; g_PendingCmd = c_CmdSave | (g_CmdCounter << 8); }
        }
        else if (req == 2) Save(g_StateRequestSlot, self);
        else if (req == 3 && InOnlineMatch())
        {
            // online: propose loading this save of the match; it loads once everyone with a seat accepts
            int index = 0;
            for (auto& kv : g_MpFileSlot) if (kv.second == g_StateRequestSlot) index = kv.first;
            if (!HaveSeat() || g_Vote.active || g_SnapLoad.phase != 0) SetResult(-1);
            else if (index > 0 && HaveSnapshotFor(index)) g_PendingCmd = c_CmdPropose | (index << 8);
            else OfferOtherSave(g_StateRequestSlot);
        }
        else if (req == 3) Load(g_StateRequestSlot, self);
        else if (req == 13) { g_Snaps.erase(g_StateRequestSlot); Load(g_StateRequestSlot, self); }
        else if (req == 16) { g_ForceReplay = true; Load(g_StateRequestSlot, self); }
        else if (req == 17 || req == 18) AnswerVote(req == 17);
        else if (req == 20) StartRejoin(self);
        else if (req == 21) remove(MatchFilePath().c_str());   // "no thanks"
        else if (req == 15) { usleep(550 * 1000); __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "slept 550 ms at frame %u", g_Session.m_Frame); }
        else if (req == 14)
        {
            RValue v; int game = GlobalGet("currGameID", v, self) ? static_cast<int>(YYGetReal(&v, 0)) : -1;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "where: game %d, frame %u, clients %d", game, g_Session.m_Frame, g_Session.m_OnlineState == EOnlineState::Online ? g_Session.m_ClientCount : 0);
        }
        else if (req == 10) SnapTest("capture", self);
        else if (req == 11 || (req == 0 && g_SnapTest == 1)) SnapTest("restore", self);
        else if (req == 12) { int64 t0 = fakesteam::NowMs(); std::string h = Snap("hash", g_StateRequestSlot, self); __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "fingerprint %s (%lld ms)", h.c_str(), static_cast<long long>(fakesteam::NowMs() - t0)); }
        else if (req == 9)
        {
            int fn = Script_Find_Id("pfo_snapshot");
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "probe: script %d", fn);
            if (fn >= 0) { RValue result = {}; RValue args[2]; YYCreateString(&args[0], "probe"); init_real(args[1], 0); Script_Perform(fn, self, self, 2, &result, args); }
        }
    }

    // after pfo_update: has the restore arrived?
    // For the overlay's player chips: "<our client>;<client of seat 0>,...,<seat 7>;<name of client 0>|..."
    // (empty when not in a session with others). Names come from the game (global.onlineClientNames).
    std::mutex g_SeatMutex;
    std::string g_SeatInfo;
    void UpdateSeatInfo(CInstance* self)
    {
        static uint32 lastFrame = 0;
        auto& s = g_Session;
        std::string v;
        if (s.m_OnlineState == EOnlineState::Online && s.m_ClientCount > 1)
        {
            if (s.m_Frame - lastFrame < 30 && s.m_Frame >= lastFrame) return;
            lastFrame = s.m_Frame;
            v = std::to_string(s.m_ClientIndex) + ";";
            for (int p = 0; p < 8; p++) v += (p ? "," : "") + std::to_string(p < countof(s.m_PlayerIndexToClientIndexMap) ? s.m_PlayerIndexToClientIndexMap[p] : -1);
            v += ";" + Snap("seats", 0, self);
        }
        else lastFrame = 0;
        std::lock_guard<std::mutex> lock(g_SeatMutex);
        g_SeatInfo = v;
    }

    // development: files/inputlog makes every frame's input (and the random generator's place) go to
    // files/inputlog_live.txt, or _replay.txt while a replay is read, to compare the two
    void InputLog(CInstance* self)
    {
        static int64 checked = -100000; static bool on = false;
        int64 now = fakesteam::NowMs();
        if (now - checked > 2000) { checked = now; on = access("/sdcard/Android/data/" PFO_PACKAGE "/files/inputlog", F_OK) == 0; }
        // kept in memory and written in batches (writing every frame changes the timing)
        static std::string buf[3];   // live, replay, the snapshot's 120-frame check
        auto flush = [](int k) {
            if (buf[k].empty()) return;
            if (FILE* f = fopen(k == 2 ? "/sdcard/Android/data/" PFO_PACKAGE "/files/inputlog_check.txt" : k ? "/sdcard/Android/data/" PFO_PACKAGE "/files/inputlog_replay.txt" : "/sdcard/Android/data/" PFO_PACKAGE "/files/inputlog_live.txt", "a"))
            { fwrite(buf[k].data(), 1, buf[k].size(), f); fclose(f); }
            buf[k].clear();
        };
        if (!on || g_Session.m_OnlineState != EOnlineState::Online) { flush(0); flush(1); flush(2); return; }
        static uint32 last = 0;
        static int still = 0;   // steps since the frame last moved (the game holding frames)
        if (g_Session.m_Frame == last) { still++; return; }
        last = g_Session.m_Frame;
        int held = still; still = 0;
        bool replaying = g_Session.m_ReplayReader.m_Buffer != nullptr;
        auto& me = g_Session.m_ClientData[g_Session.m_ClientIndex];
        auto& pd = g_Session.m_ClientData[0].m_PlayerData;
        uint32 input = static_cast<uint32>(pd.m_InputBuffer[g_Session.m_Frame & (countof(pd.m_InputBuffer) - 1)]);
        uint32 rng = 0;
        if (RngPointers()) { for (int i = 0; i < 16; i++) rng = rng * 31 + g_RngState[i]; rng ^= *g_RngIndex; }
        auto& cb = me.m_ChecksumData[g_Session.m_Frame & (countof(me.m_ChecksumData) - 1)];
        uint32 sum = cb.m_Checksum;
        // the checksummed bytes themselves (session part, then the GML part) as hex
        std::string raw = "-";
        if (cb.m_Frame == g_Session.m_Frame)
        {
            size_t n = static_cast<size_t>(cb.m_SessionBufferSize) + static_cast<size_t>(cb.m_GMLBufferSize);
            raw = std::to_string(cb.m_SessionBufferSize) + ":";
            char hx[3];
            for (size_t i = 0; i < n && i < 2048; i++)
            {
                snprintf(hx, sizeof(hx), "%02x", g_ChecksumRingBuffer[(cb.m_BufferOffset + i) & (c_ChecksumRingBufferSize - 1)]);
                raw += hx;
            }
        }
        char line[80];
        snprintf(line, sizeof(line), "%u %08x %08x %08x %d ", g_Session.m_Frame, input, rng, sum, held);
        int k = g_SnapLoad.phase != 0 ? 2 : replaying ? 1 : 0;
        // the calls of UFO 50's own random generator since the last frame (PFO_RNG_TRACE builds)
        std::string who = Snap("rngtrace", 0, self);
        buf[k] += line;
        buf[k] += raw;
        buf[k] += ' ';
        buf[k] += who.empty() ? "-" : who;
        buf[k] += "\n";
        if (buf[k].size() > 64000) flush(k);
        if (!replaying && !buf[1].empty()) flush(1);   // a replay ended
        if (k != 2 && !buf[2].empty()) flush(2);       // the check ended
    }

    // the game's pause menu is open (global.paused), for the overlay's "auto save when I pause"
    std::atomic<bool> g_GamePaused{false};

    void StateAfterUpdate(CInstance* self)
    {
        {
            RValue v;
            g_GamePaused = !g_Restore.active && g_Session.m_ReplayReader.m_Buffer == nullptr
                && GlobalGet("paused", v, self) && YYGetBool(&v, 0);
        }
        InputLog(self);
        if (g_SnapLoad.phase == 0) ScanCommands(self);
        PublishVote();
        {
            static bool inMatch = false;
            bool now = InOnlineMatch() && g_Session.m_ReplayReader.m_Buffer == nullptr && g_Rejoin.phase == 0;
            if (now) WriteMatchFile();
            else if (inMatch && g_Session.m_OnlineState != EOnlineState::Online) remove(MatchFilePath().c_str());   // over, and we saw it end
            if (g_Session.m_OnlineState != EOnlineState::Online || now) inMatch = now;
            RejoinServe(false);
        }
        UpdateSeatInfo(self);
        // note when a game starts (also while replaying, so a loaded session knows it too)
        if (g_Session.m_OnlineState == EOnlineState::Online)
        {
            RValue v;
            if (GlobalGet("currGameID", v, self))
            {
                int id = static_cast<int>(YYGetReal(&v, 0));
                if (id != g_TrackedGameID) { g_TrackedGameID = id; g_GameStartFrame = g_Session.m_Frame; }
            }
        }
        else g_TrackedGameID = -1;
        if (g_SnapSettle) { g_SnapSettle = false; g_ChecksumInstanceFix = 0; }   // the step after, they're gone
        if (g_DiffAfterFinal)
        {
            g_DiffAfterFinal = false;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "diff after the final restore, frame %u:", g_Session.m_Frame);
            Snap("diff", 0, self);
            gmi::DumpTo("lists_final.txt");
        }
        if (g_SnapLoad.phase != 0)
        {
            SetDraw(!g_LastAdvanceArg, self);
            SnapLoadAfter(self);
            // a restore next step (gmi::ArrangeSuccessor)
            if ((g_SnapLoad.phase == 1 || g_SnapLoad.phase == 3) && !gmi::ArrangeSuccessor(g_SnapRunner[g_SnapLoad.id], self))
                __android_log_print(ANDROID_LOG_WARN, "PFOSTATE", "load slot %d: can't line up the animation pass", g_SnapLoad.id);
            return;
        }
        if (!g_Restore.active) { SnapCollect(self); return; }
        if (g_Rejoin.phase == 1) return;   // still asking for the match (the session starts when it arrives)
        // While the game holds frames (it does that while a game's textures or music load), let it
        // draw: texture groups only finish loading when something is drawn.
        SetDraw(!g_LastAdvanceArg, self);
        static int64 lastLog = 0;
        int64 now = fakesteam::NowMs();
        if (now - lastLog >= 1000)
        {
            lastLog = now;
            __android_log_print(ANDROID_LOG_INFO, "PFOSTATE", "restoring: frame %u of %u, state %d, advance %d, reader %d/%d, speed %.0f fps",
                g_Session.m_Frame, g_Restore.header.frame, static_cast<int>(g_Session.m_OnlineState), g_LastAdvanceArg ? 1 : 0,
                g_Session.m_ReplayReader.m_Offset, g_Session.m_ReplayReader.m_Size, g_FinalGameSpeedFPS);
        }
        if (g_Session.m_OnlineState != EOnlineState::Online && g_Session.m_OnlineState != EOnlineState::GoingOnline)
        {
            // the replay ended the session (shouldn't happen): give the controls back
            SetFastForward(false, self);
            g_Restore.active = false;
            SetResult(-3);
            return;
        }
        if (g_Session.m_OnlineState == EOnlineState::Online && g_Session.m_Frame >= g_Restore.header.frame) FinishRestore(self);
    }

    // for the overlay: bit 0 in a solo session, bit 1 can save now, bit 2 restoring, bit 3 the game is paused;
    // bits 8-15 restore progress (0-100); bits 16-23 last result (signed); bits 24-30 result count
    int StateStatus()
    {
        int v = 0;
        if (InSoloSession()) v |= 1;
        if (CanSave() || CanSaveOnline()) v |= 2;
        if (g_GamePaused) v |= 8;
        if (g_Restore.active)
        {
            v |= 4;
            uint32 pct;
            if (g_SnapLoad.phase != 0)
                pct = g_SnapLoad.phase == 1 ? 10 : g_SnapLoad.phase == 3 ? 95
                    : 15 + std::min<uint32>(80, (g_Session.m_Frame - std::min(g_Session.m_Frame, g_Restore.header.frame)) * 80 / c_SnapVerifyFrames);
            else pct = std::min<uint32>(100, g_Session.m_Frame * 100 / std::max<uint32>(1, g_Restore.header.frame));
            v |= (pct & 0xff) << 8;
        }
        v |= (g_StateResult.load() & 0xff) << 16;
        v |= (g_StateResultSeq.load() & 0x7f) << 24;
        return v;
    }
}

// pfo_runner_mark() / pfo_runner_rehide(): around reading the game with everything switched on
// (pfo_snapshot.gml), note which instances were off and switch exactly those off again
static void pfo_runner_mark(RValue& r, CInstance*, CInstance*, int, RValue*) { pfo_android::gmi::Mark(); r.kind = VALUE_UNDEFINED; }
static void pfo_runner_rehide(RValue& r, CInstance*, CInstance*, int, RValue*) { init_real(r, pfo_android::gmi::Rehide()); }
static void pfo_runner_settle(RValue& r, CInstance*, CInstance*, int, RValue*) { init_real(r, pfo_android::gmi::Settle()); }

// pfo_android_screen(x, y, w, h, window w, window h): the game reports where it draws its picture each frame
static void pfo_android_screen(RValue& r, CInstance*, CInstance*, int argc, RValue* arg)
{
    r.kind = VALUE_UNDEFINED;
    if (argc < 6) return;
    for (int i = 0; i < 6; i++) pfo_android::g_ScreenRect[i] = static_cast<int>(YYGetReal(arg, i));
    pfo_android::DuringDraw();
}

extern "C" JNIEXPORT jintArray JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeScreenRect(JNIEnv* env, jclass)
{
    jint v[6];
    for (int i = 0; i < 6; i++) v[i] = pfo_android::g_ScreenRect[i];
    jintArray a = env->NewIntArray(6);
    env->SetIntArrayRegion(a, 0, 6, v);
    return a;
}

extern "C" JNIEXPORT void JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeStateRequest(JNIEnv*, jclass, jint what, jint slot)
{
    pfo_android::g_StateRequestSlot = slot;
    pfo_android::g_StateRequest = what;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeSeatInfo(JNIEnv* env, jclass)
{
    std::string v;
    { std::lock_guard<std::mutex> lock(pfo_android::g_SeatMutex); v = pfo_android::g_SeatInfo; }
    return env->NewStringUTF(v.c_str());
}

// online vote for the overlay: active, index, proposer seat, accepted mask, required mask, our slot for
// it, answered by us, outcome, outcome seat, outcome count, seconds since it started
extern "C" JNIEXPORT jintArray JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeVote(JNIEnv* env, jclass)
{
    using namespace pfo_android;
    jint v[11];
    {
        std::lock_guard<std::mutex> lock(g_PubMutex);
        for (int i = 0; i < 11; i++) v[i] = g_PubVote[i];
    }
    jintArray a = env->NewIntArray(11);
    env->SetIntArrayRegion(a, 0, 11, v);
    return a;
}

extern "C" JNIEXPORT void JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeVoteAnswer(JNIEnv*, jclass, jboolean accept)
{
    using namespace pfo_android;
    std::lock_guard<std::mutex> lock(g_PubMutex);
    g_VoteAnswerRequest = accept ? 1 : 2;
    if (g_PubVote[0]) g_PubVote[6] = 1;   // answered, as far as the overlay is concerned
}

// which of this match's online saves a slot holds (0: not one of this match's)
extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeMatchSave(JNIEnv*, jclass, jint slot)
{
    std::lock_guard<std::mutex> lock(pfo_android::g_PubMutex);
    for (auto& kv : pfo_android::g_PubMatchSaves) if (kv.first == slot) return kv.second;
    return 0;
}

// online: the seats we're frozen waiting for (bits), seconds left before giving up, reconnections so far
extern "C" JNIEXPORT jintArray JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeWaiting(JNIEnv* env, jclass)
{
    // nothing to wait for once the match is over (a give-up leaves the loop without clearing it)
    bool online = g_Session.m_OnlineState == EOnlineState::Online;
    jint v[3] = { online ? static_cast<jint>(g_WaitMask.load()) : 0, g_WaitSecondsLeft.load(), g_RejoinSeq.load() };
    jintArray a = env->NewIntArray(3);
    env->SetIntArrayRegion(a, 0, 3, v);
    return a;
}

// a match this app was in and didn't see end (it was closed or crashed): "<seconds ago>|<our seat index>|<names>"
// while it's recent enough to rejoin, else ""; and how the last rejoin went (1 back, -1 couldn't)
extern "C" JNIEXPORT jstring JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeRejoinOffer(JNIEnv* env, jclass)
{
    using namespace pfo_android;
    std::string v;
    MatchFile m;
    if (g_Session.m_OnlineState == EOnlineState::Offline && g_Rejoin.phase == 0 && ReadMatchFile(m))
    {
        int64 ago = static_cast<int64>(time(nullptr)) - m.t;
        if (ago >= 0 && ago <= c_RejoinOfferSeconds) v = std::to_string(ago) + "|" + std::to_string(m.k) + "|" + m.names;
    }
    return env->NewStringUTF(v.c_str());
}

// a save that can't be loaded in this match: the number of players it was made with (0: none)
extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeOfferRefused(JNIEnv*, jclass)
{
    return pfo_android::g_OfferRefused.exchange(0);
}

extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeRejoinResult(JNIEnv*, jclass)
{
    return pfo_android::g_RejoinResult.exchange(0);
}

extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeLastSavedSlot(JNIEnv*, jclass)
{
    return pfo_android::g_LastSavedSlot;
}

extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeStateStatus(JNIEnv*, jclass)
{
    return pfo_android::StateStatus();
}
