/*
 * Gamebuino AKA - portable shared file mapping (POSIX mmap / Win32).
 * aka_shm_map() opens (creating if needed) a file of `size` bytes and maps it
 * read/write shared. Returns NULL on failure. *created is set when the file
 * was newly created or zero-sized (optional).
 */
#ifndef AKA_SHM_H
#define AKA_SHM_H

#include <stddef.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>

static inline void *aka_shm_map(const char *path, size_t size, bool rw)
{
    DWORD acc = rw ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    HANDLE f = CreateFileA(path, acc,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, rw ? OPEN_ALWAYS : OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    HANDLE m = CreateFileMappingA(f, NULL, rw ? PAGE_READWRITE : PAGE_READONLY,
                                  0, (DWORD)size, NULL);
    CloseHandle(f);
    if (!m) {
        return NULL;
    }
    void *p = MapViewOfFile(m, rw ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ,
                            0, 0, size);
    CloseHandle(m);          /* the view keeps the mapping alive */
    return p;
}

#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

static inline void *aka_shm_map(const char *path, size_t size, bool rw)
{
    int fd = open(path, rw ? (O_RDWR | O_CREAT) : O_RDONLY, 0666);
    if (fd < 0) {
        return NULL;
    }
    if (rw && ftruncate(fd, size) != 0) {
        close(fd);
        return NULL;
    }
    void *p = mmap(NULL, size, rw ? (PROT_READ | PROT_WRITE) : PROT_READ,
                   MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? NULL : p;
}
#endif

#endif
