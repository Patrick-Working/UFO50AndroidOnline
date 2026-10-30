#define PFO_PACKAGE_PATH "com/unofficial/ufo50"
// Android bridge: lets PFO run as a native library inside the GameMaker Android runner.
//
// On Android the runner sends every extension call through Java, which can only pass
// numbers and strings. Instead, a tiny Java class (PFO_Android) loads libPFO.so at
// activity start, and JNI_OnLoad registers each PFO function as a runner built-in
// (Function_Add), so GML calls reach the native code directly. The YYRunnerInterface
// PFO expects is assembled from libyoyo.so's exported functions, plus small
// replacements for the few members libyoyo doesn't export under the same name.
// The PFO extension entry stays in game.droid (for its options) but has no files.

#include <dlfcn.h>
#include <jni.h>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <string>

namespace pfo_android
{
    YYRunnerInterface g_Interface = {};
    bool g_InterfaceReady = false;
    bool g_ExtensionInitialised = false;
    void* g_Yoyo = nullptr;
    JavaVM* g_JavaVM = nullptr;
    jclass g_PfoClass = nullptr;        // PFO_Android (global ref, looked up while its class loader is active)
    jmethodID g_OkpPost = nullptr;

    // HTTP for the okplay client, through Java (Android has HTTPS built in there). Runs on the
    // okplay worker thread, which attaches itself to the VM once.
    std::string HttpPostJava(const std::string& url, const std::string& body)
    {
        if (!g_JavaVM || !g_PfoClass || !g_OkpPost) return "0\nno java helper";
        JNIEnv* env = nullptr;
        if (g_JavaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
            if (g_JavaVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return "0\nattach failed";
        jstring ju = env->NewStringUTF(url.c_str());
        jstring jb = env->NewStringUTF(body.c_str());
        auto jr = static_cast<jstring>(env->CallStaticObjectMethod(g_PfoClass, g_OkpPost, ju, jb));
        std::string out = "0\njava exception";
        if (env->ExceptionCheck()) env->ExceptionClear();
        else if (jr) { const char* c = env->GetStringUTFChars(jr, nullptr); out = c; env->ReleaseStringUTFChars(jr, c); env->DeleteLocalRef(jr); }
        env->DeleteLocalRef(ju); env->DeleteLocalRef(jb);
        return out;
    }

    template<typename T> T Sym(const char* name) { return g_Yoyo ? reinterpret_cast<T>(dlsym(g_Yoyo, name)) : nullptr; }

    void Log(const char* fmt, va_list args)
    {
        char buf[2048];
        vsnprintf(buf, sizeof(buf), fmt, args);
        __android_log_write(ANDROID_LOG_INFO, "yoyo", buf);
    }
    void DebugOut(const char* fmt, ...) { va_list a; va_start(a, fmt); Log(fmt, a); va_end(a); }
    void ReleaseOut(const char* fmt, ...) { va_list a; va_start(a, fmt); Log(fmt, a); va_end(a); }

    double (*p_REAL_RValue_Ex)(const RValue*) = nullptr;
    int (*p_ArrayGetLength)(RValue*) = nullptr;
    void (*p_JniCreateAsync)(JNIEnv*, jclass, jint, jint) = nullptr;
    void (*p_dsMapAddDouble)(int, const char*, double) = nullptr;
    void (*p_dsMapAddString)(int, const char*, const char*) = nullptr;
    void (*p_dsMapAddInt64)(int, const char*, int64) = nullptr;
    IBuffer* (*p_GetIBuffer)(int) = nullptr;
    TYYBuiltin p_buffer_get_address = nullptr, p_buffer_get_size = nullptr, p_buffer_tell = nullptr, p_buffer_resize = nullptr, p_buffer_set_used_size = nullptr, p_buffer_get_used_size = nullptr;
    std::map<IBuffer*, int> g_BufferIndex;

    double Real(const RValue* v) { return p_REAL_RValue_Ex ? p_REAL_RValue_Ex(v) : v->val; }
    int Kind(const RValue* v) { return v->kind & MASK_KIND_RVALUE; }
    // The runner's JNI entry points cache the JNIEnv they are given, so pass the real one
    // for the calling thread (GML runs on the GL thread, which is attached to the VM).
    void CreateAsync(int map, int ev)
    {
        if (!p_JniCreateAsync || !g_JavaVM) return;
        JNIEnv* env = nullptr;
        if (g_JavaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env)
        {
            if (g_JavaVM->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        }
        jclass cls = env->FindClass("com/yoyogames/runner/RunnerJNILib");
        if (env->ExceptionCheck()) { env->ExceptionClear(); cls = nullptr; }
        p_JniCreateAsync(env, cls, map, ev);
        if (cls) env->DeleteLocalRef(cls);
    }
    bool MapAddDouble(int m, const char* k, double v) { if (!p_dsMapAddDouble) return false; p_dsMapAddDouble(m, k, v); return true; }
    bool MapAddString(int m, const char* k, const char* v) { if (!p_dsMapAddString) return false; p_dsMapAddString(m, k, v); return true; }
    bool MapAddInt64(int m, const char* k, int64 v) { if (!p_dsMapAddInt64) return false; p_dsMapAddInt64(m, k, v); return true; }
    int ArrayLength(RValue* v) { return p_ArrayGetLength ? p_ArrayGetLength(v) : 0; }

    RValue CallBuffer(TYYBuiltin fn, int index, double extra = 0, int argc = 1)
    {
        RValue args[2] = {};
        args[0].kind = VALUE_REAL; args[0].val = index;
        args[1].kind = VALUE_REAL; args[1].val = extra;
        RValue res = {};
        res.kind = VALUE_UNDEFINED;
        if (fn) fn(res, nullptr, nullptr, argc, args);
        return res;
    }
    uint8* BufferAddress(int index)
    {
        RValue r = CallBuffer(p_buffer_get_address, index);
        int k = Kind(&r);
        return (k == VALUE_PTR || k == VALUE_INT64) ? static_cast<uint8*>(r.ptr) : nullptr;
    }
    int BufferSize(int index)
    {
        RValue r = CallBuffer(p_buffer_get_size, index);
        return static_cast<int>(Real(&r));
    }

    bool BufferGetContent(int index, void** data, int* size)
    {
        uint8* src = BufferAddress(index);
        int n = BufferSize(index);
        if (!src || n < 0) return false;
        void* copy = g_Interface.YYAlloc ? g_Interface.YYAlloc(n > 0 ? n : 1) : malloc(n > 0 ? n : 1);
        memcpy(copy, src, n);
        *data = copy;
        *size = n;
        return true;
    }

    int BufferWriteContent(int index, int offset, const void* src, int size, bool grow, bool)
    {
        if (offset < 0 || size < 0) return 0;
        int have = BufferSize(index);
        if (offset + size > have)
        {
            if (!grow) size = have - offset;
            else CallBuffer(p_buffer_resize, index, offset + size, 2);
        }
        uint8* dst = BufferAddress(index);
        if (!dst || size <= 0) return 0;
        memcpy(dst + offset, src, size);
        // memcpy alone leaves GameMaker's "used size" at 0, so buffer_decompress etc. see an empty buffer
        RValue used = CallBuffer(p_buffer_get_used_size, index);
        if (Real(&used) < offset + size) CallBuffer(p_buffer_set_used_size, index, offset + size, 2);
        return size;
    }

    IBuffer* BufferFromGML(int index)
    {
        IBuffer* b = p_GetIBuffer ? p_GetIBuffer(index) : nullptr;
        if (b) g_BufferIndex[b] = index;
        return b;
    }
    int BufferTell(IBuffer* b)
    {
        auto it = g_BufferIndex.find(b);
        if (it == g_BufferIndex.end()) return 0;
        RValue r = CallBuffer(p_buffer_tell, it->second);
        return static_cast<int>(Real(&r));
    }
    unsigned char* BufferData(IBuffer* b)
    {
        auto it = g_BufferIndex.find(b);
        return it == g_BufferIndex.end() ? nullptr : BufferAddress(it->second);
    }

    bool BuildInterface()
    {
        if (g_InterfaceReady) return true;
        g_Yoyo = dlopen("libyoyo.so", RTLD_NOW | RTLD_NOLOAD);
        if (!g_Yoyo) g_Yoyo = dlopen("libyoyo.so", RTLD_NOW);
        if (!g_Yoyo) { __android_log_write(ANDROID_LOG_ERROR, "yoyo", "PFO: libyoyo.so not found"); return false; }

        struct Entry { size_t offset; const char* sym; const char* name; };
        #define RUNNER_SYM(member, symbol) { offsetof(YYRunnerInterface, member), symbol, #member },
        static const Entry entries[] = {
            #include "runner_symbols.inc"
        };
        #undef RUNNER_SYM
        int resolved = 0;
        for (auto& e : entries)
        {
            void* p = e.sym ? dlsym(g_Yoyo, e.sym) : nullptr;
            *reinterpret_cast<void**>(reinterpret_cast<uint8*>(&g_Interface) + e.offset) = p;
            if (p) resolved++;
        }

        p_REAL_RValue_Ex = Sym<decltype(p_REAL_RValue_Ex)>("_Z14REAL_RValue_ExPK6RValue");
        p_ArrayGetLength = Sym<decltype(p_ArrayGetLength)>("_Z14ArrayGetLengthP6RValue");
        p_JniCreateAsync = Sym<decltype(p_JniCreateAsync)>("Java_com_yoyogames_runner_RunnerJNILib_CreateAsynEventWithDSMap");
        p_dsMapAddDouble = Sym<decltype(p_dsMapAddDouble)>("dsMapAddDouble");
        p_dsMapAddString = Sym<decltype(p_dsMapAddString)>("dsMapAddString");
        p_dsMapAddInt64 = Sym<decltype(p_dsMapAddInt64)>("dsMapAddInt64");
        p_GetIBuffer = Sym<decltype(p_GetIBuffer)>("_Z10GetIBufferi");

        g_Interface.DebugConsoleOutput = &DebugOut;
        g_Interface.ReleaseConsoleOutput = &ReleaseOut;
        g_Interface.REAL_RValue = &Real;
        g_Interface.KIND_RValue = &Kind;
        g_Interface.CreateAsyncEventWithDSMap = &CreateAsync;
        g_Interface.DsMapAddDouble = &MapAddDouble;
        g_Interface.DsMapAddString = &MapAddString;
        g_Interface.DsMapAddInt64 = &MapAddInt64;
        g_Interface.YYArrayGetLength = &ArrayLength;
        g_Interface.BufferGetContent = &BufferGetContent;
        g_Interface.BufferWriteContent = &BufferWriteContent;
        g_Interface.BufferGetFromGML = &BufferFromGML;
        g_Interface.BufferTELL = &BufferTell;
        g_Interface.BufferGet = &BufferData;

        char msg[160];
        snprintf(msg, sizeof(msg), "PFO: runner interface %d/%d from libyoyo, async=%p dsmap=%p", resolved, static_cast<int>(sizeof(entries) / sizeof(entries[0])), (void*)p_JniCreateAsync, (void*)p_dsMapAddDouble);
        __android_log_write(ANDROID_LOG_INFO, "yoyo", msg);
        g_InterfaceReady = true;
        return true;
    }

    // Runs on the first PFO call from GML (the game data is loaded by then).
    void EnsureExtensionInitialised();
}

#include "selftest.inc"

namespace pfo_android
{
    // Optional input probe (tests): when Android/data/<pkg>/files/inputprobe exists, log every change
    // in which player keys / gamepad buttons the engine sees as held (logcat -s PFOPROBE).
    void InputProbe()
    {
        static int enabled = -1;
        static bool (*keyDown)(int) = nullptr;
        static TYYBuiltin padCheck = nullptr;
        static uint64 last = 0;
        static int64 lastMs = 0;
        if (enabled < 0)
        {
            FILE* f = fopen("/sdcard/Android/data/" PFO_PACKAGE "/files/inputprobe", "r");
            enabled = f != nullptr;
            if (f) fclose(f);
            keyDown = Sym<bool (*)(int)>("_Z11IO_Key_Downi");
            padCheck = Sym<TYYBuiltin>("_Z20F_GamepadButtonCheckR6RValueP9CInstanceS2_iPS_");
        }
        if (!enabled || !keyDown) return;
        int64 now = fakesteam::NowMs();
        if (now - lastMs < 10) return;
        lastMs = now;
        static const int vks[] = { 38, 40, 37, 39, 90, 88, 68, 27,          // P1: arrows, Z, X, D, Esc
                                   101, 98, 97, 99, 46, 35, 33 };            // P2: numpad 5/2/1/3, Del, End, PgUp
        static const int pads[] = { 32781, 32782, 32783, 32784, 32769, 32771, 32778 };  // padu/d/l/r, face1, face3, start
        uint64 state = 0; int bit = 0;
        for (int vk : vks) { if (keyDown(vk)) state |= 1ull << bit; bit++; }
        for (int slot = 0; slot < 4 && padCheck; slot++)
            for (int b : pads)
            {
                RValue args[2] = {}; args[0].kind = VALUE_REAL; args[0].val = slot; args[1].kind = VALUE_REAL; args[1].val = b;
                RValue r = {}; padCheck(r, nullptr, nullptr, 2, args);
                if (Real(&r) > 0.5) state |= 1ull << bit;
                bit++;
            }
        if (state == last) return;
        last = state;
        char buf[512]; int o = 0;
        bit = 0;
        for (int vk : vks) { if (state >> bit & 1) o += snprintf(buf + o, sizeof(buf) - o, "vk%d ", vk); bit++; }
        for (int slot = 0; slot < 4; slot++) for (int b : pads) { if (state >> bit & 1) o += snprintf(buf + o, sizeof(buf) - o, "pad%d:%d ", slot, b); bit++; }
        __android_log_print(ANDROID_LOG_INFO, "PFOPROBE", "held: %s", o ? buf : "(none)");
    }
}

namespace pfo_android
{
    // Which virtual touch pad (1-4) GameMaker put in each gamepad slot; the overlay asks so player N
    // always drives slot N (GameMaker hands out slots in its own order). Refreshed on the game thread.
    volatile int g_SlotPad[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

    // Real (non-touch) pads, for the overlay's players panel: name and buttons held per slot.
    std::mutex g_PadMutex;
    std::string g_SlotName[8];
    bool g_SlotReal[8] = {};
    uint32 g_SlotButtons[8] = {};

    // Who reads which slot, set by the overlay (nativeSetPlayerSlots): >= 0 a real pad's slot,
    // -3 = the touch pad for that player (virtual pad "PFO Touch Pad <p+1>"), -1 = no gamepad.
    volatile bool g_BindManaged = false;
    volatile int g_PlayerSlot[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    volatile int g_OpenPanel = -1;   // set by the pause menu's pad-pairing item: player index

    void RefreshPadSlots()
    {
        static TYYBuiltin getDesc = nullptr, connected = nullptr, check = nullptr;
        static int64 lastMs = 0, lastButtonsMs = 0;
        int64 now = fakesteam::NowMs();
        if (!getDesc)
        {
            getDesc = Sym<TYYBuiltin>("_Z23F_GamepadGetDescriptionR6RValueP9CInstanceS2_iPS_");
            connected = Sym<TYYBuiltin>("_Z18F_GamepadConnectedR6RValueP9CInstanceS2_iPS_");
            check = Sym<TYYBuiltin>("_Z20F_GamepadButtonCheckR6RValueP9CInstanceS2_iPS_");
        }
        if (!getDesc || !connected || !g_Interface.YYGetString) return;
        if (now - lastMs >= 500)
        {
            lastMs = now;
            auto getString = g_Interface.YYGetString;
            for (int slot = 0; slot < 8; slot++)
            {
                RValue arg = {}; arg.kind = VALUE_REAL; arg.val = slot;
                RValue c = {}; connected(c, nullptr, nullptr, 1, &arg);
                bool on = Real(&c) > 0.5;
                RValue r = {}; if (on) getDesc(r, nullptr, nullptr, 1, &arg);
                const char* d = on && Kind(&r) == VALUE_STRING ? getString(&r, 0) : nullptr;
                int pad = -1;
                if (d && strncmp(d, "PFO Touch Pad ", 14) == 0) pad = atoi(d + 14);
                g_SlotPad[slot] = pad;
                std::lock_guard<std::mutex> lock(g_PadMutex);
                g_SlotReal[slot] = on && pad < 0;
                g_SlotName[slot] = d && pad < 0 ? d : "";
                if (!g_SlotReal[slot]) g_SlotButtons[slot] = 0;
            }
        }
        // buttons held on real pads, so the panel can light up "the pad you're pressing"
        if (check && now - lastButtonsMs >= 30)
        {
            lastButtonsMs = now;
            for (int slot = 0; slot < 8; slot++)
            {
                if (!g_SlotReal[slot]) continue;
                uint32 mask = 0;
                for (int b = 0; b < 16; b++)
                {
                    RValue args[2] = {}; args[0].kind = VALUE_REAL; args[0].val = slot; args[1].kind = VALUE_REAL; args[1].val = 32769 + b;
                    RValue r = {}; check(r, nullptr, nullptr, 2, args);
                    if (Real(&r) > 0.5) mask |= 1u << b;
                }
                std::lock_guard<std::mutex> lock(g_PadMutex);
                g_SlotButtons[slot] = mask;
            }
        }
    }

    int TouchPadSlot(int player)
    {
        for (int slot = 0; slot < 8; slot++) if (g_SlotPad[slot] == player + 1) return slot;
        return -1;
    }
}

extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativePadForSlot(JNIEnv*, jclass, jint slot)
{
    return slot >= 0 && slot < 8 ? pfo_android::g_SlotPad[slot] : -1;
}

// Private lobbies: our lobby's invite code ("" if public / not hosting), INVITE FRIENDS presses,
// and codes the player typed in (players panel > JOIN CODE).
extern "C" JNIEXPORT jstring JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeLobbyCode(JNIEnv* env, jclass)
{
    std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
    return env->NewStringUTF(fakesteam::g_ShareCode.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeTakeInviteRequest(JNIEnv*, jclass)
{
    bool r = fakesteam::g_InviteRequested;
    fakesteam::g_InviteRequested = false;
    return r;
}

extern "C" JNIEXPORT void JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeAddJoinCode(JNIEnv* env, jclass, jstring code)
{
    const char* c = code ? env->GetStringUTFChars(code, nullptr) : nullptr;
    std::string k;
    for (const char* q = c; q && *q && k.size() < 12; q++) if (isalnum(static_cast<unsigned char>(*q))) k += static_cast<char>(toupper(*q));
    if (c) env->ReleaseStringUTFChars(code, c);
    k = fakesteam::NormalizeCode(k);
    if (k.empty()) return;
    std::string tag = fakesteam::CodeTag(k);
    std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
    auto& v = fakesteam::g_JoinTags;
    auto& cs = fakesteam::g_JoinCodes;
    for (size_t i = 0; i < v.size(); i++) if (v[i] == tag) { v.erase(v.begin() + i); cs.erase(cs.begin() + i); break; }
    v.push_back(tag);
    cs.push_back(k);
    if (v.size() > 8) { v.erase(v.begin()); cs.erase(cs.begin()); }
}

// Online status for the overlay: bits 0-3 how we're connected (0 none, 1 LAN, 2 DIRECT, 3 RELAY),
// bit 4 in a lobby, bit 5 hosting it, bit 6 in an online game. The overlay runs a foreground
// service while bits 4/6 are set, so Android doesn't freeze the app in the background.
extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeNetStatus(JNIEnv*, jclass)
{
    int v = fakesteam::g_ConnKind & 15;
    if (fakesteam::g_InLobby) v |= 16;
    if (fakesteam::g_InLobby && fakesteam::g_IsOwner) v |= 32;
    if (g_Session.m_OnlineState >= EOnlineState::Online) v |= 64;
    return v;
}

extern "C" JNIEXPORT void JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeSetUpnp(JNIEnv*, jclass, jboolean on)
{
    fakesteam::upnp::enabled = on;
}

// Player name from the overlay (empty = device default); applied when not in a lobby.
extern "C" JNIEXPORT void JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeSetPlayerName(JNIEnv* env, jclass, jstring name)
{
    const char* c = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
    std::string n;
    for (const char* q = c; q && *q && n.size() < 16; q++) if (*q >= 32 && *q < 127) n += *q;
    if (c) env->ReleaseStringUTFChars(name, c);
    std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
    fakesteam::g_PendingName = n;
    fakesteam::g_NamePending = true;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeGetPlayerName(JNIEnv* env, jclass)
{
    std::string n;
    {
        std::lock_guard<std::mutex> lock(fakesteam::g_NameMutex);
        n = fakesteam::g_NamePending && !fakesteam::g_PendingName.empty() ? fakesteam::g_PendingName : fakesteam::g_NameForUi;
    }
    return env->NewStringUTF(n.c_str());
}

// Real pads for the players panel: one line per pad, "slot<TAB>buttons held<TAB>name".
extern "C" JNIEXPORT jstring JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativePadState(JNIEnv* env, jclass)
{
    using namespace pfo_android;
    std::string out;
    {
        std::lock_guard<std::mutex> lock(g_PadMutex);
        for (int slot = 0; slot < 8; slot++)
        {
            if (!g_SlotReal[slot]) continue;
            char head[32];
            snprintf(head, sizeof(head), "%d\t%u\t", slot, g_SlotButtons[slot]);
            out += head;
            for (char ch : g_SlotName[slot]) out += (ch == '\t' || ch == '\n') ? ' ' : ch;
            out += '\n';
        }
    }
    return env->NewStringUTF(out.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeSetPlayerSlots(JNIEnv* env, jclass, jintArray slots)
{
    using namespace pfo_android;
    jint n = env->GetArrayLength(slots);
    jint* v = env->GetIntArrayElements(slots, nullptr);
    for (int p = 0; p < 8; p++) g_PlayerSlot[p] = p < n ? v[p] : -1;
    env->ReleaseIntArrayElements(slots, v, JNI_ABORT);
    g_BindManaged = true;
}

extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeTakeOpenPanel(JNIEnv*, jclass)
{
    int p = pfo_android::g_OpenPanel;
    pfo_android::g_OpenPanel = -1;
    return p;
}

// pfo_android_joyslot(player, game_slot): the gamepad slot this player reads. Patched into the
// game's input scripts, which write the result back into global.joySlot, so the game's own menus
// show the same pairing. Until the overlay has sent its bindings, the game's choice stands.
static void pfo_android_joyslot(RValue& r, CInstance*, CInstance*, int argc, RValue* arg)
{
    using namespace pfo_android;
    int p = argc > 0 ? static_cast<int>(Real(&arg[0])) : -1;
    double gameSlot = argc > 1 ? Real(&arg[1]) : -1;
    double slot = gameSlot;
    if (g_BindManaged && p >= 0 && p < 8)
    {
        int w = g_PlayerSlot[p];
        slot = w == -3 ? TouchPadSlot(p) : w;
    }
    static double s_last[8] = { -9, -9, -9, -9, -9, -9, -9, -9 };
    if (p >= 0 && p < 8 && s_last[p] != slot)
    {
        s_last[p] = slot;
        __android_log_print(ANDROID_LOG_INFO, "PFOBIND", "player %d reads gamepad slot %d (game had %d)", p, static_cast<int>(slot), static_cast<int>(gameSlot));
    }
    r.kind = VALUE_REAL;
    r.val = slot;
}

// pfo_android_keys(player, up, down, left, right, fire1, fire2, start): the keyboard keys (vk codes)
// the game has bound for a player, reported from scrGetInput. Touch sends these keys.
namespace pfo_android { volatile int g_Keys[8][7] = {}; volatile bool g_KeysKnown[8] = {}; }
static void pfo_android_keys(RValue& r, CInstance*, CInstance*, int argc, RValue* arg)
{
    using namespace pfo_android;
    r.kind = VALUE_UNDEFINED;
    int p = argc > 0 ? static_cast<int>(Real(&arg[0])) : -1;
    if (p < 0 || p >= 8 || argc < 8) return;
    for (int i = 0; i < 7; i++) g_Keys[p][i] = static_cast<int>(Real(&arg[1 + i]));
    g_KeysKnown[p] = true;
}

extern "C" JNIEXPORT jintArray JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeKeys(JNIEnv* env, jclass, jint p)
{
    using namespace pfo_android;
    if (p < 0 || p >= 8 || !g_KeysKnown[p]) return nullptr;
    jint v[7];
    for (int i = 0; i < 7; i++) v[i] = g_Keys[p][i];
    jintArray a = env->NewIntArray(7);
    env->SetIntArrayRegion(a, 0, 7, v);
    return a;
}

// pfo_android_quit(): the main menu's Quit. GameMaker's game_end() doesn't close the app on
// Android, so ask Java to finish the activity and end the process (stops the network threads too).
static void pfo_android_quit(RValue& r, CInstance*, CInstance*, int, RValue*)
{
    using namespace pfo_android;
    r.kind = VALUE_UNDEFINED;
    {
        std::lock_guard<std::recursive_mutex> netLock(fakesteam::g_NetMutex);
        fakesteam::LeaveLobby();
        fakesteam::UpnpUnmapAsync();
    }
    JNIEnv* env = nullptr;
    if (!g_JavaVM || !g_PfoClass || g_JavaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) return;
    jmethodID m = env->GetStaticMethodID(g_PfoClass, "quitApp", "()V");
    if (m) env->CallStaticVoidMethod(g_PfoClass, m);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// pfo_android_ask_code(): JOIN BY CODE in the game's lobby list opens the overlay's code box.
static void pfo_android_ask_code(RValue& r, CInstance*, CInstance*, int, RValue*)
{
    fakesteam::g_AskCodeRequested = true;
    r.kind = VALUE_UNDEFINED;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeTakeAskCode(JNIEnv*, jclass)
{
    bool r = fakesteam::g_AskCodeRequested;
    fakesteam::g_AskCodeRequested = false;
    return r;
}

// pfo_android_open_players(player): the pause menu's pad-pairing item opens the overlay's panel.
static void pfo_android_open_players(RValue& r, CInstance*, CInstance*, int argc, RValue* arg)
{
    pfo_android::g_OpenPanel = argc > 0 ? static_cast<int>(pfo_android::Real(&arg[0])) : 0;
    r.kind = VALUE_UNDEFINED;
}

// The overlay asks this to lock its player switch online: -1 = not online, -2 = online without a
// seat (spectating), else the player index this device plays. Online, the game always reads the
// device's P1 controls for that seat; every other seat comes from the network.
extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeOnlineSeat(JNIEnv*, jclass)
{
    if (g_Session.m_OnlineState < EOnlineState::Online) return -1;
    int c = g_Session.m_ClientIndex;
    if (c < 0 || c >= static_cast<int>(countof(g_Session.m_ClientData))) return -2;
    int seat = g_Session.m_ClientData[c].m_PlayerData.m_PlayerIndex;
    return seat >= 0 ? seat : -2;
}

// Hot seat: turn-based games call pfo_android_turn(player) each frame with the player whose
// controls they are reading (patched in by scripts/android_patch.csx). The overlay polls
// nativeHotSeatTurn() and follows along; when the calls stop (left the game) it reports -1.
namespace pfo_android
{
    volatile int g_TurnPlayer = -1;
    volatile int g_TurnCount = 0;   // players in the game (global.numPlayers), 0 = unknown
    volatile int64 g_TurnMs = 0;
}

static void pfo_android_turn(RValue& r, CInstance*, CInstance*, int argc, RValue* arg)
{
    r.kind = VALUE_UNDEFINED;
    if (argc < 1) return;
    int k = pfo_android::Kind(&arg[0]);
    int p = k == VALUE_REAL ? static_cast<int>(arg[0].val) : k == VALUE_INT32 ? arg[0].v32 : k == VALUE_INT64 ? static_cast<int>(arg[0].v64) : -1;
    pfo_android::g_TurnPlayer = p;
    pfo_android::g_TurnCount = argc > 1 ? static_cast<int>(pfo_android::Real(&arg[1])) : 0;
    pfo_android::g_TurnMs = fakesteam::NowMs();
}

extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeHotSeatCount(JNIEnv*, jclass)
{
    return pfo_android::g_TurnCount;
}

extern "C" JNIEXPORT jint JNICALL Java_com_unofficial_ufo50_PFO_1Android_nativeHotSeatTurn(JNIEnv*, jclass)
{
    if (g_Session.m_OnlineState >= EOnlineState::Online) return -1;
    if (fakesteam::NowMs() - pfo_android::g_TurnMs > 700) return -1;
    return pfo_android::g_TurnPlayer;
}

#define PFO_BUILTIN(name) \
    static void pfo_builtin_##name(RValue& r, CInstance* s, CInstance* o, int argc, RValue* arg) {         std::lock_guard<std::recursive_mutex> netLock(fakesteam::g_NetMutex);   /* the background pump waits while the game runs */         fakesteam::g_LastGameCallMs = fakesteam::NowMs();         pfo_android::EnsureExtensionInitialised(); pfo_android::InputProbe(); pfo_android::RefreshPadSlots(); name(r, s, o, argc, arg); }
#include "builtins.inc"
#undef PFO_BUILTIN

void pfo_android::EnsureExtensionInitialised()
{
    if (g_ExtensionInitialised) return;
    g_ExtensionInitialised = true;
    if (!BuildInterface()) return;
    p_buffer_get_address = Sym<TYYBuiltin>("_Z19F_BUFFER_GetAddressR6RValueP9CInstanceS2_iPS_");
    p_buffer_get_size = Sym<TYYBuiltin>("_Z17F_BUFFER_Get_SizeR6RValueP9CInstanceS2_iPS_");
    p_buffer_tell = Sym<TYYBuiltin>("_Z13F_BUFFER_TellR6RValueP9CInstanceS2_iPS_");
    p_buffer_resize = Sym<TYYBuiltin>("_Z15F_BUFFER_ResizeR6RValueP9CInstanceS2_iPS_");
    p_buffer_set_used_size = Sym<TYYBuiltin>("_Z20F_BUFFER_SetUsedSizeR6RValueP9CInstanceS2_iPS_");
    p_buffer_get_used_size = Sym<TYYBuiltin>("_Z20F_BUFFER_GetUsedSizeR6RValueP9CInstanceS2_iPS_");
    RunSelfTest();
    YYExtensionInitialise(&g_Interface, sizeof(g_Interface));
    RValue result = {};
    pfo_init(result, nullptr, nullptr, 0, nullptr);
    __android_log_write(ANDROID_LOG_INFO, "yoyo", "PFO: extension initialised");
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*)
{
    using namespace pfo_android;
    g_JavaVM = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env)
    {
        jclass c = env->FindClass(PFO_PACKAGE_PATH "/PFO_Android");
        if (c) { g_PfoClass = static_cast<jclass>(env->NewGlobalRef(c)); g_OkpPost = env->GetStaticMethodID(c, "okpPost", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"); }
        if (env->ExceptionCheck()) env->ExceptionClear();
        fakesteam::g_HttpPost = &HttpPostJava;
        __android_log_print(ANDROID_LOG_INFO, "yoyo", "PFO: java http helper %s", g_OkpPost ? "ready" : "missing");
    }
    if (!BuildInterface()) return JNI_VERSION_1_6;
    auto functionAdd = Sym<void (*)(const char*, TYYBuiltin, int, bool)>("_Z12Function_AddPKcPFvR6RValueP9CInstanceS4_iPS1_Eib");
    if (!functionAdd)
    {
        __android_log_write(ANDROID_LOG_ERROR, "yoyo", "PFO: Function_Add not found");
        return JNI_VERSION_1_6;
    }
    int count = 0;
    #define PFO_BUILTIN(name) functionAdd(#name, &pfo_builtin_##name, -1, false); count++;
    #include "builtins.inc"
    #undef PFO_BUILTIN
    functionAdd("pfo_android_turn", &pfo_android_turn, -1, false); count++;
    functionAdd("pfo_android_joyslot", &pfo_android_joyslot, 2, false); count++;
    functionAdd("pfo_android_open_players", &pfo_android_open_players, 1, false); count++;
    functionAdd("pfo_android_keys", &pfo_android_keys, 8, false); count++;
    functionAdd("pfo_android_ask_code", &pfo_android_ask_code, 0, false); count++;
    functionAdd("pfo_android_quit", &pfo_android_quit, 0, false); count++;
    char msg[96];
    snprintf(msg, sizeof(msg), "PFO: registered %d built-in functions", count);
    __android_log_write(ANDROID_LOG_INFO, "yoyo", msg);
    return JNI_VERSION_1_6;
}
