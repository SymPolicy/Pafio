#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pafio
{

struct RegistryMaterializationResult
{
  std::string registry_root;
  std::string package_name;
  std::string version;
  std::string sha256;
  std::filesystem::path snapshot_root;
};

RegistryMaterializationResult MaterializeRegistryPackage(
    const std::string &registry_root,
    const std::string &package_name,
    const std::string &version,
    bool offline,
    const std::optional<std::string> &locked_sha256 = std::nullopt);

}  // namespace pafio
