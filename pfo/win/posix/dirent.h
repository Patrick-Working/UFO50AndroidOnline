// <dirent.h> for the Windows build: just what android/savestate.inc uses, over FindFirstFileW,
// with the Android file names mapped as in win/compat_win.h. Names come back as UTF-8.
#pragma once

#include "../compat_win.h"

struct dirent
{
    char d_name[MAX_PATH * 3 + 1];
};

struct DIR
{
    HANDLE find;
    bool first;
    WIN32_FIND_DATAW data;
    dirent entry;
};

inline DIR* opendir(const char* path)
{
    std::wstring pattern = pfo_win::Path(path) + L"\\*";
    DIR* d = new DIR();
    d->find = FindFirstFileW(pattern.c_str(), &d->data);
    if (d->find == INVALID_HANDLE_VALUE) { delete d; return nullptr; }
    d->first = true;
    return d;
}

inline dirent* readdir(DIR* d)
{
    if (!d) return nullptr;
    if (d->first) d->first = false;
    else if (!FindNextFileW(d->find, &d->data)) return nullptr;
    std::string name = pfo_win::Utf8(d->data.cFileName);
    snprintf(d->entry.d_name, sizeof(d->entry.d_name), "%s", name.c_str());
    return &d->entry;
}

inline int closedir(DIR* d)
{
    if (!d) return -1;
    FindClose(d->find);
    delete d;
    return 0;
}
