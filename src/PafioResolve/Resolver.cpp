#include "PafioResolve/Resolver.hpp"

#include "PafioCore/ContractLexicon.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/FnvHash.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Process.hpp"
#include "PafioCore/ToolPaths.hpp"
#include "PafioCore/Version.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioRegistryClient/Client.hpp"
#include "PafioSecurity/PrescanArchive.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
namespace fs = std::filesystem;

namespace
{

enum class SourceKind
{
  kWorkspace,
  kPath,
  kGit,
  kRegistry,
};

struct SourceOrigin
{
  SourceKind kind = SourceKind::kPath;
  std::optional<std::string> git_source;
  std::optional<std::string> git_rev;
  std::optional<std::string> registry_root;
  std::optional<std::string> registry_version;
  std::optional<std::string> registry_sha256;
  std::optional<std::string> repo_hash;
  std::optional<fs::path> snapshot_root;
};

struct ManifestSelection
{
  fs::path manifest_path;
  SourceOrigin origin;
};

std::string PathKey(const fs::path &path)
{
  return pafio::CanonicalAbsolutePath(path).generic_string();
}

bool LooksLikeRemoteGitSource(const std::string &source)
{
  return source.find("://") != std::string::npos || source.starts_with("git@");
}

bool PathIsWithin(const fs::path &candidate, const fs::path &root)
{
  const fs::path relative = pafio::CanonicalAbsolutePath(candidate).lexically_relative(pafio::CanonicalAbsolutePath(root));
  if (relative.empty())
  {
    return false;
  }
  const std::string text = relative.generic_string();
  return text != ".." && !text.starts_with("../");
}

std::string NormalizeGitSource(const std::string &source, const fs::path &package_dir)
{
  if (LooksLikeRemoteGitSource(source))
  {
    return source;
  }

  const fs::path source_path(std::u8string(source.begin(), source.end()));
  fs::path normalized = pafio::CanonicalAbsolutePath(source_path.is_absolute() ? source_path : package_dir / source_path);
  std::string value = pafio::ProcessPathString(normalized);
#if defined(_WIN32)
  std::replace(value.begin(), value.end(), '\\', '/');
#endif
  return value;
}

std::string SourceKindString(SourceKind kind)
{
  switch (kind)
  {
    case SourceKind::kWorkspace:
      return "workspace";
    case SourceKind::kPath:
      return "path";
    case SourceKind::kGit:
      return "git";
    case SourceKind::kRegistry:
      return "registry";
  }
  return "path";
}

std::string RegistryLockHintKey(
    const std::string &registry_root,
    const std::string &package_name,
    const std::string &version)
{
  return pafio::NormalizeRegistryRoot(registry_root) + '\n' + package_name + '\n' + version;
}

std::string SnapshotRelativeKey(const SourceOrigin &origin, const fs::path &package_dir)
{
  const fs::path snapshot_root = pafio::CanonicalAbsolutePath(origin.snapshot_root.value());
  const fs::path relative = pafio::CanonicalAbsolutePath(package_dir).lexically_relative(snapshot_root);
  const std::string key = relative.generic_string();
  return key.empty() ? "." : key;
}

std::string BuildSourceFingerprint(const SourceOrigin &origin, const fs::path &package_dir)
{
  if (origin.kind == SourceKind::kGit)
  {
    return "git:" + origin.git_source.value() + "#" + origin.git_rev.value() + ":" + SnapshotRelativeKey(origin, package_dir);
  }
  if (origin.kind == SourceKind::kRegistry)
  {
    return "registry:" + origin.registry_root.value() + "#" + origin.registry_sha256.value() + ":" + SnapshotRelativeKey(origin, package_dir);
  }
  return SourceKindString(origin.kind) + ":" + PathKey(package_dir);
}

std::string BuildLockId(const SourceOrigin &origin, const pafio::PackageConfig &package)
{
  std::string id = SourceKindString(origin.kind) + ":" + package.name + "@" + package.version;
  if (origin.kind == SourceKind::kGit)
  {
    id += "#" + origin.git_rev.value();
  }
  else if (origin.kind == SourceKind::kRegistry)
  {
    id += "#" + origin.registry_sha256.value();
  }
  return id;
}

std::vector<pafio::Dependency> CollectDependencies(const pafio::PackageConfig &package)
{
  std::vector<pafio::Dependency> dependencies;
  dependencies.reserve(package.dependencies.size() + package.dev_dependencies.size());
  dependencies.insert(dependencies.end(), package.dependencies.begin(), package.dependencies.end());
  dependencies.insert(dependencies.end(), package.dev_dependencies.begin(), package.dev_dependencies.end());
  std::sort(
      dependencies.begin(),
      dependencies.end(),
      [](const pafio::Dependency &left, const pafio::Dependency &right) {
        return left.alias < right.alias;
      });
  return dependencies;
}

class GitSourceCache
{
public:
  explicit GitSourceCache(const fs::path &pafio_home, const bool offline, std::optional<fs::path> vendor_root)
      : pafio_home_(pafio::CanonicalAbsolutePath(pafio_home)),
        offline_(offline)
  {
    if (vendor_root.has_value())
    {
      vendor_root_ = pafio::CanonicalAbsolutePath(*vendor_root);
    }
    fs::create_directories(pafio_home_ / "git" / "repos");
    fs::create_directories(pafio_home_ / "git" / "checkouts");
  }

  SourceOrigin Materialize(const std::string &normalized_source, const std::string &rev)
  {
    pafio::RequireGitCommitId(rev, "git dependency '" + normalized_source + "' rev");
    const std::string repo_hash = pafio::Hex64(pafio::Fnv1a64(normalized_source));
    if (const std::optional<fs::path> vendored_snapshot = FindVendoredSnapshot(repo_hash, rev); vendored_snapshot.has_value())
    {
      return SourceOrigin{
          .kind = SourceKind::kGit,
          .git_source = normalized_source,
          .git_rev = rev,
          .repo_hash = repo_hash,
          .snapshot_root = *vendored_snapshot,
      };
    }

    const fs::path repo_dir = pafio_home_ / "git" / "repos" / (repo_hash + ".git");
    if (!fs::exists(repo_dir))
    {
      if (offline_)
      {
        throw pafio::FetchError(
            "offline mode requires a vendored snapshot or cached git mirror for '" + normalized_source + "'");
      }
      EnsureMirror(normalized_source, repo_dir);
    }
    if (!HasRevision(repo_dir, rev))
    {
      if (offline_)
      {
        throw pafio::FetchError(
            "offline mode is missing git rev '" + rev + "' in the local cache for '" + normalized_source + "'");
      }
      FetchOrigin(repo_dir);
      if (!HasRevision(repo_dir, rev))
      {
        throw pafio::FetchError("git source does not contain requested rev '" + rev + "': " + normalized_source);
      }
    }

    const fs::path snapshot_root = pafio_home_ / "git" / "checkouts" / repo_hash / rev;
    EnsureSnapshot(repo_dir, snapshot_root, rev);
    return SourceOrigin{
        .kind = SourceKind::kGit,
        .git_source = normalized_source,
        .git_rev = rev,
        .repo_hash = repo_hash,
        .snapshot_root = snapshot_root,
    };
  }

private:
  std::optional<fs::path> FindVendoredSnapshot(const std::string &repo_hash, const std::string &rev) const
  {
    if (!vendor_root_.has_value())
    {
      return std::nullopt;
    }

    const fs::path snapshot_root = *vendor_root_ / "git" / repo_hash / rev;
    if (!fs::exists(snapshot_root))
    {
      return std::nullopt;
    }

    const fs::path ready_marker = snapshot_root / ".pafio-snapshot-ready";
    if (fs::exists(ready_marker) || fs::exists(snapshot_root / "pafio.toml"))
    {
      return pafio::CanonicalAbsolutePath(snapshot_root);
    }
    return std::nullopt;
  }

  void EnsureMirror(const std::string &normalized_source, const fs::path &repo_dir) const
  {
    if (fs::exists(repo_dir))
    {
      return;
    }

    fs::create_directories(repo_dir.parent_path());
    const pafio::ProcessResult result = pafio::RunProcess<pafio::CacheError>({
        .program = pafio::ResolvedGitPath(),
        .args = {"clone", "--mirror", normalized_source, pafio::ProcessPathString(repo_dir)},
        .search_path = false,
        .timeout = std::nullopt,
        .error_context = "resolver process",
    });
    if (result.exit_code != 0)
    {
      throw pafio::FetchError(
          "failed to clone git source '" + normalized_source + "': " +
          pafio::DescribeProcessFailure(result));
    }
  }

  bool HasRevision(const fs::path &repo_dir, const std::string &rev) const
  {
    // The rev must name a commit object itself, not merely peel to one: an
    // annotated tag object id would satisfy rev^{commit} while recording a
    // rev that differs from the materialized code.
    const pafio::ProcessResult result = pafio::RunProcess<pafio::CacheError>({
        .program = pafio::ResolvedGitPath(),
        .args = {"--git-dir", pafio::ProcessPathString(repo_dir), "cat-file", "-t", rev},
        .search_path = false,
        .timeout = pafio::kExternalProcessProbeTimeout,
        .error_context = "resolver process",
    });
    if (result.timed_out)
    {
      throw pafio::FetchError("failed to check git rev '" + rev + "': " + pafio::DescribeProcessFailure(result));
    }
    return result.exit_code == 0 && result.stdout_text == "commit\n";
  }

  void FetchOrigin(const fs::path &repo_dir) const
  {
    const pafio::ProcessResult result = pafio::RunProcess<pafio::CacheError>({
        .program = pafio::ResolvedGitPath(),
        .args = {"--git-dir", pafio::ProcessPathString(repo_dir), "fetch", "--prune", "origin"},
        .search_path = false,
        .timeout = std::nullopt,
        .error_context = "resolver process",
    });
    if (result.exit_code != 0)
    {
      throw pafio::FetchError(
          "failed to fetch git source cache '" + repo_dir.string() + "': " +
          pafio::DescribeProcessFailure(result));
    }
  }

  void EnsureSnapshot(const fs::path &repo_dir, const fs::path &snapshot_root, const std::string &rev) const
  {
    const fs::path ready_marker = snapshot_root / ".pafio-snapshot-ready";
    if (fs::exists(ready_marker))
    {
      return;
    }

    fs::remove_all(snapshot_root);
    fs::create_directories(snapshot_root);
    const fs::path archive_path = snapshot_root.parent_path() / (pafio::Hex64(pafio::Fnv1a64(rev)) + ".tar");
    const pafio::ProcessResult archive = pafio::RunProcess<pafio::CacheError>({
        .program = pafio::ResolvedGitPath(),
        .args = {
            "--git-dir",
            pafio::ProcessPathString(repo_dir),
            "archive",
            "--format=tar",
            "--output",
            pafio::ProcessPathString(archive_path),
            rev},
        .search_path = false,
        .timeout = pafio::kExternalProcessStepTimeout,
        .error_context = "resolver process",
    });
    if (archive.exit_code != 0)
    {
      std::error_code ignored;
      fs::remove(archive_path, ignored);
      throw pafio::FetchError(
          "failed to archive git rev '" + rev + "': " +
          pafio::DescribeProcessFailure(archive));
    }

    const pafio::ProcessResult path_listing = pafio::RunProcess<pafio::CacheError>({
        .program = pafio::ResolvedTarPath(),
        .args = {"-tf", pafio::ProcessPathString(archive_path)},
        .search_path = false,
        .timeout = pafio::kExternalProcessStepTimeout,
        .max_stdout_bytes = pafio::kArchiveListingMaxBytes,
        .error_context = "resolver process",
    });
    if (path_listing.exit_code != 0)
    {
      std::error_code ignored;
      fs::remove(archive_path, ignored);
      throw pafio::CacheError(
          "failed to list git archive '" + archive_path.string() + "': " +
          pafio::DescribeProcessFailure(path_listing));
    }
    const pafio::ProcessResult verbose_listing = pafio::RunProcess<pafio::CacheError>({
        .program = pafio::ResolvedTarPath(),
        .args = {"-tvf", pafio::ProcessPathString(archive_path)},
        .search_path = false,
        .timeout = pafio::kExternalProcessStepTimeout,
        .max_stdout_bytes = pafio::kArchiveListingMaxBytes,
        .error_context = "resolver process",
    });
    if (verbose_listing.exit_code != 0)
    {
      std::error_code ignored;
      fs::remove(archive_path, ignored);
      throw pafio::CacheError(
          "failed to inspect git archive '" + archive_path.string() + "': " +
          pafio::DescribeProcessFailure(verbose_listing));
    }
    const pafio::ArchivePrescanResult prescan = pafio::PrescanArchiveListing(
        path_listing.stdout_text,
        verbose_listing.stdout_text,
        path_listing.stdout_truncated || verbose_listing.stdout_truncated,
        false);
    if (!prescan.ok)
    {
      std::error_code ignored;
      fs::remove(archive_path, ignored);
      throw pafio::CacheError(
          std::string("git archive prescan failed") +
          (prescan.error_code.empty() ? "" : " (" + prescan.error_code + ")") + ": " + prescan.error);
    }

    const pafio::ProcessResult extract = pafio::RunProcess<pafio::CacheError>({
        .program = pafio::ResolvedTarPath(),
        .args = {"-xf", pafio::ProcessPathString(archive_path), "-C", pafio::ProcessPathString(snapshot_root)},
        .search_path = false,
        .timeout = pafio::kExternalProcessStepTimeout,
        .error_context = "resolver process",
    });
    fs::remove(archive_path);
    if (extract.exit_code != 0)
    {
      throw pafio::CacheError(
          "failed to extract git snapshot '" + snapshot_root.string() + "': " +
          pafio::DescribeProcessFailure(extract));
    }

    std::ofstream marker(ready_marker);
    marker << "ready\n";
  }

  fs::path pafio_home_;
  bool offline_ = false;
  std::optional<fs::path> vendor_root_;
};

class SingleVersionResolver
{
public:
  explicit SingleVersionResolver(const fs::path &manifest_path, const pafio::ResolveOptions &options)
      : root_manifest_path_(pafio::CanonicalAbsolutePath(manifest_path)),
        root_manifest_(pafio::LoadManifest(root_manifest_path_)),
        options_(NormalizeOptions(root_manifest_path_, options)),
        git_cache_(pafio::ResolvePafioHome(), options_.offline, options_.vendor_root)
  {
    LoadLockedRegistryHints();
    BuildRootSeeds();
  }

  pafio::LockGenerationResult Resolve()
  {
    const pafio::ResolvedGraphResult graph = ResolveGraph();
    return {
        .manifest_path = graph.manifest_path,
        .lockfile_path = graph.lockfile_path,
        .root_ids = graph.root_ids,
        .lockfile = graph.lockfile,
    };
  }

  pafio::ResolvedGraphResult ResolveGraph()
  {
    std::vector<std::string> root_ids;
    for (const ManifestSelection &seed : root_seeds_)
    {
      const size_t root_index = ResolveNode(seed.manifest_path, seed.origin, {});
      root_ids.push_back(nodes_.at(root_index).id);
    }
    std::sort(root_ids.begin(), root_ids.end());
    root_ids.erase(std::unique(root_ids.begin(), root_ids.end()), root_ids.end());

    std::vector<pafio::ResolvedPackage> packages;
    packages.reserve(nodes_.size());
    for (const Node &node : nodes_)
    {
      packages.push_back({
          .manifest_path = node.manifest_path,
          .root_dir = node.package_dir,
          .package = node.package,
          .id = node.id,
          .source_kind = SourceKindString(node.origin.kind),
          .git = node.origin.git_source,
          .rev = node.origin.git_rev,
          .registry = node.origin.registry_root,
          .sha256 = node.origin.registry_sha256,
          .repo_hash = node.origin.repo_hash,
          .snapshot_root = node.origin.snapshot_root,
          .dependencies = node.dependencies,
          .dependency_aliases = node.dependency_aliases,
      });
    }
    std::sort(
        packages.begin(),
        packages.end(),
        [](const pafio::ResolvedPackage &left, const pafio::ResolvedPackage &right) {
          return left.id < right.id;
        });

    pafio::LockfileDocument lockfile;
    lockfile.generated_by = std::string("pafio ") + std::string(pafio::kVersion);
    lockfile.resolver = "single-version-v1";
    lockfile.packages.reserve(packages.size());
    for (const pafio::ResolvedPackage &package : packages)
    {
      lockfile.packages.push_back({
          .id = package.id,
          .name = package.package.name,
          .version = package.package.version,
          .source_kind = package.source_kind,
          .git = package.git,
          .rev = package.rev,
          .registry = package.registry,
          .sha256 = package.sha256,
          .dependencies = package.dependencies,
      });
    }

    return {
        .manifest_path = root_manifest_path_,
        .lockfile_path = root_manifest_path_.parent_path() / "pafio.lock",
        .root_ids = std::move(root_ids),
        .packages = std::move(packages),
        .lockfile = std::move(lockfile),
    };
  }

private:
  static pafio::ResolveOptions NormalizeOptions(const fs::path &manifest_path, const pafio::ResolveOptions &options)
  {
    pafio::ResolveOptions normalized = options;
    if (!normalized.vendor_root.has_value())
    {
      const fs::path default_vendor_root = pafio::ProjectVendorRootForManifest(manifest_path);
      if (fs::exists(default_vendor_root))
      {
        normalized.vendor_root = default_vendor_root;
      }
    }
    else
    {
      normalized.vendor_root = pafio::CanonicalAbsolutePath(*normalized.vendor_root);
    }
    return normalized;
  }

  struct Node
  {
    fs::path manifest_path;
    fs::path package_dir;
    pafio::PackageConfig package;
    SourceOrigin origin;
    std::string source_fingerprint;
    std::string id;
    std::vector<std::string> dependencies;
    std::vector<pafio::ResolvedDependencyAlias> dependency_aliases;
  };

  void LoadLockedRegistryHints()
  {
    const fs::path lockfile_path = root_manifest_path_.parent_path() / "pafio.lock";
    if (!fs::exists(lockfile_path))
    {
      return;
    }

    pafio::LockfileDocument lockfile;
    try
    {
      lockfile = pafio::LoadLockfile(lockfile_path);
    }
    catch (const pafio::ValidationError &)
    {
      if (options_.locked)
      {
        throw;
      }
      return;
    }

    std::unordered_set<std::string> ambiguous_keys;
    for (const pafio::LockPackage &package : lockfile.packages)
    {
      if (package.source_kind != "registry" ||
          !package.registry.has_value() ||
          !package.sha256.has_value())
      {
        continue;
      }
      const std::string key =
          RegistryLockHintKey(*package.registry, package.name, package.version);
      if (ambiguous_keys.contains(key))
      {
        continue;
      }
      const auto [existing, inserted] =
          locked_registry_digests_.emplace(key, *package.sha256);
      if (!inserted && existing->second != *package.sha256)
      {
        locked_registry_digests_.erase(existing);
        ambiguous_keys.insert(key);
      }
    }
  }

  void BuildRootSeeds()
  {
    const SourceOrigin workspace_origin{.kind = SourceKind::kWorkspace};
    if (root_manifest_.package.has_value())
    {
      root_seeds_.push_back({.manifest_path = root_manifest_path_, .origin = workspace_origin});
      top_level_workspace_dirs_.insert(PathKey(root_manifest_path_.parent_path()));
    }

    if (!root_manifest_.workspace.has_value())
    {
      return;
    }

    const std::set<std::string> excluded(
        root_manifest_.workspace->exclude.begin(),
        root_manifest_.workspace->exclude.end());
    std::vector<std::string> members = root_manifest_.workspace->members;
    std::sort(members.begin(), members.end());
    for (const std::string &member : members)
    {
      if (excluded.contains(member))
      {
        throw pafio::WorkspaceError("workspace member is also excluded: " + member);
      }

      const fs::path member_manifest = pafio::CanonicalAbsolutePath(root_manifest_path_.parent_path() / member / "pafio.toml");
      if (!fs::exists(member_manifest))
      {
        throw pafio::WorkspaceError("workspace member manifest not found: " + member_manifest.string());
      }
      const pafio::ManifestDocument member_doc = pafio::LoadManifest(member_manifest);
      if (!member_doc.package.has_value())
      {
        throw pafio::WorkspaceError("workspace member manifest must define [package]: " + member_manifest.string());
      }

      root_seeds_.push_back({.manifest_path = member_manifest, .origin = workspace_origin});
      top_level_workspace_dirs_.insert(PathKey(member_manifest.parent_path()));
    }

    std::sort(
        root_seeds_.begin(),
        root_seeds_.end(),
        [](const ManifestSelection &left, const ManifestSelection &right) {
          return left.manifest_path < right.manifest_path;
        });
    root_seeds_.erase(
        std::unique(
            root_seeds_.begin(),
            root_seeds_.end(),
            [](const ManifestSelection &left, const ManifestSelection &right) {
              return left.manifest_path == right.manifest_path;
            }),
        root_seeds_.end());
  }

  ManifestSelection SelectPackageManifestFromRoot(
      const fs::path &root_manifest_path,
      const SourceOrigin &origin_hint,
      const std::optional<std::string> &expected_package)
  {
    const pafio::ManifestDocument root_doc = root_manifest_path == root_manifest_path_ ? root_manifest_ : pafio::LoadManifest(root_manifest_path);
    std::vector<fs::path> matches;

    if (root_doc.package.has_value())
    {
      if (!expected_package.has_value() || root_doc.package->name == *expected_package)
      {
        matches.push_back(root_manifest_path);
      }
    }

    if (root_doc.workspace.has_value())
    {
      if (!root_doc.package.has_value() && !expected_package.has_value())
      {
        throw pafio::ResolutionError("dependency target is a workspace root and requires an explicit package: " + root_manifest_path.string());
      }

      if (expected_package.has_value())
      {
        const std::set<std::string> excluded(root_doc.workspace->exclude.begin(), root_doc.workspace->exclude.end());
        std::vector<std::string> members = root_doc.workspace->members;
        std::sort(members.begin(), members.end());
        for (const std::string &member : members)
        {
          if (excluded.contains(member))
          {
            continue;
          }
          const fs::path member_manifest = pafio::CanonicalAbsolutePath(root_manifest_path.parent_path() / member / "pafio.toml");
          if (origin_hint.snapshot_root.has_value() &&
              !PathIsWithin(member_manifest, origin_hint.snapshot_root.value()))
          {
            throw pafio::ResolutionError(
                "workspace member escapes its materialized source snapshot: " + member_manifest.string());
          }
          if (!fs::exists(member_manifest))
          {
            throw pafio::ResolutionError("dependency workspace member manifest not found: " + member_manifest.string());
          }
          const pafio::ManifestDocument member_doc = pafio::LoadManifest(member_manifest);
          if (!member_doc.package.has_value())
          {
            throw pafio::ResolutionError("dependency workspace member manifest must define [package]: " + member_manifest.string());
          }
          if (member_doc.package->name == *expected_package)
          {
            matches.push_back(member_manifest);
          }
        }
      }
    }

    if (matches.empty())
    {
      if (expected_package.has_value())
      {
        throw pafio::ResolutionError(
            "dependency target package '" + *expected_package + "' was not found under " + root_manifest_path.string());
      }
      throw pafio::ResolutionError("dependency target manifest does not define [package]: " + root_manifest_path.string());
    }
    if (matches.size() > 1)
    {
      throw pafio::ResolutionError("dependency target package is ambiguous under " + root_manifest_path.string());
    }

    SourceOrigin selected_origin = origin_hint;
    if (origin_hint.kind == SourceKind::kWorkspace || origin_hint.kind == SourceKind::kPath)
    {
      const std::string selected_dir = PathKey(matches.front().parent_path());
      selected_origin.kind = top_level_workspace_dirs_.contains(selected_dir) ? SourceKind::kWorkspace : SourceKind::kPath;
    }

    return {
        .manifest_path = matches.front(),
        .origin = selected_origin,
    };
  }

  ManifestSelection ResolvePathDependency(const Node &parent, const pafio::Dependency &dependency)
  {
    const fs::path dependency_root = pafio::CanonicalAbsolutePath(parent.package_dir / dependency.source / "pafio.toml");
    if (!fs::exists(dependency_root))
    {
      throw pafio::ResolutionError("path dependency manifest not found: " + dependency_root.string());
    }

    if (parent.origin.snapshot_root.has_value())
    {
      if (!PathIsWithin(dependency_root.parent_path(), parent.origin.snapshot_root.value()) &&
          pafio::CanonicalAbsolutePath(dependency_root.parent_path()) != pafio::CanonicalAbsolutePath(parent.origin.snapshot_root.value()))
      {
        throw pafio::ResolutionError(
            "path dependency escapes its materialized source snapshot: " + dependency_root.string());
      }
      return SelectPackageManifestFromRoot(dependency_root, parent.origin, dependency.package);
    }

    return SelectPackageManifestFromRoot(dependency_root, SourceOrigin{.kind = SourceKind::kPath}, dependency.package);
  }

  ManifestSelection ResolveGitDependency(const Node &parent, const pafio::Dependency &dependency)
  {
    const std::string normalized_source = NormalizeGitSource(dependency.source, parent.package_dir);
    const SourceOrigin git_origin = git_cache_.Materialize(normalized_source, dependency.rev.value());
    const fs::path root_manifest = git_origin.snapshot_root.value() / "pafio.toml";
    if (!fs::exists(root_manifest))
    {
      throw pafio::ResolutionError("git dependency snapshot does not contain pafio.toml: " + root_manifest.string());
    }
    return SelectPackageManifestFromRoot(root_manifest, git_origin, dependency.package);
  }

  ManifestSelection ResolveRegistryDependency(const Node &parent, const pafio::Dependency &dependency)
  {
    (void) parent;
    if (!dependency.package.has_value())
    {
      throw pafio::ResolutionError("registry dependency '" + dependency.alias + "' must declare package = \"namespace/name\"");
    }
    if (!dependency.version.has_value())
    {
      throw pafio::ResolutionError(
          "registry dependency '" + dependency.alias +
          "' must declare version = \"x.y.z\" (exact semver only; ranges are not supported in single-version-v1)");
    }

    const std::string hint_key =
        RegistryLockHintKey(dependency.source, *dependency.package, *dependency.version);
    std::optional<std::string> locked_sha256;
    if (const auto hint = locked_registry_digests_.find(hint_key);
        hint != locked_registry_digests_.end())
    {
      locked_sha256 = hint->second;
    }
    if (options_.locked && !locked_sha256.has_value())
    {
      throw pafio::ResolutionError(
          "locked registry dependency has no matching package digest in pafio.lock: " +
          *dependency.package + "@" + *dependency.version);
    }

    const std::string materialize_key = dependency.source + '\x1f' + *dependency.package + '\x1f' +
        *dependency.version + '\x1f' + (options_.offline ? "1" : "0") + '\x1f' +
        locked_sha256.value_or("");
    const auto memoized = materialized_registry_.find(materialize_key);
    const pafio::RegistryMaterializationResult materialized =
        memoized != materialized_registry_.end()
            ? memoized->second
            : materialized_registry_
                  .emplace(
                      materialize_key,
                      pafio::MaterializeRegistryPackage(
                          dependency.source,
                          *dependency.package,
                          *dependency.version,
                          options_.offline,
                          locked_sha256))
                  .first->second;
    const SourceOrigin registry_origin{
        .kind = SourceKind::kRegistry,
        .registry_root = materialized.registry_root,
        .registry_version = materialized.version,
        .registry_sha256 = materialized.sha256,
        .snapshot_root = materialized.snapshot_root,
    };
    const fs::path root_manifest = materialized.snapshot_root / "pafio.toml";
    if (!fs::exists(root_manifest))
    {
      throw pafio::ResolutionError("registry dependency snapshot does not contain pafio.toml: " + root_manifest.string());
    }
    return SelectPackageManifestFromRoot(root_manifest, registry_origin, dependency.package);
  }

  enum class VisitState
  {
    kActive,
    kDone,
  };

  static std::string JoinRequirementChain(const std::vector<std::string> &chain)
  {
    std::ostringstream out;
    for (size_t i = 0; i < chain.size(); ++i)
    {
      if (i != 0)
      {
        out << " -> ";
      }
      out << chain[i];
    }
    return out.str();
  }

  std::string FormatCyclePath(size_t cycle_index) const
  {
    const auto cycle_start = std::find(active_stack_.begin(), active_stack_.end(), cycle_index);
    std::vector<std::string> cycle_ids;
    for (auto it = cycle_start; it != active_stack_.end(); ++it)
    {
      cycle_ids.push_back(nodes_.at(*it).id);
    }
    cycle_ids.push_back(nodes_.at(cycle_index).id);
    return JoinRequirementChain(cycle_ids);
  }

  [[noreturn]] void ThrowVersionConflict(
      const Node &other,
      const Node &candidate,
      const std::vector<std::string> &candidate_chain) const
  {
    const std::vector<std::string> &other_chain = path_ids_by_index_.at(node_by_package_name_.at(other.package.name));
    throw pafio::ResolutionError(
        "single-version-v1 conflict for package '" + candidate.package.name + "': versions '" +
        other.package.version + "' and '" + candidate.package.version + "'\n"
        "  required by: " +
        JoinRequirementChain(other_chain) +
        "\n"
        "  required by: " +
        JoinRequirementChain(candidate_chain));
  }

  [[noreturn]] void ThrowSourceConflict(
      const Node &other,
      const Node &candidate,
      const std::vector<std::string> &candidate_chain) const
  {
    const std::vector<std::string> &other_chain = path_ids_by_index_.at(node_by_package_name_.at(other.package.name));
    throw pafio::ResolutionError(
        "single-version-v1 source conflict for package '" + candidate.package.name + "'\n"
        "  required by: " +
        JoinRequirementChain(other_chain) +
        "\n"
        "  required by: " +
        JoinRequirementChain(candidate_chain));
  }

  size_t ResolveNode(
      const fs::path &manifest_path,
      const SourceOrigin &origin,
      const std::vector<std::string> &parent_path)
  {
    const fs::path normalized_manifest_path = pafio::CanonicalAbsolutePath(manifest_path);
    const fs::path package_dir = normalized_manifest_path.parent_path();
    const std::string source_fingerprint = BuildSourceFingerprint(origin, package_dir);

    if (const auto existing = node_by_source_fingerprint_.find(source_fingerprint); existing != node_by_source_fingerprint_.end())
    {
      const size_t existing_index = existing->second;
      if (const auto visit = visit_state_by_index_.find(existing_index);
          visit != visit_state_by_index_.end() && visit->second == VisitState::kActive)
      {
        throw pafio::ResolutionError(
            "single-version-v1 requires an acyclic dependency graph: " + FormatCyclePath(existing_index));
      }
      return existing_index;
    }

    const pafio::ManifestDocument manifest =
        normalized_manifest_path == root_manifest_path_ ? root_manifest_ : pafio::LoadManifest(normalized_manifest_path);
    if (!manifest.package.has_value())
    {
      if (origin.kind == SourceKind::kWorkspace)
      {
        throw pafio::WorkspaceError("workspace package manifest must define [package]: " + normalized_manifest_path.string());
      }
      throw pafio::ResolutionError("resolved manifest must define [package]: " + normalized_manifest_path.string());
    }

    Node node{
        .manifest_path = normalized_manifest_path,
        .package_dir = package_dir,
        .package = *manifest.package,
        .origin = origin,
        .source_fingerprint = source_fingerprint,
        .id = BuildLockId(origin, *manifest.package),
        .dependencies = {},
        .dependency_aliases = {},
    };

    std::vector<std::string> candidate_chain = parent_path;
    candidate_chain.push_back(node.id);

    if (const auto existing_by_name = node_by_package_name_.find(node.package.name); existing_by_name != node_by_package_name_.end())
    {
      const Node &other = nodes_.at(existing_by_name->second);
      if (other.package.version != node.package.version)
      {
        ThrowVersionConflict(other, node, candidate_chain);
      }
      if (other.source_fingerprint != node.source_fingerprint)
      {
        ThrowSourceConflict(other, node, candidate_chain);
      }
      return existing_by_name->second;
    }

    const size_t node_index = nodes_.size();
    nodes_.push_back(node);
    node_by_source_fingerprint_[source_fingerprint] = node_index;
    node_by_package_name_[node.package.name] = node_index;
    path_ids_by_index_[node_index] = candidate_chain;
    visit_state_by_index_[node_index] = VisitState::kActive;
    active_stack_.push_back(node_index);

    std::vector<std::string> dependencies;
    std::vector<pafio::ResolvedDependencyAlias> dependency_aliases;
    try
    {
      for (const pafio::Dependency &dependency : CollectDependencies(nodes_.at(node_index).package))
      {
        ManifestSelection selected;
        if (dependency.source_kind == pafio::DependencySourceKind::kPath)
        {
          selected = ResolvePathDependency(nodes_.at(node_index), dependency);
        }
        else if (dependency.source_kind == pafio::DependencySourceKind::kGit)
        {
          selected = ResolveGitDependency(nodes_.at(node_index), dependency);
        }
        else
        {
          selected = ResolveRegistryDependency(nodes_.at(node_index), dependency);
        }

        const size_t dependency_index = ResolveNode(selected.manifest_path, selected.origin, candidate_chain);
        const Node &dependency_node = nodes_.at(dependency_index);
        if (dependency.package.has_value() && *dependency.package != dependency_node.package.name)
        {
          throw pafio::ResolutionError(
              "dependency '" + dependency.alias + "' expected package '" + *dependency.package +
              "' but found '" + dependency_node.package.name + "'");
        }
        dependencies.push_back(dependency_node.id);
        dependency_aliases.push_back({
            .alias = dependency.alias,
            .package_id = dependency_node.id,
        });
      }
    }
    catch (...)
    {
      active_stack_.pop_back();
      visit_state_by_index_.erase(node_index);
      throw;
    }

    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    std::sort(
        dependency_aliases.begin(),
        dependency_aliases.end(),
        [](const pafio::ResolvedDependencyAlias &left, const pafio::ResolvedDependencyAlias &right) {
          return left.alias < right.alias;
        });
    nodes_[node_index].dependencies = std::move(dependencies);
    nodes_[node_index].dependency_aliases = std::move(dependency_aliases);
    active_stack_.pop_back();
    visit_state_by_index_[node_index] = VisitState::kDone;
    return node_index;
  }

  fs::path root_manifest_path_;
  pafio::ManifestDocument root_manifest_;
  pafio::ResolveOptions options_;
  GitSourceCache git_cache_;
  std::vector<ManifestSelection> root_seeds_;
  std::set<std::string> top_level_workspace_dirs_;
  std::unordered_map<std::string, size_t> node_by_source_fingerprint_;
  std::unordered_map<std::string, std::string> locked_registry_digests_;
  std::unordered_map<std::string, size_t> node_by_package_name_;
  std::unordered_map<std::string, pafio::RegistryMaterializationResult> materialized_registry_;
  std::unordered_map<size_t, VisitState> visit_state_by_index_;
  std::unordered_map<size_t, std::vector<std::string>> path_ids_by_index_;
  std::vector<size_t> active_stack_;
  std::vector<Node> nodes_;
};

}  // namespace

namespace pafio
{

ResolvedGraphResult ResolveSingleVersionGraph(const fs::path &manifest_path, const ResolveOptions &options)
{
  return SingleVersionResolver(manifest_path, options).ResolveGraph();
}

LockGenerationResult ResolveSingleVersionLockfile(const fs::path &manifest_path, const ResolveOptions &options)
{
  return SingleVersionResolver(manifest_path, options).Resolve();
}

}  // namespace pafio
