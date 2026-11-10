// Windows build: included by dllmain.cpp just before android/fakesteam_impl.inc.
// The shared sources call POSIX file and socket functions by name; from here to the end of the
// translation unit those names go to the stand-ins in win/compat_win.h (file names mapped to our
// folder, Winsock argument types).
//
// Every header the shared sources include is included here first: a function-like macro called
// "remove" or "close" must not be defined yet when a standard header is read.
#pragma once

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>   // win/posix/dirent.h

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <enet/enet.h>
#include <enet/time.h>

#ifndef F_OK
#define F_OK 0
#endif

#define fopen(path, mode) pfo_win::Fopen(path, mode)
#define access(path, mode) pfo_win::Access(path, mode)
#define remove(path) pfo_win::Remove(path)
#define rename(from, to) pfo_win::Rename(from, to)
#define mkdir(path, mode) pfo_win::Mkdir(path)

#define socket(af, type, protocol) pfo_win::Socket(af, type, protocol)
#define setsockopt(s, level, name, value, len) pfo_win::SetSockOpt(s, level, name, value, len)
#define sendto(s, data, n, flags, to, toLen) pfo_win::SendTo(s, data, n, flags, to, toLen)
#define recvfrom(s, data, n, flags, from, fromLen) pfo_win::RecvFrom(s, data, n, flags, from, fromLen)
#define send(s, data, n, flags) pfo_win::Send(s, data, n, flags)
#define recv(s, data, n, flags) pfo_win::Recv(s, data, n, flags)
#define close(s) pfo_win::CloseSocket(s)
