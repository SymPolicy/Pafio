#include "PafioResolve/ResolutionContract.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

void ValidateDigest(const std::string_view digest, const std::string_view field)
{
  if (!pafio::IsRegistrySha256Digest(std::string(digest)))
  {
    throw pafio::ResolutionError(
        std::string(field) + " must be a lowercase sha256 hex digest");
  }
}

std::string CanonicalRoot(const fs::path &root, const std::string &package_id)
{
  if (!root.is_absolute())
  {
    throw pafio::ResolutionError(
        "resolution package root must be absolute for '" + package_id + "'");
  }

  const fs::path normalized = root.lexically_normal();
  std::error_code status_error;
  if (!fs::is_directory(normalized, status_error) || status_error)
  {
    throw pafio::ResolutionError(
        "resolution package root must be an existing directory for '" + package_id + "'");
  }
  return normalized.string();
}

}  // namespace

namespace pafio
{

std::string SerializeResolutionCanonical(
    const ResolvedGraphResult &graph,
    const std::string_view manifest_sha256,
    const std::string_view lock_sha256)
{
  ValidateDigest(manifest_sha256, "manifest_sha256");
  ValidateDigest(lock_sha256, "lock_sha256");

  std::vector<const ResolvedPackage *> packages;
  packages.reserve(graph.packages.size());
  std::unordered_set<std::string> package_ids;
  package_ids.reserve(graph.packages.size());
  for (const ResolvedPackage &package : graph.packages)
  {
    if (package.id.empty())
    {
      throw ResolutionError("resolution package id must not be empty");
    }
    if (!package_ids.insert(package.id).second)
    {
      throw ResolutionError("resolution contains duplicate package id: " + package.id);
    }
    packages.push_back(&package);
  }
  std::sort(
      packages.begin(),
      packages.end(),
      [](const ResolvedPackage *left, const ResolvedPackage *right) {
        return left->id < right->id;
      });

  std::vector<std::string> roots = graph.root_ids;
  std::sort(roots.begin(), roots.end());
  if (std::adjacent_find(roots.begin(), roots.end()) != roots.end())
  {
    throw ResolutionError("resolution contains duplicate root package id");
  }
  for (const std::string &root_id : roots)
  {
    if (!package_ids.contains(root_id))
    {
      throw ResolutionError("resolution root references unknown package id: " + root_id);
    }
  }

  json package_values = json::array();
  for (const ResolvedPackage *package : packages)
  {
    std::vector<ResolvedDependencyAlias> aliases = package->dependency_aliases;
    std::sort(
        aliases.begin(),
        aliases.end(),
        [](const ResolvedDependencyAlias &left, const ResolvedDependencyAlias &right) {
          return left.alias < right.alias;
        });

    json dependency_values = json::array();
    std::string previous_alias;
    bool has_previous_alias = false;
    for (const ResolvedDependencyAlias &dependency : aliases)
    {
      if (dependency.alias.empty())
      {
        throw ResolutionError(
            "resolution dependency alias must not be empty for '" + package->id + "'");
      }
      if (has_previous_alias && dependency.alias == previous_alias)
      {
        throw ResolutionError(
            "resolution contains duplicate dependency alias '" + dependency.alias +
            "' for '" + package->id + "'");
      }
      if (!package_ids.contains(dependency.package_id))
      {
        throw ResolutionError(
            "resolution dependency alias '" + dependency.alias +
            "' references unknown package id: " + dependency.package_id);
      }
      dependency_values.push_back({
          {"alias", dependency.alias},
          {"package_id", dependency.package_id},
      });
      previous_alias = dependency.alias;
      has_previous_alias = true;
    }

    json content_sha256 = nullptr;
    if (package->source_kind == "registry")
    {
      if (!package->sha256.has_value())
      {
        throw ResolutionError(
            "registry resolution package is missing content sha256: " + package->id);
      }
      ValidateDigest(*package->sha256, "registry content_sha256");
      const std::string digest_suffix = "#" + *package->sha256;
      if (!package->id.ends_with(digest_suffix))
      {
        throw ResolutionError(
            "registry resolution package id does not match content sha256: " + package->id);
      }
      content_sha256 = *package->sha256;
    }
    else if (package->sha256.has_value())
    {
      throw ResolutionError(
          "mutable resolution package must not declare content sha256: " + package->id);
    }

    package_values.push_back({
        {"content_sha256", std::move(content_sha256)},
        {"dependencies", std::move(dependency_values)},
        {"id", package->id},
        {"root", CanonicalRoot(package->root_dir, package->id)},
    });
  }

  const json document = {
      {"lock_sha256", std::string(lock_sha256)},
      {"manifest_sha256", std::string(manifest_sha256)},
      {"packages", std::move(package_values)},
      {"roots", std::move(roots)},
      {"schema_version", 1},
  };
  return document.dump(2) + '\n';
}

}  // namespace pafio
