module;

#ifndef _MSC_VER
#include <string>
#include <span>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#else
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>
#endif

export module net_io.ipc_transport;
import net_io_base; // make SocketException / NetworkException available
import net_io_concepts;

export namespace modern::net {

/**
 * @brief Platform-independent synchronous IPC transport (pipe/FIFO/named pipe)
 */
class IPCTransport
{
public:
    IPCTransport() = default;

#ifndef _MSC_VER
    explicit IPCTransport(const std::string& name, bool write = false)
    {
        int flags = write ? O_WRONLY : O_RDONLY;
        fd_ = ::open(name.c_str(), flags);
        if (fd_ < 0)
            throw SocketException(std::string("IPCTransport open failed: ") + std::strerror(errno), errno);
    }

    std::size_t read(char* buf, std::size_t n)
    {
        ssize_t ret = ::read(fd_, buf, n);
        if (ret < 0)
            throw SocketException(std::string("IPCTransport read failed: ") + std::strerror(errno), errno);
        return static_cast<std::size_t>(ret);
    }

    void write(const char* buf, std::size_t n)
    {
        ssize_t ret = ::write(fd_, buf, n);
        if (ret < 0)
            throw SocketException(std::string("IPCTransport write failed: ") + std::strerror(errno), errno);
    }

    void open(const std::string& name, bool write = false)
    {
        int flags = write ? O_WRONLY : O_RDONLY;
        fd_ = ::open(name.c_str(), flags);
        if (fd_ < 0)
            throw SocketException(std::string("IPCTransport open failed: ") + std::strerror(errno), errno);
    }

    void close()
    {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = -1;
    }

    ~IPCTransport() { close(); }

private:
    int fd_ = -1;
#else
    explicit IPCTransport(const std::string& name, bool write = false)
    {
        DWORD access = write ? GENERIC_WRITE : GENERIC_READ;
        h_ = CreateFileA(
            name.c_str(),
            access,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (h_ == INVALID_HANDLE_VALUE)
            throw SocketException("IPCTransport open failed", -1);
    }

    std::size_t read(char* buf, std::size_t n)
    {
        DWORD read = 0;
        if (!ReadFile(h_, buf, static_cast<DWORD>(n), &read, nullptr))
            throw SocketException("IPCTransport read failed", -1);
        return static_cast<std::size_t>(read);
    }

    void write(const char* buf, std::size_t n)
    {
        DWORD written = 0;
        if (!WriteFile(h_, buf, static_cast<DWORD>(n), &written, nullptr))
            throw SocketException("IPCTransport write failed", -1);
    }

    void open(const std::string& name, bool write = false)
    {
        DWORD access = write ? GENERIC_WRITE : GENERIC_READ;
        h_ = CreateFileA(
            name.c_str(),
            access,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (h_ == INVALID_HANDLE_VALUE)
            throw SocketException("IPCTransport open failed", -1);
    }

    void close()
    {
        if (h_ != INVALID_HANDLE_VALUE)
            CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }

    ~IPCTransport() { close(); }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
#endif
};

} // namespace modern::net

export namespace net_io {

using IPCTransport = modern::net::IPCTransport;

} // namespace net_io

// Compile-time check: IPCTransport should satisfy Readable or Writable (platform-specific)
static_assert(net_io_concepts::Readable<modern::net::IPCTransport> && net_io_concepts::Writable<modern::net::IPCTransport>,
              "IPCTransport should implement both Readable and Writable concepts");