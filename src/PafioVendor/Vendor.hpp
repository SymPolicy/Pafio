#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace pafio
{

struct VendorRequest
{
  std::filesystem::path manifest_path = "pafio.toml";
  std::optional<std::filesystem::path> output_path;
  bool offline = false;
};

struct VendorResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path vendor_root;
  std::filesystem::path metadata_path;
  size_t package_count = 0;
  size_t git_snapshot_count = 0;
};

VendorResult WriteVendorTree(const VendorRequest &request);

}  // namespace pafio
