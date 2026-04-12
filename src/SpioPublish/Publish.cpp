#include "SpioPublish/Publish.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioManifest/Manifest.hpp"
#include "SpioPack/Pack.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

struct PackageSelection
{
  fs::path manifest_path;
  fs::path package_root;
  spio::ManifestDocument manifest;
};

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

std::vector<PackageSelection> CollectPackageCandidates(const fs::path &root_manifest_path)
{
  const fs::path normalized_root_manifest = CanonicalAbsolutePath(root_manifest_path);
  const fs::path root_dir = normalized_root_manifest.parent_path();
  const spio::ManifestDocument root_manifest = spio::LoadManifest(normalized_root_manifest);

  std::vector<PackageSelection> candidates;
  if (root_manifest.package.has_value())
  {
    candidates.push_back({
        .manifest_path = normalized_root_manifest,
        .package_root = root_dir,
        .manifest = root_manifest,
    });
  }

  if (root_manifest.workspace.has_value())
  {
    for (const std::string &member : root_manifest.workspace->members)
    {
      const fs::path member_manifest_path = CanonicalAbsolutePath(root_dir / member / "spio.toml");
      const spio::ManifestDocument member_manifest = spio::LoadManifest(member_manifest_path);
      if (!member_manifest.package.has_value())
      {
        throw spio::WorkspaceError("workspace member manifest must define [package]: " + member_manifest_path.string());
      }
      candidates.push_back({
          .manifest_path = member_manifest_path,
          .package_root = member_manifest_path.parent_path(),
          .manifest = member_manifest,
      });
    }
  }

  return candidates;
}

PackageSelection SelectPackageForPublish(const spio::PublishRequest &request)
{
  const fs::path root_manifest_path = CanonicalAbsolutePath(request.manifest_path);
  const std::vector<PackageSelection> candidates = CollectPackageCandidates(root_manifest_path);
  if (candidates.empty())
  {
    throw spio::PublishError("publish requires a manifest with a local [package] target");
  }

  if (request.package_name.has_value())
  {
    std::vector<const PackageSelection *> matches;
    for (const PackageSelection &candidate : candidates)
    {
      if (candidate.manifest.package->name == *request.package_name)
      {
        matches.push_back(&candidate);
      }
    }
    if (matches.empty())
    {
      throw spio::PublishError("selected package was not found under the active manifest: " + *request.package_name);
    }
    if (matches.size() > 1U)
    {
      throw spio::PublishError("selected package is ambiguous under the active manifest: " + *request.package_name);
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

  throw spio::PublishError("workspace publish is ambiguous; select a root package with --package <namespace/name>");
}

void ValidatePublishCandidate(const PackageSelection &selection)
{
  const spio::PackageConfig &package = *selection.manifest.package;
  if (!package.publish)
  {
    throw spio::PublishError("selected package is marked publish = false: " + package.name);
  }

  auto validate_dependency_table = [&](const std::vector<spio::Dependency> &dependencies, const std::string &section_name) {
    for (const spio::Dependency &dependency : dependencies)
    {
      if (dependency.source_kind != spio::DependencySourceKind::kRegistry)
      {
        throw spio::PublishError(
            "published packages may only contain registry-addressable dependencies in [" + section_name + "]: " +
            package.name);
      }
      if (!dependency.package.has_value() || dependency.package->empty())
      {
        throw spio::PublishError(
            "registry dependency in [" + section_name + "] must include package = \"namespace/name\": " + package.name);
      }
      if (!dependency.version.has_value() || dependency.version->empty())
      {
        throw spio::PublishError(
            "registry dependency in [" + section_name + "] must include version = \"x.y.z\": " + package.name);
      }
      if (dependency.source.empty())
      {
        throw spio::PublishError(
            "registry dependency in [" + section_name + "] must include registry = \"<url>\": " + package.name);
      }
    }
  };

  validate_dependency_table(package.dependencies, "dependencies");
  validate_dependency_table(package.dev_dependencies, "dev-dependencies");
}

}  // namespace

namespace spio
{

PublishResult PreparePublishCandidate(const PublishRequest &request)
{
  const PackageSelection selection = SelectPackageForPublish(request);
  ValidatePublishCandidate(selection);

  const PackResult package_archive = WriteSourcePackage({
      .manifest_path = selection.manifest_path,
      .package_name = selection.manifest.package->name,
      .output_path = request.output_path,
  });

  return {
      .manifest_path = selection.manifest_path,
      .package_root = selection.package_root,
      .archive_path = package_archive.archive_path,
      .package_name = selection.manifest.package->name,
      .package_version = selection.manifest.package->version,
      .dependencies = selection.manifest.package->dependencies,
      .dev_dependencies = selection.manifest.package->dev_dependencies,
      .dependency_count = selection.manifest.package->dependencies.size(),
      .dev_dependency_count = selection.manifest.package->dev_dependencies.size(),
  };
}

}  // namespace spio
