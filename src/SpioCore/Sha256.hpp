#pragma once

#include <filesystem>
#include <string>

namespace spio
{

std::string Sha256File(const std::filesystem::path &path);

}  // namespace spio
