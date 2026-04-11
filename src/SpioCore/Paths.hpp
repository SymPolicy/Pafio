#pragma once

#include <filesystem>

namespace spio
{

inline std::filesystem::path ProjectRoot()
{
  return std::filesystem::path(SPIO_PROJECT_ROOT);
}

}  // namespace spio
