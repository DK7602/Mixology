#pragma once

#include <cstddef>
#include <string>

/** A named block of memory shared by every module loaded into this process.

    Mixology and Mixology Link are two plug-ins, so the host loads them as two separate libraries: a static
    variable in one is invisible to the other. Both open the same named block instead (the name includes the
    process ID, so two hosts running side by side never see each other's). The OS creates it zero-filled for
    whoever comes first and keeps it alive while anyone has it open.

    Windows: a page-file-backed file mapping in the session-local namespace. Elsewhere: POSIX shared memory,
    removed again when the last user in the process closes it. If the OS refuses, the block falls back to
    private memory: everything still works inside one library, the plug-ins just can't see each other. */
namespace mixology
{
    class SharedBlock
    {
    public:
        SharedBlock (const std::string& name, std::size_t bytes);
        ~SharedBlock();

        SharedBlock (const SharedBlock&) = delete;
        SharedBlock& operator= (const SharedBlock&) = delete;

        /** Zero-filled on creation; aligned to at least 64 bytes. */
        void* data() const { return userData; }
        std::size_t size() const { return userBytes; }

        /** False when the OS refused and the block is private to this library. */
        bool isShared() const { return shared; }

        /** "<base>.<process id>": one block per host process. */
        static std::string perProcessName (const std::string& base);

    private:
        void* mapping = nullptr;   // the whole mapping (our header + user data)
        void* userData = nullptr;
        std::size_t userBytes = 0, totalBytes = 0;
        bool shared = false;
        std::string osName;
#if defined(_WIN32)
        void* handle = nullptr;
#endif
    };
}
