// Startup self-test for the runner-interface functions this bridge implements itself
// (the ones libyoyo.so doesn't export under the names PFO expects). Each test checks the
// replacement against GameMaker's own built-ins, so a semantic mismatch (like a buffer
// write that doesn't update the used size) shows up in logcat on the first launch:
//   adb logcat -s PFOTEST
namespace pfo_android
{
    struct SelfTest
    {
        int passed = 0, failed = 0;
        void check(bool ok, const char* name, const char* detail = "")
        {
            if (ok) { passed++; __android_log_print(ANDROID_LOG_INFO, "PFOTEST", "PASS %s", name); }
            else { failed++; __android_log_print(ANDROID_LOG_ERROR, "PFOTEST", "FAIL %s %s", name, detail); }
        }
    };

    TYYBuiltin B(const char* sym) { return Sym<TYYBuiltin>(sym); }

    RValue Call(TYYBuiltin fn, std::initializer_list<RValue> args)
    {
        RValue a[8] = {};
        int n = 0;
        for (auto& v : args) a[n++] = v;
        RValue r = {};
        r.kind = VALUE_UNDEFINED;
        if (fn) fn(r, nullptr, nullptr, n, a);
        return r;
    }
    RValue Num(double v) { RValue r = {}; r.kind = VALUE_REAL; r.val = v; return r; }
    RValue Str(const char* s) { RValue r = {}; if (g_Interface.YYCreateString) g_Interface.YYCreateString(&r, s); return r; }

    void RunSelfTest()
    {
        SelfTest t;
        char detail[160];

        // REAL / KIND
        RValue rv = Num(2.5);
        t.check(Real(&rv) == 2.5 && Kind(&rv) == VALUE_REAL, "REAL_RValue/KIND_RValue");

        // Buffers: write -> used size -> read back -> compress/decompress round trip
        auto fCompress = B("_Z17F_BUFFER_CompressR6RValueP9CInstanceS2_iPS_");
        auto fDecompress = B("_Z19F_BUFFER_DecompressR6RValueP9CInstanceS2_iPS_");
        auto fDelete = B("_Z15F_BUFFER_DeleteR6RValueP9CInstanceS2_iPS_");
        int idx = g_Interface.CreateBuffer ? g_Interface.CreateBuffer(64, eBuffer_Format_Fixed, 1) : -1;
        t.check(idx >= 0, "CreateBuffer");
        if (idx >= 0)
        {
            uint8 data[48];
            for (int i = 0; i < 48; i++) data[i] = static_cast<uint8>(i * 7 + 3);
            int written = BufferWriteContent(idx, 0, data, 48, false, false);
            t.check(written == 48, "BufferWriteContent returns size");
            RValue used = CallBuffer(p_buffer_get_used_size, idx);
            snprintf(detail, sizeof(detail), "used=%g", Real(&used));
            t.check(Real(&used) >= 48, "BufferWriteContent sets used size", detail);
            uint8* addr = BufferAddress(idx);
            t.check(addr && memcmp(addr, data, 48) == 0, "BufferWriteContent bytes land in the buffer");

            void* copy = nullptr; int copySize = 0;
            bool ok = BufferGetContent(idx, &copy, &copySize);
            t.check(ok && copy && copySize >= 48 && memcmp(copy, data, 48) == 0, "BufferGetContent copies the data");
            if (copy && g_Interface.YYFree) g_Interface.YYFree(copy);

            IBuffer* ib = BufferFromGML(idx);
            t.check(ib != nullptr && BufferData(ib) == addr, "BufferGetFromGML/BufferGet");
            t.check(ib != nullptr && BufferTell(ib) == 0, "BufferTELL");

            RValue c = Call(fCompress, { Num(idx), Num(0), Num(48) });
            RValue cs = Call(p_buffer_get_size, { c });
            RValue d = Call(fDecompress, { c, Num(0), cs });
            RValue ds = Call(p_buffer_get_size, { d });
            RValue da = Call(p_buffer_get_address, { d });
            bool round = Real(&ds) == 48 && (Kind(&da) == VALUE_PTR) && da.ptr && memcmp(da.ptr, data, 48) == 0;
            snprintf(detail, sizeof(detail), "compressed=%g decompressed=%g", Real(&cs), Real(&ds));
            t.check(round, "buffer compress/decompress round trip", detail);
            Call(fDelete, { d }); Call(fDelete, { c }); Call(fDelete, { Num(idx) });
        }

        // ds_map helpers
        auto fFind = B("_Z16F_DsMapFindValueR6RValueP9CInstanceS2_iPS_");
        auto fSize = B("_Z11F_DsMapSizeR6RValueP9CInstanceS2_iPS_");
        auto fDestroy = B("_Z14F_DsMapDestroyR6RValueP9CInstanceS2_iPS_");
        auto createDsMap = g_Interface.CreateDsMap;  // (CreateDsMap is a macro in Extension_Interface.h)
        int map = createDsMap ? createDsMap(0, 0) : -1;
        t.check(map >= 0, "CreateDsMap");
        if (map >= 0)
        {
            t.check(MapAddDouble(map, "d", 1.5), "DsMapAddDouble");
            t.check(MapAddString(map, "s", "abc"), "DsMapAddString");
            t.check(MapAddInt64(map, "i", 1234567890123LL), "DsMapAddInt64");
            RValue sz = Call(fSize, { Num(map) });
            snprintf(detail, sizeof(detail), "size=%g", Real(&sz));
            t.check(Real(&sz) == 3, "ds_map has 3 entries", detail);
            RValue vd = Call(fFind, { Num(map), Str("d") });
            t.check(Real(&vd) == 1.5, "ds_map double reads back");
            RValue vs = Call(fFind, { Num(map), Str("s") });
            auto getString = g_Interface.YYGetString;
            const char* sv = getString ? getString(&vs, 0) : nullptr;
            t.check(sv && strcmp(sv, "abc") == 0, "ds_map string reads back");
            RValue vi = Call(fFind, { Num(map), Str("i") });
            auto getInt64 = g_Interface.YYGetInt64;
            int64 iv = getInt64 ? getInt64(&vi, 0) : 0;
            snprintf(detail, sizeof(detail), "got=%lld kind=%d", static_cast<long long>(iv), Kind(&vi));
            t.check(iv == 1234567890123LL, "ds_map int64 reads back exactly", detail);
            Call(fDestroy, { Num(map) });
        }

        // Async events need the runner's JNI entry point and a real JNIEnv
        JNIEnv* env = nullptr;
        bool haveEnv = g_JavaVM && g_JavaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env;
        t.check(p_JniCreateAsync && haveEnv, "CreateAsyncEventWithDSMap prerequisites");
        t.check(p_ArrayGetLength != nullptr, "YYArrayGetLength resolved");

        __android_log_print(t.failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "PFOTEST", "SUMMARY %d passed, %d failed", t.passed, t.failed);
    }
}
