#pragma once

#include "SpioManifest/Manifest.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace spio
{

struct PublishRequest
{
  std::filesystem::path manifest_path = "spio.toml";
  std::optional<std::string> package_name;
  std::optional<std::filesystem::path> output_path;
};

struct PublishResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path package_root;
  std::filesystem::path archive_path;
  std::string package_name;
  std::string package_version;
  std::vector<Dependency> dependencies;
  std::vector<Dependency> dev_dependencies;
  size_t dependency_count = 0;
  size_t dev_dependency_count = 0;
};

PublishResult PreparePublishCandidate(const PublishRequest &request);

}  // namespace spio
