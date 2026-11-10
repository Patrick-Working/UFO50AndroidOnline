// Windows stand-ins for the Android and POSIX pieces the shared sources use
// (android/fakesteam_impl.inc, netplus.inc, okplay_client.inc, savestate.inc).
// pch.h includes this after windows.h and winsock2.h when PFO_WINDOWS_OURS is defined.
//
// Files: the Android build keeps its own files in /sdcard/Android/data/<package>/files. Here
// that folder is %LOCALAPPDATA%\ufo50_online (Path() below maps the names). Relative names stay
// in the working directory, the game's folder.
// Log: logcat's place is taken by %LOCALAPPDATA%\ufo50_online\pfo_online.log.
#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <share.h>
#include <mutex>
#include <string>

#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6

namespace pfo_win
{
    inline std::wstring Wide(const char* utf8)
    {
        if (!utf8 || !*utf8) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (n <= 1) return std::wstring();
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
        return w;
    }

    inline std::string Utf8(const wchar_t* w)
    {
        if (!w || !*w) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1) return std::string();
        std::string s(static_cast<size_t>(n - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
        return s;
    }

    // our own folder (made when first asked for)
    inline const std::wstring& DataDir()
    {
        static const std::wstring dir = []
        {
            wchar_t base[MAX_PATH] = {};
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
            std::wstring d = (n > 0 && n < MAX_PATH) ? std::wstring(base) + L"\\ufo50_online" : std::wstring(L"ufo50_online");
            CreateDirectoryW(d.c_str(), nullptr);
            return d;
        }();
        return dir;
    }

    // "/sdcard/Android/data/com.unofficial.ufo50/files/x" -> "<our folder>\x"; other names unchanged
    inline std::wstring Path(const char* p)
    {
        static const char prefix[] = "/sdcard/Android/data/com.unofficial.ufo50/files";
        const size_t n = sizeof(prefix) - 1;
        if (p && strncmp(p, prefix, n) == 0 && (p[n] == '/' || p[n] == 0))
        {
            std::wstring w = DataDir() + Wide(p + n);
            for (wchar_t& c : w) if (c == L'/') c = L'\\';
            return w;
        }
        return Wide(p);
    }

    // ---- files (always binary, as on Android)
    inline FILE* Fopen(const char* path, const char* mode)
    {
        std::wstring m = Wide(mode);
        if (m.find(L'b') == std::wstring::npos) m += L'b';
        return _wfopen(Path(path).c_str(), m.c_str());
    }
    inline int Access(const char* path, int) { return GetFileAttributesW(Path(path).c_str()) == INVALID_FILE_ATTRIBUTES ? -1 : 0; }
    inline int Remove(const char* path) { return DeleteFileW(Path(path).c_str()) ? 0 : -1; }
    inline int Rename(const char* from, const char* to) { return MoveFileExW(Path(from).c_str(), Path(to).c_str(), MOVEFILE_REPLACE_EXISTING) ? 0 : -1; }
    inline int Mkdir(const char* path) { return CreateDirectoryW(Path(path).c_str(), nullptr) ? 0 : -1; }

    // ---- log
    inline void LogLine(int prio, const char* tag, const char* text)
    {
        static std::mutex mutex;
        static FILE* file = nullptr;
        static bool tried = false;
        std::lock_guard<std::mutex> lock(mutex);
        if (!tried)
        {
            tried = true;
            std::wstring path = DataDir() + L"\\pfo_online.log";
            // keep one old log: a run starts a new file once the last one is over 4 MB
            WIN32_FILE_ATTRIBUTE_DATA info = {};
            if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && (info.nFileSizeHigh != 0 || info.nFileSizeLow > 4u * 1024 * 1024))
                MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
            file = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
        }
        if (!file) return;
        SYSTEMTIME t;
        GetLocalTime(&t);
        static const char c_Prio[] = "??VDIWEF";
        size_t len = strlen(text);
        while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) len--;
        fprintf(file, "%02d-%02d %02d:%02d:%02d.%03d %c %s: %.*s\n", t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                c_Prio[prio >= 0 && prio <= 7 ? prio : 0], tag ? tag : "", static_cast<int>(len), text);
        fflush(file);
    }

    // ---- sockets: Winsock takes char buffers and int lengths, and its sockets aren't ints
    inline int Socket(int af, int type, int protocol)
    {
        static const bool started = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
        (void)started;
        SOCKET s = ::socket(af, type, protocol);
        if (s == INVALID_SOCKET) return -1;
        if (type == SOCK_DGRAM)
        {
            // an ICMP "port unreachable" must not fail the next receive on a UDP socket
            BOOL off = FALSE;
            DWORD got = 0;
            WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &off, sizeof(off), nullptr, 0, &got, nullptr, nullptr);
        }
        return static_cast<int>(s);
    }
    inline int SetSockOpt(int s, int level, int name, const void* value, int len) { return ::setsockopt(static_cast<SOCKET>(s), level, name, static_cast<const char*>(value), len); }
    inline int SendTo(int s, const void* data, size_t n, int flags, const sockaddr* to, int toLen) { return ::sendto(static_cast<SOCKET>(s), static_cast<const char*>(data), static_cast<int>(n), flags, to, toLen); }
    inline int RecvFrom(int s, void* data, size_t n, int flags, sockaddr* from, int* fromLen) { return ::recvfrom(static_cast<SOCKET>(s), static_cast<char*>(data), static_cast<int>(n), flags, from, fromLen); }
    inline int Send(int s, const void* data, size_t n, int flags) { return ::send(static_cast<SOCKET>(s), static_cast<const char*>(data), static_cast<int>(n), flags); }
    inline int Recv(int s, void* data, size_t n, int flags) { return ::recv(static_cast<SOCKET>(s), static_cast<char*>(data), static_cast<int>(n), flags); }
    inline int CloseSocket(int s) { return ::closesocket(static_cast<SOCKET>(s)); }
}

inline int __android_log_write(int prio, const char* tag, const char* text)
{
    pfo_win::LogLine(prio, tag, text ? text : "");
    return 1;
}

inline int __android_log_print(int prio, const char* tag, const char* format, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    pfo_win::LogLine(prio, tag, buf);
    return 1;
}

// MSVC's intrinsic (clang names it __lzcnt32)
inline unsigned __lzcnt(uint32_t v) { return v ? static_cast<unsigned>(__builtin_clz(v)) : 32u; }

// ---- fcntl: only the non-blocking switch is used
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x800
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

inline int fcntl(int s, int cmd, int arg = 0)
{
    if (cmd != F_SETFL) return 0;
    u_long on = (arg & O_NONBLOCK) ? 1 : 0;
    return ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on);
}

inline int inet_aton(const char* text, in_addr* out)
{
    return text && inet_pton(AF_INET, text, out) == 1 ? 1 : 0;
}

// ---- getifaddrs: IPv4 addresses of the adapters that are up, those with a gateway first
#ifndef IFF_UP
#define IFF_UP 0x1
#endif
#ifndef IFF_BROADCAST
#define IFF_BROADCAST 0x2
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK 0x4
#endif

struct ifaddrs
{
    ifaddrs* ifa_next;
    const char* ifa_name;
    unsigned int ifa_flags;
    sockaddr* ifa_addr;
    sockaddr* ifa_netmask;
    sockaddr* ifa_broadaddr;
    sockaddr_in pfo_addr, pfo_mask, pfo_broad;
};

inline void freeifaddrs(ifaddrs* p)
{
    while (p) { ifaddrs* next = p->ifa_next; free(p); p = next; }
}

inline int getifaddrs(ifaddrs** out)
{
    *out = nullptr;
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES* list = nullptr;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < 4 && rc == ERROR_BUFFER_OVERFLOW; i++)
    {
        free(list);
        list = static_cast<IP_ADAPTER_ADDRESSES*>(malloc(size));
        if (!list) return -1;
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS, nullptr, list, &size);
    }
    if (rc != NO_ERROR) { free(list); return -1; }
    ifaddrs** tail = out;
    for (int pass = 0; pass < 2; pass++)
    {
        for (IP_ADAPTER_ADDRESSES* a = list; a; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp) continue;
            if ((a->FirstGatewayAddress != nullptr) != (pass == 0)) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next)
            {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                ifaddrs* n = static_cast<ifaddrs*>(calloc(1, sizeof(ifaddrs)));
                if (!n) break;
                n->pfo_addr = *reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                unsigned int bits = u->OnLinkPrefixLength > 32 ? 32 : u->OnLinkPrefixLength;
                uint32_t mask = bits ? htonl(0xffffffffu << (32 - bits)) : 0;
                n->pfo_mask.sin_family = AF_INET;
                n->pfo_mask.sin_addr.s_addr = mask;
                n->pfo_broad = n->pfo_addr;
                n->pfo_broad.sin_addr.s_addr |= ~mask;
                bool loopback = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
                n->ifa_name = "";
                n->ifa_flags = IFF_UP | (loopback ? IFF_LOOPBACK : IFF_BROADCAST);
                n->ifa_addr = reinterpret_cast<sockaddr*>(&n->pfo_addr);
                n->ifa_netmask = reinterpret_cast<sockaddr*>(&n->pfo_mask);
                n->ifa_broadaddr = loopback ? nullptr : reinterpret_cast<sockaddr*>(&n->pfo_broad);
                *tail = n;
                tail = &n->ifa_next;
            }
        }
    }
    free(list);
    return 0;
}
