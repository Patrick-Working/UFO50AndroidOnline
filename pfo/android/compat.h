// Windows/MSVC compatibility shims for building PFO with the Android NDK (clang).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <new>
#include <type_traits>
#include <android/log.h>

#define __debugbreak() __builtin_trap()
inline bool IsDebuggerPresent() { return false; }

template<size_t N>
inline int vsprintf_s(char (&buf)[N], const char* fmt, va_list args) { return vsnprintf(buf, N, fmt, args); }
template<size_t N, typename... A>
inline int sprintf_s(char (&buf)[N], const char* fmt, A... args) { return snprintf(buf, N, fmt, args...); }
inline int sprintf_s(char* buf, size_t n, const char* fmt, ...) { va_list a; va_start(a, fmt); int r = vsnprintf(buf, n, fmt, a); va_end(a); return r; }
template<size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) { snprintf(dst, N, "%s", src); return 0; }
template<size_t N>
inline int strcat_s(char (&dst)[N], const char* src) { size_t l = strnlen(dst, N); snprintf(dst + l, N - l, "%s", src); return 0; }

// ---- minimal Win32 file / misc API on top of POSIX ----
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

typedef void* HANDLE;
typedef uint32_t DWORD;
typedef int BOOL;
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_GENERIC_READ 0x00120089u
#define FILE_GENERIC_WRITE 0x00120116u
#define FILE_WRITE_ATTRIBUTES 0x0100u
#define FILE_WRITE_EA 0x0010u
#define FILE_APPEND_DATA 0x0004u
#define SYNCHRONIZE 0x00100000u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define FILE_SHARE_DELETE 4u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_ATTRIBUTE_NORMAL 0x80u

inline HANDLE CreateFileA(const char* name, uint32_t access, uint32_t, void*, uint32_t disposition, uint32_t, void*)
{
    int flags = 0;
    bool wantRead = (access & (GENERIC_READ | 0x0001u)) != 0;
    bool wantWrite = (access & (GENERIC_WRITE | FILE_APPEND_DATA | 0x0002u)) != 0;
    flags = wantRead && wantWrite ? O_RDWR : wantWrite ? O_WRONLY : O_RDONLY;
    if ((access & FILE_APPEND_DATA) && !(access & 0x0002u)) flags |= O_APPEND;
    switch (disposition)
    {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;
    }
    int fd = open(name, flags | O_CLOEXEC, 0644);
    return fd < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
}
inline int HandleFd(HANDLE h) { return static_cast<int>(reinterpret_cast<intptr_t>(h)); }
inline BOOL WriteFile(HANDLE h, const void* data, DWORD n, DWORD* written, void*)
{
    ssize_t r = write(HandleFd(h), data, n);
    if (written) *written = r > 0 ? static_cast<DWORD>(r) : 0;
    return r == static_cast<ssize_t>(n);
}
inline BOOL ReadFile(HANDLE h, void* data, DWORD n, DWORD* read_, void*)
{
    ssize_t r = read(HandleFd(h), data, n);
    if (read_) *read_ = r > 0 ? static_cast<DWORD>(r) : 0;
    return r >= 0;
}
inline BOOL CloseHandle(HANDLE h) { return close(HandleFd(h)) == 0; }
inline DWORD GetFileSize(HANDLE h, DWORD* high)
{
    struct stat st;
    if (fstat(HandleFd(h), &st) != 0) return 0xffffffffu;
    if (high) *high = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) >> 32);
    return static_cast<DWORD>(st.st_size);
}
inline void OutputDebugStringA(const char* s) { __android_log_write(ANDROID_LOG_DEBUG, "PFO", s); }

#define ES_SYSTEM_REQUIRED 1u
#define ES_DISPLAY_REQUIRED 2u
#define ES_CONTINUOUS 0x80000000u
inline uint32_t SetThreadExecutionState(uint32_t) { return 0; }
inline void ExitProcess(unsigned code) { _exit(static_cast<int>(code)); }

// Ring buffer: the same physical pages mapped twice back to back, so reads and
// writes can run past the end and wrap around without splitting.
inline void* AndroidAllocateRingBuffer(size_t size)
{
    int fd = static_cast<int>(syscall(__NR_memfd_create, "pfo_ring", 0));
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) != 0) return nullptr;
    uint8_t* base = static_cast<uint8_t*>(mmap(nullptr, size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (base == MAP_FAILED) { close(fd); return nullptr; }
    void* a = mmap(base, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    void* b = mmap(base + size, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    close(fd);
    return (a == MAP_FAILED || b == MAP_FAILED) ? nullptr : base;
}
inline void* AndroidAllocateLargeBuffer(size_t size)
{
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

#define MAX_PATH 260
typedef size_t rsize_t;
inline int strcpy_s(char* dst, rsize_t n, const char* src) { snprintf(dst, n, "%s", src); return 0; }
inline int strncpy_s(char* dst, rsize_t n, const char* src, rsize_t count) { size_t c = count < n - 1 ? count : n - 1; memcpy(dst, src, c); dst[c] = 0; return 0; }
inline const char* GetCommandLineA() { return ""; }
inline unsigned __lzcnt(uint32_t v) { return v ? static_cast<unsigned>(__builtin_clz(v)) : 32u; }

#define MOVEFILE_REPLACE_EXISTING 1u
#define MOVEFILE_COPY_ALLOWED 2u
#define MOVEFILE_WRITE_THROUGH 8u
inline BOOL MoveFileExA(const char* from, const char* to, uint32_t) { return rename(from, to) == 0; }

// No Win32 message loop on Android: the runner owns the event loop.
struct MSG { uint32_t message; void* hwnd; uintptr_t wParam; intptr_t lParam; };
#define PM_REMOVE 1u
#define WM_CLOSE 0x0010u
#define WM_QUIT 0x0012u
inline BOOL PeekMessageW(MSG*, void*, uint32_t, uint32_t, uint32_t) { return 0; }
inline BOOL TranslateMessage(const MSG*) { return 0; }
inline intptr_t DispatchMessageW(const MSG*) { return 0; }
inline BOOL PostMessageW(void*, uint32_t, uintptr_t, intptr_t) { return 0; }
