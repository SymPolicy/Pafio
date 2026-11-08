#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "PafioCore/Paths.hpp"
#include "PafioManifest/Manifest.hpp"

namespace pafio
{

// Workspace package selection shared by pack and publish: enumerate the root
// manifest's [package] plus workspace members, then pick the requested package
// (or the single/root candidate) with identical ambiguity rules. The error type
// stays caller-specific so each command keeps its own exception contract.
struct PackageSelection
{
  std::filesystem::path manifest_path;
  std::filesystem::path package_root;
  pafio::ManifestDocument manifest;
};

std::vector<PackageSelection> CollectPackageCandidates(const std::filesystem::path &root_manifest_path);

template <typename ErrorT>
PackageSelection SelectWorkspacePackage(
    const std::filesystem::path &manifest_path,
    const std::optional<std::string> &package_name,
    std::string_view verb)
{
  const std::filesystem::path root_manifest_path = pafio::CanonicalAbsolutePath(manifest_path);
  const std::vector<PackageSelection> candidates = CollectPackageCandidates(root_manifest_path);
  if (candidates.empty())
  {
    throw ErrorT(std::string(verb) + " requires a manifest with a local [package] target");
  }

  if (package_name.has_value())
  {
    std::vector<const PackageSelection *> matches;
    for (const PackageSelection &candidate : candidates)
    {
      if (candidate.manifest.package->name == *package_name)
      {
        matches.push_back(&candidate);
      }
    }
    if (matches.empty())
    {
      throw ErrorT("selected package was not found under the active manifest: " + *package_name);
    }
    if (matches.size() > 1U)
    {
      throw ErrorT("selected package is ambiguous under the active manifest: " + *package_name);
    }
    return *matches.front();
  }

  for (const PackageSelection &candidate : candidates)
  {
    if (candidate.manifest_path == root_manifest_path && candidate.manifest.package.has_value())
    {
      return candidate;
    }
  }

  if (candidates.size() == 1U)
  {
    return candidates.front();
  }

  throw ErrorT(
      "workspace " + std::string(verb) +
      " is ambiguous; select a root package with --package <namespace/name>");
}

}  // namespace pafio
