#pragma once

#include "SpioManifest/Manifest.hpp"
#include "SpioManifest/Lockfile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace spio
{

struct ResolveOptions
{
  bool offline = false;
  std::optional<std::filesystem::path> vendor_root;
};

struct LockGenerationResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path lockfile_path;
  std::vector<std::string> root_ids;
  LockfileDocument lockfile;
};

struct ResolvedDependencyAlias
{
  std::string alias;
  std::string package_id;
};

struct ResolvedPackage
{
  std::filesystem::path manifest_path;
  std::filesystem::path root_dir;
  PackageConfig package;
  std::string id;
  std::string source_kind;
  std::optional<std::string> git;
  std::optional<std::string> rev;
  std::optional<std::string> registry;
  std::optional<std::string> sha256;
  std::optional<std::string> repo_hash;
  std::optional<std::filesystem::path> snapshot_root;
  std::vector<std::string> dependencies;
  std::vector<ResolvedDependencyAlias> dependency_aliases;
};

struct ResolvedGraphResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path lockfile_path;
  std::vector<std::string> root_ids;
  std::vector<ResolvedPackage> packages;
  LockfileDocument lockfile;
};

ResolvedGraphResult ResolveSingleVersionGraph(
    const std::filesystem::path &manifest_path,
    const ResolveOptions &options = {});
LockGenerationResult ResolveSingleVersionLockfile(
    const std::filesystem::path &manifest_path,
    const ResolveOptions &options = {});

}  // namespace spio
