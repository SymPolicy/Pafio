#pragma once

#include "PafioManifest/Manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pafio
{

struct PublishRequest
{
  std::filesystem::path manifest_path = "pafio.toml";
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

inline constexpr std::uintmax_t kMaxRemotePublishArchiveBytes = 64U * 1024U * 1024U;

struct RemotePublishArchive
{
  std::string name;
  std::string base64;
  std::string sha256;
  std::uintmax_t size_bytes = 0;
};

PublishResult PreparePublishCandidate(const PublishRequest &request);
RemotePublishArchive ReadRemotePublishArchive(
    const std::filesystem::path &archive_path,
    std::uintmax_t max_size_bytes = kMaxRemotePublishArchiveBytes);
std::string BuildRemotePublishRequestJson(
    const PublishResult &candidate,
    std::uintmax_t max_size_bytes = kMaxRemotePublishArchiveBytes);

}  // namespace pafio
