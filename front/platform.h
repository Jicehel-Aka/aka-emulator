// Couche plateforme du front-end : fichiers partages (mmap / CreateFileMapping)
// et pilotage du processus QEMU (fork/exec / CreateProcess).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace plat {

// Repertoire par defaut des fichiers partages QEMU <-> front-end.
inline std::string shared_dir()
{
#ifdef _WIN32
    const char *t = getenv("TEMP");
    std::string d = std::string(t ? t : ".") + "\\aka-emu";
    CreateDirectoryA(d.c_str(), nullptr);
    return d + "\\";
#else
    return "/dev/shm/aka-";      // prefixe : /dev/shm/aka-fb, aka-input, aka-audio
#endif
}

inline bool file_exists(const std::string &p)
{
#ifdef _WIN32
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    return access(p.c_str(), R_OK) == 0;
#endif
}

// Supprime un fichier partage (impossible sous Windows tant qu'il est mappe : ignore).
inline void remove_shared(const char *p)
{
#ifndef _WIN32
    unlink(p);
#else
    (void)p;
#endif
}

// Mappe un fichier partage. rw=true : cree/agrandit a `size`. rw=false : lecture seule,
// echoue (nullptr) si le fichier n'existe pas ou fait moins de `size` octets.
inline void *map_file(const char *path, size_t size, bool rw)
{
#ifdef _WIN32
    HANDLE f = CreateFileA(path, rw ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           rw ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    if (!rw) {
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(f, &sz) || (size_t)sz.QuadPart < size) { CloseHandle(f); return nullptr; }
    }
    HANDLE m = CreateFileMappingA(f, nullptr, rw ? PAGE_READWRITE : PAGE_READONLY, 0, (DWORD)size, nullptr);
    CloseHandle(f);
    if (!m) return nullptr;
    void *p = MapViewOfFile(m, rw ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ, 0, 0, size);
    CloseHandle(m);
    return p;
#else
    int fd = open(path, rw ? (O_RDWR | O_CREAT) : O_RDONLY, 0666);
    if (fd < 0) return nullptr;
    void *p = nullptr;
    struct stat st;
    if (rw ? ftruncate(fd, size) == 0 : (fstat(fd, &st) == 0 && (size_t)st.st_size >= size))
        p = mmap(nullptr, size, rw ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? nullptr : p;
#endif
}

// ---- processus ----
#ifdef _WIN32
typedef HANDLE proc_t;
inline proc_t proc_none() { return nullptr; }
inline bool proc_valid(proc_t p) { return p != nullptr; }

static inline std::string quote_arg(const std::string &a)
{
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string r = "\"";
    for (size_t i = 0; i < a.size(); i++) {
        size_t bs = 0;
        while (i < a.size() && a[i] == '\\') { bs++; i++; }
        if (i == a.size()) { r.append(bs * 2, '\\'); break; }
        if (a[i] == '"') { r.append(bs * 2 + 1, '\\'); r += '"'; }
        else { r.append(bs, '\\'); r += a[i]; }
    }
    return r + "\"";
}

inline proc_t spawn(const std::vector<std::string> &argv)
{
    std::string cmd;
    for (auto &a : argv) { if (!cmd.empty()) cmd += ' '; cmd += quote_arg(a); }
    STARTUPINFOA si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}
inline void kill_wait(proc_t &p)
{
    if (!p) return;
    TerminateProcess(p, 0); WaitForSingleObject(p, 5000); CloseHandle(p); p = nullptr;
}
inline bool exited(proc_t &p)         // true (et libere) si le processus est termine
{
    if (!p) return false;
    if (WaitForSingleObject(p, 0) == WAIT_OBJECT_0) { CloseHandle(p); p = nullptr; return true; }
    return false;
}
#else
typedef pid_t proc_t;
inline proc_t proc_none() { return -1; }
inline bool proc_valid(proc_t p) { return p > 0; }

inline proc_t spawn(const std::vector<std::string> &a)
{
    pid_t pid = fork();
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY); dup2(dn, 1); dup2(dn, 2);
        std::vector<char *> av; for (auto &x : a) av.push_back((char *)x.c_str()); av.push_back(nullptr);
        execvp(av[0], av.data()); _exit(127);
    }
    return pid;
}
inline void kill_wait(proc_t &p)
{
    if (p > 0) { kill(p, SIGTERM); int st; waitpid(p, &st, 0); p = -1; }
}
inline bool exited(proc_t &p)
{
    if (p <= 0) return false;
    int st; if (waitpid(p, &st, WNOHANG) == p) { p = -1; return true; }
    return false;
}
#endif

} // namespace plat
