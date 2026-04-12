#pragma once

#include "SpioPublish/Publish.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spio
{

struct RegistryPublishRequest
{
  PublishRequest publish_request;
  std::filesystem::path registry_root;
};

struct HttpRegistryPublishRequest
{
  PublishRequest publish_request;
  std::string registry_root;
  std::vector<std::string> request_headers;
};

struct RegistryPublishResult
{
  PublishResult candidate;
  std::filesystem::path registry_root;
  std::filesystem::path registry_marker_path;
  std::filesystem::path registry_blob_path;
  std::filesystem::path registry_entry_path;
  std::string archive_sha256;
  uint64_t archive_size_bytes = 0;
  std::string published_at_utc;
};

struct HttpRegistryPublishResult
{
  PublishResult candidate;
  std::string registry_root;
  std::string registry_marker_url;
  std::string registry_blob_url;
  std::string registry_entry_url;
  std::string archive_sha256;
  uint64_t archive_size_bytes = 0;
  std::string published_at_utc;
};

RegistryPublishResult PublishToFilesystemRegistry(const RegistryPublishRequest &request);
HttpRegistryPublishResult PublishToHttpRegistry(const HttpRegistryPublishRequest &request);

}  // namespace spio
