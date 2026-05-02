#pragma once

#include <filesystem>
#include <string>

namespace spio
{

std::string Sha256File(const std::filesystem::path &path);
std::string Sha256Text(const std::string &text);

}  // namespace spio
