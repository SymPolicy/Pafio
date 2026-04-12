#pragma once

#include "SpioResolve/Resolver.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace spio
{

struct BuildPlanRequest
{
  std::filesystem::path manifest_path = "spio.toml";
  std::string intent = "build";
  std::optional<std::string> package_name;
  std::optional<std::string> bin_name;
  std::optional<std::string> test_name;
  bool select_lib = false;
  std::string profile = "dev";
  std::optional<std::string> compiler_version;
  bool offline = false;
  std::optional<std::filesystem::path> vendor_root;
};

struct BuildPlanResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path workspace_root;
  std::filesystem::path build_root;
  std::filesystem::path artifact_dir;
  std::filesystem::path diag_dir;
  std::filesystem::path plan_path;
  std::string cache_key;
  std::string plan_json;
  std::string entry_package_id;
  std::string entry_package_name;
  std::string entry_target_kind;
  std::string entry_target_name;
  std::string profile_name;
  size_t package_count = 0;
};

BuildPlanResult WriteBuildCompilePlan(const BuildPlanRequest &request);

}  // namespace spio
