#pragma once

#include <filesystem>
#include <string>

namespace pafio
{

std::string Sha256File(const std::filesystem::path &path);
std::string Sha256Text(const std::string &text);

}  // namespace pafio
