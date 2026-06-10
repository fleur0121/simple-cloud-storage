#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace scs
{

    void send_file(const std::string &host, std::uint16_t port, const std::filesystem::path &file_path);
    void receive_file(std::uint16_t port, const std::filesystem::path &output_dir);

} // namespace scs
