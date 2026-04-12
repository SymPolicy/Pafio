#include "SpioVendor/Vendor.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Version.hpp"
#include "SpioResolve/Resolver.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

struct VendoredSnapshot
{
  std::string source;
  std::string rev;
  std::string repo_hash;
  fs::path snapshot_root;
};

void CopySnapshotTree(const fs::path &source_root, const fs::path &destination_root)
{
  if (!fs::exists(source_root))
  {
    throw spio::VendorError("vendored source snapshot does not exist: " + source_root.string());
  }
  if (!fs::is_directory(source_root))
  {
    throw spio::VendorError("vendored source snapshot is not a directory: " + source_root.string());
  }

  std::error_code ignored;
  fs::remove_all(destination_root, ignored);
  fs::create_directories(destination_root.parent_path());
  fs::copy(source_root, destination_root, fs::copy_options::recursive);

  std::ofstream marker(destination_root / ".spio-snapshot-ready");
  if (!marker)
  {
    throw spio::VendorError("failed to write vendor ready marker: " + (destination_root / ".spio-snapshot-ready").string());
  }
  marker << "ready\n";
}

std::vector<VendoredSnapshot> CollectVendoredSnapshots(const spio::ResolvedGraphResult &graph)
{
  std::vector<VendoredSnapshot> snapshots;
  for (const spio::ResolvedPackage &package : graph.packages)
  {
    if (package.source_kind != "git")
    {
      continue;
    }
    if (!package.git.has_value() || !package.rev.has_value() || !package.repo_hash.has_value() || !package.snapshot_root.has_value())
    {
      throw spio::VendorError("resolved git package is missing snapshot metadata: " + package.id);
    }
    snapshots.push_back({
        .source = *package.git,
        .rev = *package.rev,
        .repo_hash = *package.repo_hash,
        .snapshot_root = *package.snapshot_root,
    });
  }

  std::sort(
      snapshots.begin(),
      snapshots.end(),
      [](const VendoredSnapshot &left, const VendoredSnapshot &right) {
        return std::tie(left.repo_hash, left.rev, left.source) < std::tie(right.repo_hash, right.rev, right.source);
      });
  snapshots.erase(
      std::unique(
          snapshots.begin(),
          snapshots.end(),
          [](const VendoredSnapshot &left, const VendoredSnapshot &right) {
            return left.repo_hash == right.repo_hash && left.rev == right.rev;
          }),
      snapshots.end());
  return snapshots;
}

}  // namespace

namespace spio
{

VendorResult WriteVendorTree(const VendorRequest &request)
{
  const fs::path manifest_path = CanonicalAbsolutePath(request.manifest_path);
  if (!fs::exists(manifest_path))
  {
    throw ValidationError("manifest not found: " + manifest_path.string());
  }

  const fs::path vendor_root = request.output_path.has_value()
                                   ? CanonicalAbsolutePath(*request.output_path)
                                   : ProjectVendorRootForManifest(manifest_path);
  ResolveOptions resolve_options;
  resolve_options.offline = request.offline;
  resolve_options.vendor_root = vendor_root;
  const ResolvedGraphResult graph = ResolveSingleVersionGraph(manifest_path, resolve_options);
  const std::vector<VendoredSnapshot> snapshots = CollectVendoredSnapshots(graph);

  fs::create_directories(vendor_root);
  fs::remove_all(vendor_root / "git");

  json records = json::array();
  for (const VendoredSnapshot &snapshot : snapshots)
  {
    const fs::path destination_root = vendor_root / "git" / snapshot.repo_hash / snapshot.rev;
    CopySnapshotTree(snapshot.snapshot_root, destination_root);
    records.push_back({
        {"source", snapshot.source},
        {"rev", snapshot.rev},
        {"repo_hash", snapshot.repo_hash},
        {"path", destination_root.string()},
    });
  }

  const fs::path metadata_path = vendor_root / "spio-vendor.json";
  const json metadata{
      {"tool", "spio"},
      {"version", std::string(kVersion)},
      {"manifest_path", manifest_path.string()},
      {"vendor_root", vendor_root.string()},
      {"git_snapshots", records},
  };
  std::ofstream out(metadata_path);
  if (!out)
  {
    throw VendorError("failed to open vendor metadata for write: " + metadata_path.string());
  }
  out << metadata.dump(2) << '\n';
  if (!out.good())
  {
    throw VendorError("failed to write vendor metadata: " + metadata_path.string());
  }

  return {
      .manifest_path = manifest_path,
      .vendor_root = vendor_root,
      .metadata_path = metadata_path,
      .package_count = graph.packages.size(),
      .git_snapshot_count = snapshots.size(),
  };
}

}  // namespace spio
