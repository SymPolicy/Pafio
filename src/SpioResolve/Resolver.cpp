#include "SpioResolve/Resolver.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Version.hpp"
#include "SpioManifest/Manifest.hpp"
#include "SpioRegistryClient/Client.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

struct ChildProcessResult
{
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

std::string PathKey(const fs::path &path)
{
  return CanonicalAbsolutePath(path).generic_string();
}

uint64_t Fnv1a64(const std::string &value)
{
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char ch : value)
  {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string Hex64(uint64_t value)
{
  std::ostringstream out;
  out << std::hex << value;
  return out.str();
}

bool LooksLikeRemoteGitSource(const std::string &source)
{
  return source.find("://") != std::string::npos || source.starts_with("git@");
}

bool PathIsWithin(const fs::path &candidate, const fs::path &root)
{
  const fs::path relative = CanonicalAbsolutePath(candidate).lexically_relative(CanonicalAbsolutePath(root));
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

  const fs::path source_path(source);
  if (source_path.is_absolute())
  {
    return CanonicalAbsolutePath(source_path).generic_string();
  }
  return CanonicalAbsolutePath(package_dir / source_path).generic_string();
}

ChildProcessResult RunChildProcess(const std::string &binary, const std::vector<std::string> &args)
{
  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    throw spio::CacheError("failed to create pipes for resolver process execution");
  }

  const pid_t child = fork();
  if (child < 0)
  {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    throw spio::CacheError("failed to fork resolver process");
  }

  if (child == 0)
  {
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 2U);
    argv.push_back(const_cast<char *>(binary.c_str()));
    for (const std::string &arg : args)
    {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(binary.c_str(), argv.data());
    _exit(127);
  }

  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  auto read_all = [](int fd) {
    std::string text;
    std::array<char, 4096> buffer{};
    ssize_t read_size = 0;
    while ((read_size = read(fd, buffer.data(), buffer.size())) > 0)
    {
      text.append(buffer.data(), static_cast<size_t>(read_size));
    }
    close(fd);
    return text;
  };

  ChildProcessResult result;
  result.stdout_text = read_all(stdout_pipe[0]);
  result.stderr_text = read_all(stderr_pipe[0]);

  int status = 0;
  waitpid(child, &status, 0);
  if (WIFEXITED(status))
  {
    result.exit_code = WEXITSTATUS(status);
  }
  else
  {
    result.exit_code = 1;
  }
  return result;
}

std::string TrimTrailingNewline(std::string text)
{
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
  {
    text.pop_back();
  }
  return text;
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

std::string SnapshotRelativeKey(const SourceOrigin &origin, const fs::path &package_dir)
{
  const fs::path snapshot_root = CanonicalAbsolutePath(origin.snapshot_root.value());
  const fs::path relative = CanonicalAbsolutePath(package_dir).lexically_relative(snapshot_root);
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

std::string BuildLockId(const SourceOrigin &origin, const spio::PackageConfig &package)
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

std::vector<spio::Dependency> CollectDependencies(const spio::PackageConfig &package)
{
  std::vector<spio::Dependency> dependencies;
  dependencies.reserve(package.dependencies.size() + package.dev_dependencies.size());
  dependencies.insert(dependencies.end(), package.dependencies.begin(), package.dependencies.end());
  dependencies.insert(dependencies.end(), package.dev_dependencies.begin(), package.dev_dependencies.end());
  std::sort(
      dependencies.begin(),
      dependencies.end(),
      [](const spio::Dependency &left, const spio::Dependency &right) {
        return left.alias < right.alias;
      });
  return dependencies;
}

class GitSourceCache
{
public:
  explicit GitSourceCache(const fs::path &spio_home, const bool offline, std::optional<fs::path> vendor_root)
      : spio_home_(CanonicalAbsolutePath(spio_home)),
        offline_(offline)
  {
    if (vendor_root.has_value())
    {
      vendor_root_ = CanonicalAbsolutePath(*vendor_root);
    }
    fs::create_directories(spio_home_ / "git" / "repos");
    fs::create_directories(spio_home_ / "git" / "checkouts");
  }

  SourceOrigin Materialize(const std::string &normalized_source, const std::string &rev)
  {
    const std::string repo_hash = Hex64(Fnv1a64(normalized_source));
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

    const fs::path repo_dir = spio_home_ / "git" / "repos" / (repo_hash + ".git");
    if (!fs::exists(repo_dir))
    {
      if (offline_)
      {
        throw spio::FetchError(
            "offline mode requires a vendored snapshot or cached git mirror for '" + normalized_source + "'");
      }
      EnsureMirror(normalized_source, repo_dir);
    }
    if (!HasRevision(repo_dir, rev))
    {
      if (offline_)
      {
        throw spio::FetchError(
            "offline mode is missing git rev '" + rev + "' in the local cache for '" + normalized_source + "'");
      }
      FetchOrigin(repo_dir);
      if (!HasRevision(repo_dir, rev))
      {
        throw spio::FetchError("git source does not contain requested rev '" + rev + "': " + normalized_source);
      }
    }

    const fs::path snapshot_root = spio_home_ / "git" / "checkouts" / repo_hash / rev;
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

    const fs::path ready_marker = snapshot_root / ".spio-snapshot-ready";
    if (fs::exists(ready_marker) || fs::exists(snapshot_root / "spio.toml"))
    {
      return CanonicalAbsolutePath(snapshot_root);
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
    const ChildProcessResult result = RunChildProcess("git", {"clone", "--mirror", normalized_source, repo_dir.string()});
    if (result.exit_code != 0)
    {
      throw spio::FetchError(
          "failed to clone git source '" + normalized_source + "': " +
          TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text));
    }
  }

  bool HasRevision(const fs::path &repo_dir, const std::string &rev) const
  {
    const ChildProcessResult result = RunChildProcess("git", {"--git-dir", repo_dir.string(), "cat-file", "-e", rev + "^{commit}"});
    return result.exit_code == 0;
  }

  void FetchOrigin(const fs::path &repo_dir) const
  {
    const ChildProcessResult result = RunChildProcess("git", {"--git-dir", repo_dir.string(), "fetch", "--prune", "origin"});
    if (result.exit_code != 0)
    {
      throw spio::FetchError(
          "failed to fetch git source cache '" + repo_dir.string() + "': " +
          TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text));
    }
  }

  void EnsureSnapshot(const fs::path &repo_dir, const fs::path &snapshot_root, const std::string &rev) const
  {
    const fs::path ready_marker = snapshot_root / ".spio-snapshot-ready";
    if (fs::exists(ready_marker))
    {
      return;
    }

    fs::remove_all(snapshot_root);
    fs::create_directories(snapshot_root);
    const ChildProcessResult archive = RunChildProcess("git", {"--git-dir", repo_dir.string(), "archive", "--format=tar", rev});
    if (archive.exit_code != 0)
    {
      throw spio::FetchError(
          "failed to archive git rev '" + rev + "': " +
          TrimTrailingNewline(archive.stderr_text.empty() ? archive.stdout_text : archive.stderr_text));
    }

    const fs::path archive_path = snapshot_root.parent_path() / (Hex64(Fnv1a64(rev)) + ".tar");
    {
      std::ofstream out(archive_path, std::ios::binary);
      if (!out)
      {
        throw spio::CacheError("failed to create temporary git archive: " + archive_path.string());
      }
      out.write(archive.stdout_text.data(), static_cast<std::streamsize>(archive.stdout_text.size()));
      if (!out.good())
      {
        throw spio::CacheError("failed to write temporary git archive: " + archive_path.string());
      }
    }

    const ChildProcessResult extract = RunChildProcess("tar", {"-xf", archive_path.string(), "-C", snapshot_root.string()});
    fs::remove(archive_path);
    if (extract.exit_code != 0)
    {
      throw spio::CacheError(
          "failed to extract git snapshot '" + snapshot_root.string() + "': " +
          TrimTrailingNewline(extract.stderr_text.empty() ? extract.stdout_text : extract.stderr_text));
    }

    std::ofstream marker(ready_marker);
    marker << "ready\n";
  }

  fs::path spio_home_;
  bool offline_ = false;
  std::optional<fs::path> vendor_root_;
};

class SingleVersionResolver
{
public:
  explicit SingleVersionResolver(const fs::path &manifest_path, const spio::ResolveOptions &options)
      : root_manifest_path_(CanonicalAbsolutePath(manifest_path)),
        root_manifest_(spio::LoadManifest(root_manifest_path_)),
        options_(NormalizeOptions(root_manifest_path_, options)),
        git_cache_(spio::ResolveSpioHome(), options_.offline, options_.vendor_root)
  {
    BuildRootSeeds();
  }

  spio::LockGenerationResult Resolve()
  {
    const spio::ResolvedGraphResult graph = ResolveGraph();
    return {
        .manifest_path = graph.manifest_path,
        .lockfile_path = graph.lockfile_path,
        .root_ids = graph.root_ids,
        .lockfile = graph.lockfile,
    };
  }

  spio::ResolvedGraphResult ResolveGraph()
  {
    std::vector<std::string> root_ids;
    for (const ManifestSelection &seed : root_seeds_)
    {
      const size_t root_index = ResolveNode(seed.manifest_path, seed.origin);
      root_ids.push_back(nodes_.at(root_index).id);
    }
    std::sort(root_ids.begin(), root_ids.end());
    root_ids.erase(std::unique(root_ids.begin(), root_ids.end()), root_ids.end());

    std::vector<spio::ResolvedPackage> packages;
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
        [](const spio::ResolvedPackage &left, const spio::ResolvedPackage &right) {
          return left.id < right.id;
        });

    spio::LockfileDocument lockfile;
    lockfile.generated_by = std::string("spio ") + std::string(spio::kVersion);
    lockfile.resolver = "single-version-v1";
    lockfile.packages.reserve(packages.size());
    for (const spio::ResolvedPackage &package : packages)
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
        .lockfile_path = root_manifest_path_.parent_path() / "spio.lock",
        .root_ids = std::move(root_ids),
        .packages = std::move(packages),
        .lockfile = std::move(lockfile),
    };
  }

private:
  static spio::ResolveOptions NormalizeOptions(const fs::path &manifest_path, const spio::ResolveOptions &options)
  {
    spio::ResolveOptions normalized = options;
    if (!normalized.vendor_root.has_value())
    {
      const fs::path default_vendor_root = spio::ProjectVendorRootForManifest(manifest_path);
      if (fs::exists(default_vendor_root))
      {
        normalized.vendor_root = default_vendor_root;
      }
    }
    else
    {
      normalized.vendor_root = CanonicalAbsolutePath(*normalized.vendor_root);
    }
    return normalized;
  }

  struct Node
  {
    fs::path manifest_path;
    fs::path package_dir;
    spio::PackageConfig package;
    SourceOrigin origin;
    std::string source_fingerprint;
    std::string id;
    std::vector<std::string> dependencies;
    std::vector<spio::ResolvedDependencyAlias> dependency_aliases;
  };

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
        throw spio::WorkspaceError("workspace member is also excluded: " + member);
      }

      const fs::path member_manifest = CanonicalAbsolutePath(root_manifest_path_.parent_path() / member / "spio.toml");
      if (!fs::exists(member_manifest))
      {
        throw spio::WorkspaceError("workspace member manifest not found: " + member_manifest.string());
      }
      const spio::ManifestDocument member_doc = spio::LoadManifest(member_manifest);
      if (!member_doc.package.has_value())
      {
        throw spio::WorkspaceError("workspace member manifest must define [package]: " + member_manifest.string());
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
    const spio::ManifestDocument root_doc = root_manifest_path == root_manifest_path_ ? root_manifest_ : spio::LoadManifest(root_manifest_path);
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
        throw spio::ResolutionError("dependency target is a workspace root and requires an explicit package: " + root_manifest_path.string());
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
          const fs::path member_manifest = CanonicalAbsolutePath(root_manifest_path.parent_path() / member / "spio.toml");
          if (!fs::exists(member_manifest))
          {
            throw spio::ResolutionError("dependency workspace member manifest not found: " + member_manifest.string());
          }
          const spio::ManifestDocument member_doc = spio::LoadManifest(member_manifest);
          if (!member_doc.package.has_value())
          {
            throw spio::ResolutionError("dependency workspace member manifest must define [package]: " + member_manifest.string());
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
        throw spio::ResolutionError(
            "dependency target package '" + *expected_package + "' was not found under " + root_manifest_path.string());
      }
      throw spio::ResolutionError("dependency target manifest does not define [package]: " + root_manifest_path.string());
    }
    if (matches.size() > 1)
    {
      throw spio::ResolutionError("dependency target package is ambiguous under " + root_manifest_path.string());
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

  ManifestSelection ResolvePathDependency(const Node &parent, const spio::Dependency &dependency)
  {
    const fs::path dependency_root = CanonicalAbsolutePath(parent.package_dir / dependency.source / "spio.toml");
    if (!fs::exists(dependency_root))
    {
      throw spio::ResolutionError("path dependency manifest not found: " + dependency_root.string());
    }

    if (parent.origin.kind == SourceKind::kGit)
    {
      if (!PathIsWithin(dependency_root.parent_path(), parent.origin.snapshot_root.value()) &&
          CanonicalAbsolutePath(dependency_root.parent_path()) != CanonicalAbsolutePath(parent.origin.snapshot_root.value()))
      {
        throw spio::ResolutionError(
            "git-sourced path dependency escapes its pinned snapshot: " + dependency_root.string());
      }
      return SelectPackageManifestFromRoot(dependency_root, parent.origin, dependency.package);
    }

    return SelectPackageManifestFromRoot(dependency_root, SourceOrigin{.kind = SourceKind::kPath}, dependency.package);
  }

  ManifestSelection ResolveGitDependency(const Node &parent, const spio::Dependency &dependency)
  {
    const std::string normalized_source = NormalizeGitSource(dependency.source, parent.package_dir);
    const SourceOrigin git_origin = git_cache_.Materialize(normalized_source, dependency.rev.value());
    const fs::path root_manifest = git_origin.snapshot_root.value() / "spio.toml";
    if (!fs::exists(root_manifest))
    {
      throw spio::ResolutionError("git dependency snapshot does not contain spio.toml: " + root_manifest.string());
    }
    return SelectPackageManifestFromRoot(root_manifest, git_origin, dependency.package);
  }

  ManifestSelection ResolveRegistryDependency(const Node &parent, const spio::Dependency &dependency)
  {
    (void) parent;
    if (!dependency.package.has_value())
    {
      throw spio::ResolutionError("registry dependency '" + dependency.alias + "' must declare package = \"namespace/name\"");
    }
    if (!dependency.version.has_value())
    {
      throw spio::ResolutionError("registry dependency '" + dependency.alias + "' must declare version = \"x.y.z\"");
    }

    const spio::RegistryMaterializationResult materialized =
        spio::MaterializeRegistryPackage(dependency.source, *dependency.package, *dependency.version, options_.offline);
    const SourceOrigin registry_origin{
        .kind = SourceKind::kRegistry,
        .registry_root = materialized.registry_root,
        .registry_version = materialized.version,
        .registry_sha256 = materialized.sha256,
        .snapshot_root = materialized.snapshot_root,
    };
    const fs::path root_manifest = materialized.snapshot_root / "spio.toml";
    if (!fs::exists(root_manifest))
    {
      throw spio::ResolutionError("registry dependency snapshot does not contain spio.toml: " + root_manifest.string());
    }
    return SelectPackageManifestFromRoot(root_manifest, registry_origin, dependency.package);
  }

  size_t ResolveNode(const fs::path &manifest_path, const SourceOrigin &origin)
  {
    const fs::path normalized_manifest_path = CanonicalAbsolutePath(manifest_path);
    const fs::path package_dir = normalized_manifest_path.parent_path();
    const std::string source_fingerprint = BuildSourceFingerprint(origin, package_dir);

    if (const auto existing = node_by_source_fingerprint_.find(source_fingerprint); existing != node_by_source_fingerprint_.end())
    {
      return existing->second;
    }

    const spio::ManifestDocument manifest =
        normalized_manifest_path == root_manifest_path_ ? root_manifest_ : spio::LoadManifest(normalized_manifest_path);
    if (!manifest.package.has_value())
    {
      if (origin.kind == SourceKind::kWorkspace)
      {
        throw spio::WorkspaceError("workspace package manifest must define [package]: " + normalized_manifest_path.string());
      }
      throw spio::ResolutionError("resolved manifest must define [package]: " + normalized_manifest_path.string());
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

    if (const auto existing_by_name = node_by_package_name_.find(node.package.name); existing_by_name != node_by_package_name_.end())
    {
      const Node &other = nodes_.at(existing_by_name->second);
      if (other.package.version != node.package.version)
      {
        throw spio::ResolutionError(
            "single-version-v1 conflict for package '" + node.package.name + "': versions '" +
            other.package.version + "' and '" + node.package.version + "'");
      }
      if (other.source_fingerprint != node.source_fingerprint)
      {
        throw spio::ResolutionError(
            "single-version-v1 source conflict for package '" + node.package.name + "'");
      }
      return existing_by_name->second;
    }

    const size_t node_index = nodes_.size();
    nodes_.push_back(node);
    node_by_source_fingerprint_[source_fingerprint] = node_index;
    node_by_package_name_[node.package.name] = node_index;

    std::vector<std::string> dependencies;
    std::vector<spio::ResolvedDependencyAlias> dependency_aliases;
    for (const spio::Dependency &dependency : CollectDependencies(node.package))
    {
      ManifestSelection selected;
      if (dependency.source_kind == spio::DependencySourceKind::kPath)
      {
        selected = ResolvePathDependency(nodes_.at(node_index), dependency);
      }
      else if (dependency.source_kind == spio::DependencySourceKind::kGit)
      {
        selected = ResolveGitDependency(nodes_.at(node_index), dependency);
      }
      else
      {
        selected = ResolveRegistryDependency(nodes_.at(node_index), dependency);
      }

      const size_t dependency_index = ResolveNode(selected.manifest_path, selected.origin);
      const Node &dependency_node = nodes_.at(dependency_index);
      if (dependency.package.has_value() && *dependency.package != dependency_node.package.name)
      {
        throw spio::ResolutionError(
            "dependency '" + dependency.alias + "' expected package '" + *dependency.package +
            "' but found '" + dependency_node.package.name + "'");
      }
      if (dependency_node.source_fingerprint == node.source_fingerprint)
      {
        throw spio::ResolutionError("package '" + node.package.name + "' cannot depend on itself");
      }
      dependencies.push_back(dependency_node.id);
      dependency_aliases.push_back({
          .alias = dependency.alias,
          .package_id = dependency_node.id,
      });
    }

    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    std::sort(
        dependency_aliases.begin(),
        dependency_aliases.end(),
        [](const spio::ResolvedDependencyAlias &left, const spio::ResolvedDependencyAlias &right) {
          return left.alias < right.alias;
        });
    nodes_[node_index].dependencies = std::move(dependencies);
    nodes_[node_index].dependency_aliases = std::move(dependency_aliases);
    return node_index;
  }

  fs::path root_manifest_path_;
  spio::ManifestDocument root_manifest_;
  spio::ResolveOptions options_;
  GitSourceCache git_cache_;
  std::vector<ManifestSelection> root_seeds_;
  std::set<std::string> top_level_workspace_dirs_;
  std::unordered_map<std::string, size_t> node_by_source_fingerprint_;
  std::unordered_map<std::string, size_t> node_by_package_name_;
  std::vector<Node> nodes_;
};

}  // namespace

namespace spio
{

ResolvedGraphResult ResolveSingleVersionGraph(const fs::path &manifest_path, const ResolveOptions &options)
{
  return SingleVersionResolver(manifest_path, options).ResolveGraph();
}

LockGenerationResult ResolveSingleVersionLockfile(const fs::path &manifest_path, const ResolveOptions &options)
{
  return SingleVersionResolver(manifest_path, options).Resolve();
}

}  // namespace spio
