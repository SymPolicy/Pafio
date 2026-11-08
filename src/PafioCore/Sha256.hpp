#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace pafio
{

std::string Sha256File(const std::filesystem::path &path);
std::string Sha256Text(const std::string &text);
// Raw 32-byte digest (not hex). Used by TUF transparency-log folding.
std::string Sha256Raw(std::string_view bytes);

}  // namespace pafio
