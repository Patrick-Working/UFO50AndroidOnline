"""Where a test game's threads are: for a game that stopped answering (a hang leaves no crash line).

  python stacks.py <pid> [symbols file]

Only for a test game started by wingame.py (it refuses any other process). Each thread is paused
for a moment, its registers and the top of its stack read, and let go again. Printed per thread:
the processor time it used in the second between two looks (a busy loop shows here), where it is
(module+offset), and the stack's values that point into the game or PFO.dll (return addresses,
most recent first; a guess by value, not an unwind).

The symbols file is a list of "<hex rva> <name>" lines for PFO.dll, made by
scripts/build_pfo_windows.sh next to the PFO.dll it builds (PFO.syms).
"""
import bisect, ctypes, os, sys, time
from ctypes import wintypes as wt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE; k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenThread.restype = wt.HANDLE; k32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE; k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.SuspendThread.argtypes = [wt.HANDLE]; k32.ResumeThread.argtypes = [wt.HANDLE]
k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.GetThreadTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(wt.FILETIME)] * 4


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD), ("GlblcntUsage", wt.DWORD),
                ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", wt.DWORD), ("hModule", wt.HANDLE),
                ("szModule", ctypes.c_wchar * 256), ("szExePath", ctypes.c_wchar * 260)]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD), ("th32OwnerProcessID", wt.DWORD),
                ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG), ("dwFlags", wt.DWORD)]


def modules(pid):
    snap = k32.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
    out = []; me = MODULEENTRY32(); me.dwSize = ctypes.sizeof(me)
    k32.Module32FirstW.argtypes = k32.Module32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(MODULEENTRY32)]
    ok = k32.Module32FirstW(snap, ctypes.byref(me))
    while ok:
        out.append((me.modBaseAddr or 0, me.modBaseSize, me.szModule))
        ok = k32.Module32NextW(snap, ctypes.byref(me))
    k32.CloseHandle(snap)
    return sorted(out)


def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(0x4, 0)
    out = []; te = THREADENTRY32(); te.dwSize = ctypes.sizeof(te)
    k32.Thread32First.argtypes = k32.Thread32Next.argtypes = [wt.HANDLE, ctypes.POINTER(THREADENTRY32)]
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid: out.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    return out


def cpu_ms(h):
    c, e, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
    if not k32.GetThreadTimes(h, c, e, k, u): return 0
    return ((k.dwHighDateTime << 32 | k.dwLowDateTime) + (u.dwHighDateTime << 32 | u.dwLowDateTime)) // 10000


def main():
    pid = int(sys.argv[1])
    if pid not in wingame.game_pids(): raise SystemExit("not a test game started by wingame.py")
    syms = []
    if len(sys.argv) > 2:
        for line in open(sys.argv[2], encoding="utf-8", errors="replace"):
            p = line.split(None, 1)
            if len(p) == 2: syms.append((int(p[0], 16), p[1].strip()))
        syms.sort()
    mods = modules(pid)
    ours = [m for m in mods if m[2].lower() in ("ufo50.exe", "pfo.dll")]

    def name(a):
        for base, size, n in mods:
            if base <= a < base + size:
                rva = a - base
                if n.lower() == "pfo.dll" and syms:
                    i = bisect.bisect_right(syms, (rva, "￿")) - 1
                    if i >= 0: return "%s+%x  %s+%x" % (n, rva, syms[i][1][:90], rva - syms[i][0])
                return "%s+%x" % (n, rva)
        return "%x" % a

    proc = k32.OpenProcess(0x0010 | 0x0400, False, pid)   # read memory, query
    tids = threads(pid)
    hs = {t: k32.OpenThread(0x0002 | 0x0008 | 0x0040, False, t) for t in tids}   # suspend, get context, query
    before = {t: cpu_ms(h) for t, h in hs.items() if h}
    time.sleep(1.0)
    raw = (ctypes.c_char * (1232 + 16))()
    ctx = (ctypes.addressof(raw) + 15) & ~15
    for t in tids:
        h = hs[t]
        if not h: print("thread %d: can't be opened" % t); continue
        used = cpu_ms(h) - before[t]
        looks = []
        for _ in range(3):
            if k32.SuspendThread(h) == 0xFFFFFFFF: break
            ctypes.memset(ctx, 0, 1232)
            ctypes.c_uint32.from_address(ctx + 0x30).value = 0x100003   # control and integer registers
            got = k32.GetThreadContext(h, ctx)
            rsp = ctypes.c_uint64.from_address(ctx + 0x98).value; rip = ctypes.c_uint64.from_address(ctx + 0xF8).value
            stack = (ctypes.c_uint64 * 2048)(); n = ctypes.c_size_t(0)
            if got: k32.ReadProcessMemory(proc, ctypes.c_void_p(rsp), stack, ctypes.sizeof(stack), ctypes.byref(n))
            k32.ResumeThread(h)
            if got: looks.append((rip, [v for v in stack[: n.value // 8] if any(b <= v < b + s for b, s, _ in ours)]))
            time.sleep(0.05)
        if not looks: print("thread %d: no context" % t); continue
        if used < 20 and not any(l[1] for l in looks): continue   # idle, and nothing of ours on its stack
        print("thread %d: %d ms of processor time in the last second" % (t, used))
        for rip, rets in looks:
            print("  at %s" % name(rip))
            for v in rets[:28]: print("      %s" % name(v))
    for h in hs.values():
        if h: k32.CloseHandle(h)
    k32.CloseHandle(proc)


if __name__ == "__main__":
    main()
