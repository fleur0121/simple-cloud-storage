#pragma once

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace scs
{

    inline std::runtime_error system_error(const std::string &message)
    {
        return std::runtime_error(message + ": " + std::strerror(errno));
    }

    // Sends all bytes in the given buffer over the socket.
    // Throws if the connection is closed or an error occurs.
    inline void send_all(int fd, const void *data, std::size_t size)
    {
        const auto *cursor = static_cast<const char *>(data);
        while (size > 0)
        {
            const ssize_t sent = ::send(fd, cursor, size, 0);
            if (sent < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw system_error("send failed");
            }
            if (sent == 0)
            {
                throw std::runtime_error("socket closed while sending");
            }
            cursor += sent;
            size -= static_cast<std::size_t>(sent);
        }
    }

    // Receives the specified number of bytes from the socket and stores them in the given buffer.
    inline void recv_all(int fd, void *data, std::size_t size)
    {
        auto *cursor = static_cast<char *>(data);
        while (size > 0)
        {
            const ssize_t received = ::recv(fd, cursor, size, 0);
            if (received < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw system_error("receive failed");
            }
            if (received == 0)
            {
                throw std::runtime_error("socket closed before transfer completed");
            }
            cursor += received;
            size -= static_cast<std::size_t>(received);
        }
    }

    // Extracts the file name from the given path and checks that it is valid for use in the protocol.
    inline std::string safe_file_name(const std::filesystem::path &path)
    {
        const auto name = path.filename().string();
        if (name.empty() || name == "." || name == "..")
        {
            throw std::runtime_error("invalid file name");
        }
        return name;
    }

    // RAII wrapper for a POSIX socket file descriptor.
    //
    // socket(), accept(), and similar POSIX APIs return an int file descriptor.
    // That descriptor must eventually be closed with close(fd). If every caller
    // has to remember to close it manually, error paths can easily leak sockets.
    //
    // This class owns exactly one socket descriptor and closes it in the
    // destructor. Because destructors run automatically when an object leaves
    // scope, cleanup still happens when a function returns early or throws.
    //
    // This is called RAII: Resource Acquisition Is Initialization.
    // In short, the Socket object's lifetime controls the socket resource's
    // lifetime.
    //
    // Socket is move-only:
    //   - Copying is disabled because two Socket objects must not close the
    //     same fd.
    //   - Moving is allowed because ownership sometimes needs to be transferred,
    //     for example when returning a Socket from a helper function.
    //
    // Example:
    //   Socket listener(::socket(AF_INET, SOCK_STREAM, 0));
    //   Socket peer(::accept(listener.get(), nullptr, nullptr));
    //
    // When listener and peer leave scope, both descriptors are closed
    // automatically.
    class Socket
    {
    public:
        // fd == -1 means "this object does not currently own a socket".
        // That value is used for default construction and moved-from objects.
        explicit Socket(int fd = -1) : fd_(fd) {}

        // Destructor: close the owned descriptor if there is one.
        // This is the automatic cleanup point.
        ~Socket()
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
        }

        // Copying is disabled to avoid double-close bugs.
        // If copying were allowed, two Socket objects could both think they own
        // the same fd and both call close(fd).
        Socket(const Socket &) = delete;
        Socket &operator=(const Socket &) = delete;

        // Move constructor: take ownership of other's fd.
        // other.fd_ is set to -1 so the moved-from object will not close it.
        Socket(Socket &&other) noexcept : fd_(other.fd_)
        {
            other.fd_ = -1;
        }

        // Move assignment: release the current fd, then take ownership of
        // other's fd. The self-assignment check protects against moving an
        // object into itself.
        Socket &operator=(Socket &&other) noexcept
        {
            if (this != &other)
            {
                if (fd_ >= 0)
                {
                    ::close(fd_);
                }
                fd_ = other.fd_;
                other.fd_ = -1;
            }
            return *this;
        }

        // Return the raw descriptor for POSIX APIs such as bind(), listen(),
        // accept(), connect(), send(), and recv().
        //
        // This does not transfer ownership. The Socket object still remains
        // responsible for closing the descriptor.
        int get() const
        {
            return fd_;
        }

    private:
        // The owned socket descriptor. -1 means no descriptor is owned.
        int fd_;
    };

} // namespace scs
