#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace spio
{

struct CompatibilityReport
{
  std::filesystem::path binary;
  std::string compiler_version;
  std::string compiler_channel;
  std::string compiler_edition_max;
  std::string integration_phase;
  std::vector<int> supported_compile_plan_versions;
  std::vector<std::string> capabilities;
};

std::optional<std::filesystem::path> ResolveStyioBinary(const std::optional<std::string> &explicit_path);
CompatibilityReport CheckCompilerCompatibility(const std::filesystem::path &binary);

}  // namespace spio
