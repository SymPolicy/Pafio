#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace pafio
{

struct PackRequest
{
  std::filesystem::path manifest_path = "pafio.toml";
  std::optional<std::string> package_name;
  std::optional<std::filesystem::path> output_path;
};

struct PackResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path package_root;
  std::filesystem::path archive_path;
  std::string package_name;
  std::string package_version;
  std::string archive_prefix;
  size_t file_count = 0;
};

PackResult WriteSourcePackage(const PackRequest &request);

}  // namespace pafio
