#include "PafioResolve/MetadataContract.hpp"

#include "PafioCore/Paths.hpp"
#include "PafioManifest/Manifest.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

json PackageSummary(const pafio::ResolvedPackage &package)
{
  return {
      {"id", package.id},
      {"manifest_path", package.manifest_path.string()},
      {"name", package.package.name},
      {"publish", package.package.publish},
      {"root", package.root_dir.string()},
      {"source_kind", package.source_kind},
      {"version", package.package.version},
      {"edition", package.package.edition},
  };
}

std::string DependencySourceKindName(
    const pafio::DependencySourceKind source_kind)
{
  switch (source_kind)
  {
    case pafio::DependencySourceKind::kPath:
      return "path";
    case pafio::DependencySourceKind::kGit:
      return "git";
    case pafio::DependencySourceKind::kRegistry:
      return "registry";
  }
  throw std::logic_error("unknown dependency source kind");
}

json DependencySource(const pafio::Dependency &dependency)
{
  return {
      {"kind", DependencySourceKindName(dependency.source_kind)},
      {"location", dependency.source},
      {"rev", dependency.rev.has_value() ? json(*dependency.rev) : json(nullptr)},
  };
}

}  // namespace

namespace pafio
{

MetadataDocument BuildMetadataDocument(
    const fs::path &manifest_path,
    const ResolvedGraphResult &graph)
{
  const fs::path canonical_manifest = CanonicalAbsolutePath(manifest_path);
  const ManifestDocument manifest = LoadManifest(canonical_manifest);

  json package = json::object();
  if (manifest.package.has_value())
  {
    const auto found = std::find_if(
        graph.packages.begin(),
        graph.packages.end(),
        [&](const ResolvedPackage &candidate) {
          return candidate.manifest_path == canonical_manifest;
        });
    if (found != graph.packages.end())
    {
      package = PackageSummary(*found);
    }
  }

  json workspace = {
      {"exclude", json::array()},
      {"manifest_path", canonical_manifest.string()},
      {"members", json::array()},
      {"resolver", "1"},
      {"root", canonical_manifest.parent_path().string()},
      {"root_package_ids", graph.root_ids},
  };
  if (manifest.workspace.has_value())
  {
    workspace["members"] = manifest.workspace->members;
    workspace["exclude"] = manifest.workspace->exclude;
    workspace["resolver"] = manifest.workspace->resolver;
  }

  std::vector<const ResolvedPackage *> ordered_packages;
  ordered_packages.reserve(graph.packages.size());
  for (const ResolvedPackage &resolved_package : graph.packages)
  {
    ordered_packages.push_back(&resolved_package);
  }
  std::sort(
      ordered_packages.begin(),
      ordered_packages.end(),
      [](const ResolvedPackage *left, const ResolvedPackage *right) {
        return left->id < right->id;
      });

  json dependencies = json::array();
  json targets = json::array();
  workspace["packages"] = json::array();
  for (const ResolvedPackage *resolved_package : ordered_packages)
  {
    workspace["packages"].push_back(PackageSummary(*resolved_package));

    std::map<std::string, std::string> package_id_by_alias;
    for (const ResolvedDependencyAlias &alias :
         resolved_package->dependency_aliases)
    {
      if (!package_id_by_alias.emplace(alias.alias, alias.package_id).second)
      {
        throw std::logic_error(
            "metadata graph contains duplicate dependency alias '" +
            alias.alias + "' for '" + resolved_package->id + "'");
      }
    }

    std::vector<std::pair<const Dependency *, std::string>>
        declared_dependencies;
    declared_dependencies.reserve(
        resolved_package->package.dependencies.size() +
        resolved_package->package.dev_dependencies.size());
    for (const Dependency &dependency :
         resolved_package->package.dependencies)
    {
      declared_dependencies.emplace_back(&dependency, "normal");
    }
    for (const Dependency &dependency :
         resolved_package->package.dev_dependencies)
    {
      declared_dependencies.emplace_back(&dependency, "dev");
    }
    std::sort(
        declared_dependencies.begin(),
        declared_dependencies.end(),
        [](const auto &left, const auto &right) {
          return std::tie(left.first->alias, left.second) <
                 std::tie(right.first->alias, right.second);
        });

    for (const auto &[dependency, kind] : declared_dependencies)
    {
      const auto resolved = package_id_by_alias.find(dependency->alias);
      if (resolved == package_id_by_alias.end())
      {
        throw std::logic_error(
            "metadata graph is missing dependency alias '" +
            dependency->alias + "' for '" + resolved_package->id + "'");
      }
      const auto target = std::find_if(
          ordered_packages.begin(),
          ordered_packages.end(),
          [&](const ResolvedPackage *candidate) {
            return candidate->id == resolved->second;
          });
      if (target == ordered_packages.end())
      {
        throw std::logic_error(
            "metadata graph dependency alias '" + dependency->alias +
            "' references unknown package id '" + resolved->second + "'");
      }

      dependencies.push_back({
          {"alias", dependency->alias},
          {"kind", kind},
          {"package", (*target)->package.name},
          {"package_id", resolved->second},
          {"parent_package_id", resolved_package->id},
          {"requirement",
           dependency->version.has_value()
               ? json(*dependency->version)
               : json(nullptr)},
          {"source", DependencySource(*dependency)},
      });
    }

    if (resolved_package->package.lib.has_value())
    {
      targets.push_back({
          {"kind", "lib"},
          {"name", resolved_package->package.name},
          {"package_id", resolved_package->id},
          {"path", CanonicalAbsolutePath(
                       resolved_package->root_dir /
                       resolved_package->package.lib->path)
                       .string()},
      });
    }
    std::vector<BinTarget> bins = resolved_package->package.bins;
    std::sort(
        bins.begin(),
        bins.end(),
        [](const BinTarget &left, const BinTarget &right) {
          return std::tie(left.name, left.path) <
                 std::tie(right.name, right.path);
        });
    for (const BinTarget &bin : bins)
    {
      targets.push_back({
          {"kind", "bin"},
          {"name", bin.name},
          {"package_id", resolved_package->id},
          {"path", CanonicalAbsolutePath(resolved_package->root_dir / bin.path)
                       .string()},
      });
    }
    std::vector<TestTarget> tests = resolved_package->package.tests;
    std::sort(
        tests.begin(),
        tests.end(),
        [](const TestTarget &left, const TestTarget &right) {
          return std::tie(left.name, left.path) <
                 std::tie(right.name, right.path);
        });
    for (const TestTarget &test : tests)
    {
      targets.push_back({
          {"kind", "test"},
          {"name", test.name},
          {"package_id", resolved_package->id},
          {"path", CanonicalAbsolutePath(
                       resolved_package->root_dir / test.path)
                       .string()},
      });
    }
  }

  const fs::path resolution_path =
      ProjectStateRootForManifest(canonical_manifest) / "resolution-v1.json";
  const fs::path vendor_root =
      ProjectVendorRootForManifest(canonical_manifest);
  const fs::path vendor_metadata_path = vendor_root / "pafio-vendor.json";

  MetadataDocument document;
  document.package = std::move(package);
  document.workspace = std::move(workspace);
  document.dependencies = std::move(dependencies);
  document.targets = std::move(targets);
  document.lock = {
      {"package_count", graph.lockfile.packages.size()},
      {"path", graph.lockfile_path.string()},
      {"present", fs::is_regular_file(graph.lockfile_path)},
      {"resolver", graph.lockfile.resolver},
  };
  document.resolution = {
      {"package_count", graph.packages.size()},
      {"path", resolution_path.string()},
      {"present", fs::is_regular_file(resolution_path)},
      {"root_package_ids", graph.root_ids},
      {"schema_version", 1},
  };
  document.vendor = {
      {"metadata_path", vendor_metadata_path.string()},
      {"present", fs::is_regular_file(vendor_metadata_path)},
      {"root", vendor_root.string()},
  };
  return document;
}

nlohmann::json SerializeMetadataV1(const MetadataDocument &document)
{
  return {
      {"package", document.package},
      {"workspace", document.workspace},
      {"dependencies", document.dependencies},
      {"targets", document.targets},
      {"lock", document.lock},
      {"resolution", document.resolution},
      {"vendor", document.vendor},
  };
}

}  // namespace pafio
