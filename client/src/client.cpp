#include "client.hpp"

#include "protocol.hpp"
#include "utils.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace fs = std::filesystem;

namespace scs
{
    namespace
    {
        // Parses a string as a TCP port number. Throws if the value is invalid.
        std::uint16_t parse_port(const std::string &value)
        {
            const unsigned long parsed = std::stoul(value);
            if (parsed == 0 || parsed > std::numeric_limits<std::uint16_t>::max())
            {
                throw std::runtime_error("port must be between 1 and 65535");
            }
            return static_cast<std::uint16_t>(parsed);
        }

        // Creates a TCP socket and returns it wrapped in a Socket object. Throws on failure.
        Socket make_tcp_socket()
        {
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
            {
                throw system_error("socket creation failed");
            }
            return Socket(fd);
        }

        // Creates a sockaddr_in structure for the given host and port. Throws if the host is not a valid IPv4 address.
        sockaddr_in make_address(const std::string &host, std::uint16_t port)
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
            {
                throw std::runtime_error("host must be an IPv4 address, e.g. 127.0.0.1");
            }
            return address;
        }

        // Prints usage information to stderr.
        void print_usage(const char *program_name)
        {
            std::cerr
                << "Usage:\n"
                << "  " << program_name << " recv <port> <output_dir>\n"
                << "  " << program_name << " send <host> <port> <file_path>\n\n"
                << "Example:\n"
                << "  " << program_name << " recv 9090 ./received\n"
                << "  " << program_name << " send 127.0.0.1 9090 ./notes.txt\n";
        }

    } // namespace

    // Sends the specified file to the given host and port using the custom protocol. Throws on failure.
    void send_file(const std::string &host, std::uint16_t port, const fs::path &file_path)
    {
        if (!fs::is_regular_file(file_path))
        {
            throw std::runtime_error("input path is not a regular file: " + file_path.string());
        }

        const auto file_size = fs::file_size(file_path);
        const std::string file_name = safe_file_name(file_path);
        if (file_name.size() > kMaxFileNameLength)
        {
            throw std::runtime_error("file name is too long");
        }

        std::ifstream input(file_path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("failed to open file: " + file_path.string());
        }

        Socket socket = make_tcp_socket();
        sockaddr_in address = make_address(host, port);
        if (::connect(socket.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
        {
            throw system_error("connect failed");
        }

        const FileHeader header{
            kProtocolMagic,
            kProtocolVersion,
            0,
            static_cast<std::uint64_t>(file_size),
            static_cast<std::uint32_t>(file_name.size()),
        };

        send_all(socket.get(), &header, sizeof(header));
        send_all(socket.get(), file_name.data(), file_name.size());

        std::vector<char> buffer(kCopyBufferSize);
        std::uint64_t sent_total = 0;
        while (input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count <= 0)
            {
                break;
            }
            send_all(socket.get(), buffer.data(), static_cast<std::size_t>(count));
            sent_total += static_cast<std::uint64_t>(count);
        }

        if (sent_total != file_size)
        {
            throw std::runtime_error("file changed while sending");
        }

        std::cout << "Sent " << file_name << " (" << sent_total << " bytes)\n";
    }

    // Listens for an incoming file on the specified port and saves it to the given output directory. Throws on failure.
    void receive_file(std::uint16_t port, const fs::path &output_dir)
    {
        fs::create_directories(output_dir);

        Socket listener = make_tcp_socket();
        const int reuse = 1;
        if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            throw system_error("setsockopt failed");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);

        if (::bind(listener.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
        {
            throw system_error("bind failed");
        }
        if (::listen(listener.get(), 1) < 0)
        {
            throw system_error("listen failed");
        }

        std::cout << "Waiting for one file on port " << port << "...\n";

        Socket peer(::accept(listener.get(), nullptr, nullptr));
        if (peer.get() < 0)
        {
            throw system_error("accept failed");
        }

        FileHeader header{};
        recv_all(peer.get(), &header, sizeof(header));
        if (header.magic != kProtocolMagic || header.version != kProtocolVersion)
        {
            throw std::runtime_error("unsupported transfer protocol");
        }
        if (header.file_name_length == 0 || header.file_name_length > kMaxFileNameLength)
        {
            throw std::runtime_error("invalid file name length");
        }

        std::string file_name(header.file_name_length, '\0');
        recv_all(peer.get(), file_name.data(), file_name.size());

        const fs::path destination = output_dir / safe_file_name(file_name);
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("failed to create output file: " + destination.string());
        }

        std::vector<char> buffer(kCopyBufferSize);
        std::uint64_t remaining = header.file_size;
        while (remaining > 0)
        {
            const auto chunk_size = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            recv_all(peer.get(), buffer.data(), chunk_size);
            output.write(buffer.data(), static_cast<std::streamsize>(chunk_size));
            if (!output)
            {
                throw std::runtime_error("failed while writing output file");
            }
            remaining -= chunk_size;
        }

        std::cout << "Received " << destination << " (" << header.file_size << " bytes)\n";
    }

} // namespace scs

int main(int argc, char **argv)
{
    try
    {
        if (argc == 4 && std::string(argv[1]) == "recv")
        {
            scs::receive_file(scs::parse_port(argv[2]), argv[3]);
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "send")
        {
            scs::send_file(argv[2], scs::parse_port(argv[3]), argv[4]);
            return 0;
        }

        scs::print_usage(argv[0]);
        return 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
