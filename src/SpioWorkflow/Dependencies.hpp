#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace spio
{

enum class DependencySection
{
  kDependencies,
  kDevDependencies,
};

struct AddDependencyRequest
{
  std::filesystem::path manifest_path;
  std::string package_name;
  std::optional<std::string> alias;
  DependencySection section = DependencySection::kDependencies;
  bool use_git = false;
  std::string source;
  std::optional<std::string> rev;
};

struct RemoveDependencyRequest
{
  std::filesystem::path manifest_path;
  std::string target;
  std::optional<DependencySection> section;
};

struct DependencyCommandResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path lockfile_path;
  std::string alias;
  std::string package_name;
  DependencySection section = DependencySection::kDependencies;
  size_t package_count = 0;
};

struct FetchCommandResult
{
  std::filesystem::path manifest_path;
  size_t package_count = 0;
  size_t git_package_count = 0;
};

DependencyCommandResult AddDependencyAndRefreshLock(const AddDependencyRequest &request);
DependencyCommandResult RemoveDependencyAndRefreshLock(const RemoveDependencyRequest &request);
FetchCommandResult FetchDependencies(const std::filesystem::path &manifest_path);
std::string DependencySectionName(DependencySection section);

}  // namespace spio
