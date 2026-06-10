#pragma once

#include <cstdint>

namespace scs
{

    // Magic value sent at the beginning of every transfer header.
    // The receiver uses this to reject data that is not from this protocol.
    constexpr std::uint32_t kProtocolMagic = 0x53435331; // "SCS1"

    // Protocol version. Increase this when the wire format changes.
    constexpr std::uint16_t kProtocolVersion = 1;

    // Maximum file name length accepted from the sender.
    // This prevents accidentally allocating memory for a broken or malicious header.
    constexpr std::uint32_t kMaxFileNameLength = 4096;

    // Size of the chunks used while copying file bytes over the socket.
    // The whole file is not loaded into memory at once.
    constexpr std::uint64_t kCopyBufferSize = 64 * 1024;

    // Fixed-size metadata sent before the actual file bytes.
    // Transfer order is:
    //   1. FileHeader
    //   2. file_name_length bytes containing the file name
    //   3. file_size bytes containing the file content
    struct FileHeader
    {
        // Must match kProtocolMagic, otherwise the receiver rejects the transfer.
        std::uint32_t magic;

        // Must match kProtocolVersion, so both sides agree on the header format.
        std::uint16_t version;

        // Reserved space for future header flags/options.
        // It is currently always sent as 0.
        std::uint16_t reserved;

        // Number of file-content bytes that follow the file name.
        std::uint64_t file_size;

        // Number of bytes in the file name that follows this header.
        std::uint32_t file_name_length;
    };

} // namespace scs
