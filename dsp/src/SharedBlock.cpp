#include "mixology/SharedBlock.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace mixology
{
    namespace
    {
        // Our own header in front of the user data: how many users in this process have it open
        // (POSIX only needs it, to remove the name when the last one closes).
        struct Header
        {
            std::atomic<std::int32_t> users;
        };
        constexpr std::size_t kHeaderBytes = 64;
        static_assert (sizeof (Header) <= kHeaderBytes);
        static_assert (std::atomic<std::int32_t>::is_always_lock_free);

        Header* header (void* mapping) { return static_cast<Header*> (mapping); }
    }

    std::string SharedBlock::perProcessName (const std::string& base)
    {
#if defined(_WIN32)
        const auto pid = static_cast<unsigned long> (GetCurrentProcessId());
#else
        const auto pid = static_cast<unsigned long> (getpid());
#endif
        return base + "." + std::to_string (pid);
    }

    SharedBlock::SharedBlock (const std::string& name, std::size_t bytes)
        : userBytes (bytes), totalBytes (bytes + kHeaderBytes)
    {
#if defined(_WIN32)
        osName = "Local\\" + name;
        const auto size = static_cast<unsigned long long> (totalBytes);
        handle = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     static_cast<DWORD> (size >> 32), static_cast<DWORD> (size & 0xFFFFFFFFull), osName.c_str());
        if (handle != nullptr)
        {
            mapping = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, totalBytes);
            if (mapping == nullptr)
            {
                CloseHandle (handle);
                handle = nullptr;
            }
        }
#else
        osName = "/" + name;
        const int fd = shm_open (osName.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd >= 0)
        {
            struct stat st {};
            bool ok = fstat (fd, &st) == 0;
            if (ok && static_cast<std::size_t> (st.st_size) < totalBytes)
                ok = ftruncate (fd, static_cast<off_t> (totalBytes)) == 0;   // new pages read as zero
            if (ok)
            {
                void* p = mmap (nullptr, totalBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (p != MAP_FAILED) mapping = p;
            }
            close (fd);
        }
#endif

        if (mapping != nullptr)
        {
            shared = true;
        }
        else
        {
            // Private fallback: aligned, zeroed.
            mapping = ::operator new (totalBytes, std::align_val_t { 64 });
            std::memset (mapping, 0, totalBytes);
        }
        header (mapping)->users.fetch_add (1);
        userData = static_cast<char*> (mapping) + kHeaderBytes;
    }

    SharedBlock::~SharedBlock()
    {
        if (mapping == nullptr) return;
        const bool last = header (mapping)->users.fetch_sub (1) == 1;

        if (! shared)
        {
            ::operator delete (mapping, std::align_val_t { 64 });
            return;
        }
#if defined(_WIN32)
        (void) last;   // Windows frees the mapping when the last handle closes
        UnmapViewOfFile (mapping);
        CloseHandle (handle);
#else
        munmap (mapping, totalBytes);
        if (last) shm_unlink (osName.c_str());
#endif
    }
}
