#pragma once

#include <filesystem>
#include <string>

namespace pafio
{

std::string Sha256File(const std::filesystem::path &path);

}  // namespace pafio
