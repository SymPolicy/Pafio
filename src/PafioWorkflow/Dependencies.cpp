#include "PafioWorkflow/Dependencies.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioManifest/Lockfile.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioResolve/Resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

const std::regex &PackageNameRegex()
{
  static const std::regex pattern("^[a-z0-9][a-z0-9_-]*/[a-z0-9][a-z0-9_-]*$");
  return pattern;
}

const std::regex &SemverRegex()
{
  static const std::regex pattern("^\\d+\\.\\d+\\.\\d+$");
  return pattern;
}

bool IsNonEmptyRelativePath(const std::string &value)
{
  if (value.empty())
  {
    return false;
  }
  return !fs::path(value).is_absolute();
}

bool IsRegistryRootUrl(const std::string &value)
{
  return value.starts_with("file://") || value.starts_with("http://") || value.starts_with("https://");
}

void ValidatePackageName(const std::string &value, const std::string &context)
{
  if (!std::regex_match(value, PackageNameRegex()))
  {
    throw pafio::ValidationError(context + " must match namespace/name");
  }
}

void ValidateSemver(const std::string &value, const std::string &context)
{
  if (!std::regex_match(value, SemverRegex()))
  {
    throw pafio::ValidationError(context + " must be strict semver x.y.z");
  }
}

std::string PackageShortName(const std::string &package_name)
{
  ValidatePackageName(package_name, "package name");
  return package_name.substr(package_name.find('/') + 1U);
}

std::string ReadFile(const fs::path &path)
{
  std::ifstream in(path);
  if (!in)
  {
    throw std::runtime_error("failed to read file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void WriteFile(const fs::path &path, const std::string &content)
{
  std::ofstream out(path);
  if (!out)
  {
    throw std::runtime_error("failed to open file for write: " + path.string());
  }
  out << content;
  if (!out.good())
  {
    throw std::runtime_error("failed to write file: " + path.string());
  }
}

std::vector<pafio::Dependency> &SelectSection(pafio::PackageConfig &package, pafio::DependencySection section)
{
  if (section == pafio::DependencySection::kDevDependencies)
  {
    return package.dev_dependencies;
  }
  return package.dependencies;
}

const std::vector<pafio::Dependency> &SelectSection(const pafio::PackageConfig &package, pafio::DependencySection section)
{
  if (section == pafio::DependencySection::kDevDependencies)
  {
    return package.dev_dependencies;
  }
  return package.dependencies;
}

void EnsurePackageManifest(pafio::ManifestDocument &manifest, const fs::path &manifest_path, const std::string &command)
{
  if (!manifest.package.has_value())
  {
    throw pafio::ValidationError(command + " requires a manifest with [package]: " + manifest_path.string());
  }
}

void RestoreLockfile(const fs::path &lockfile_path, const std::optional<std::string> &original_lock)
{
  if (original_lock.has_value())
  {
    WriteFile(lockfile_path, *original_lock);
  }
  else
  {
    std::error_code ignored;
    fs::remove(lockfile_path, ignored);
  }
}

pafio::DependencyCommandResult CommitManifestEdit(
    const fs::path &manifest_path,
    const std::string &original_manifest,
    const std::optional<std::string> &original_lock,
    const pafio::ManifestDocument &manifest,
    const std::string &alias,
    const std::string &package_name,
    pafio::DependencySection section)
{
  const fs::path lockfile_path = manifest_path.parent_path() / "pafio.lock";

  try
  {
    WriteFile(manifest_path, pafio::SerializeManifestCanonical(manifest));
    const pafio::LockGenerationResult generated = pafio::ResolveSingleVersionLockfile(manifest_path);
    WriteFile(generated.lockfile_path, pafio::SerializeLockfileCanonical(generated.lockfile));
    return {
        .manifest_path = generated.manifest_path,
        .lockfile_path = generated.lockfile_path,
        .alias = alias,
        .package_name = package_name,
        .section = section,
        .package_count = generated.lockfile.packages.size(),
    };
  }
  catch (...)
  {
    WriteFile(manifest_path, original_manifest);
    RestoreLockfile(lockfile_path, original_lock);
    throw;
  }
}

bool DependencyMatchesTarget(const pafio::Dependency &dependency, const std::string &target, bool treat_as_package_name)
{
  if (!treat_as_package_name)
  {
    return dependency.alias == target;
  }
  return dependency.package.has_value() && *dependency.package == target;
}

struct MatchLocation
{
  pafio::DependencySection section = pafio::DependencySection::kDependencies;
  size_t index = 0;
  std::string alias;
  std::string package_name;
};

std::vector<MatchLocation> FindMatches(
    const pafio::PackageConfig &package,
    const std::string &target,
    const std::optional<pafio::DependencySection> &requested_section,
    bool treat_as_package_name)
{
  std::vector<MatchLocation> matches;
  auto collect = [&](pafio::DependencySection section) {
    const std::vector<pafio::Dependency> &dependencies = SelectSection(package, section);
    for (size_t index = 0; index < dependencies.size(); ++index)
    {
      if (!DependencyMatchesTarget(dependencies[index], target, treat_as_package_name))
      {
        continue;
      }
      matches.push_back({
          .section = section,
          .index = index,
          .alias = dependencies[index].alias,
          .package_name = dependencies[index].package.value_or(std::string()),
      });
    }
  };

  if (requested_section.has_value())
  {
    collect(*requested_section);
    return matches;
  }

  collect(pafio::DependencySection::kDependencies);
  collect(pafio::DependencySection::kDevDependencies);
  return matches;
}

}  // namespace

namespace pafio
{

std::string DependencySectionName(const DependencySection section)
{
  if (section == DependencySection::kDevDependencies)
  {
    return "dev-dependencies";
  }
  return "dependencies";
}

DependencyCommandResult AddDependencyAndRefreshLock(const AddDependencyRequest &request)
{
  if (!fs::exists(request.manifest_path))
  {
    throw ValidationError("manifest not found: " + request.manifest_path.string());
  }
  if (request.alias.has_value() && request.alias->empty())
  {
    throw ValidationError("dependency alias must be a non-empty string");
  }
  if (request.source.empty())
  {
    throw ValidationError("dependency source must be a non-empty string");
  }
  ValidatePackageName(request.package_name, "dependency package");
  const int source_kind_count = static_cast<int>(request.use_git) + static_cast<int>(request.use_registry) +
                                static_cast<int>(!request.use_git && !request.use_registry);
  if (source_kind_count != 1)
  {
    throw ValidationError("add requires exactly one dependency source kind");
  }
  if (!request.use_git && !request.use_registry && !IsNonEmptyRelativePath(request.source))
  {
    throw ValidationError("path dependency source must be an explicit relative path");
  }
  if (request.use_git)
  {
    if (!request.rev.has_value() || request.rev->empty())
    {
      throw ValidationError("git dependencies require --rev <rev>");
    }
    if (request.version.has_value())
    {
      throw ValidationError("--version is only valid with --registry");
    }
  }
  else if (request.use_registry)
  {
    if (request.rev.has_value())
    {
      throw ValidationError("--rev is only valid with --git");
    }
    if (!request.version.has_value() || request.version->empty())
    {
      throw ValidationError("registry dependencies require --version <x.y.z>");
    }
    ValidateSemver(*request.version, "dependency version");
    if (!IsRegistryRootUrl(request.source))
    {
      throw ValidationError("registry dependency source must use file://, http://, or https://");
    }
  }
  else if (request.rev.has_value() || request.version.has_value())
  {
    throw ValidationError("--rev is only valid with --git and --version is only valid with --registry");
  }

  const std::string alias = request.alias.value_or(PackageShortName(request.package_name));
  const std::string original_manifest = ReadFile(request.manifest_path);
  const fs::path lockfile_path = request.manifest_path.parent_path() / "pafio.lock";
  const std::optional<std::string> original_lock = fs::exists(lockfile_path) ? std::optional<std::string>(ReadFile(lockfile_path)) : std::nullopt;

  ManifestDocument manifest = LoadManifest(request.manifest_path);
  EnsurePackageManifest(manifest, request.manifest_path, "add");
  PackageConfig &package = *manifest.package;

  const auto has_alias = [&](const std::vector<Dependency> &dependencies) {
    return std::any_of(
        dependencies.begin(),
        dependencies.end(),
        [&](const Dependency &dependency) {
          return dependency.alias == alias;
        });
  };
  if (has_alias(package.dependencies) || has_alias(package.dev_dependencies))
  {
    throw ValidationError("dependency alias already exists in manifest: " + alias);
  }

  const auto has_package = [&](const std::vector<Dependency> &dependencies) {
    return std::any_of(
        dependencies.begin(),
        dependencies.end(),
        [&](const Dependency &dependency) {
          return dependency.package.has_value() && *dependency.package == request.package_name;
        });
  };
  if (has_package(package.dependencies) || has_package(package.dev_dependencies))
  {
    throw ValidationError("dependency package already exists in manifest: " + request.package_name);
  }

  Dependency dependency;
  dependency.alias = alias;
  dependency.package = request.package_name;
  dependency.source_kind = request.use_git ? DependencySourceKind::kGit
                                           : (request.use_registry ? DependencySourceKind::kRegistry : DependencySourceKind::kPath);
  dependency.source = request.source;
  dependency.rev = request.rev;
  dependency.version = request.version;

  std::vector<Dependency> &dependencies = SelectSection(package, request.section);
  dependencies.push_back(std::move(dependency));
  return CommitManifestEdit(
      request.manifest_path,
      original_manifest,
      original_lock,
      manifest,
      alias,
      request.package_name,
      request.section);
}

DependencyCommandResult RemoveDependencyAndRefreshLock(const RemoveDependencyRequest &request)
{
  if (!fs::exists(request.manifest_path))
  {
    throw ValidationError("manifest not found: " + request.manifest_path.string());
  }
  if (request.target.empty())
  {
    throw ValidationError("remove requires a dependency alias or package name");
  }

  const std::string original_manifest = ReadFile(request.manifest_path);
  const fs::path lockfile_path = request.manifest_path.parent_path() / "pafio.lock";
  const std::optional<std::string> original_lock = fs::exists(lockfile_path) ? std::optional<std::string>(ReadFile(lockfile_path)) : std::nullopt;

  ManifestDocument manifest = LoadManifest(request.manifest_path);
  EnsurePackageManifest(manifest, request.manifest_path, "remove");
  PackageConfig &package = *manifest.package;

  std::vector<MatchLocation> matches = FindMatches(package, request.target, request.section, false);
  if (matches.empty() && request.target.find('/') != std::string::npos)
  {
    matches = FindMatches(package, request.target, request.section, true);
  }
  if (matches.empty())
  {
    throw ValidationError("dependency was not found in manifest: " + request.target);
  }
  if (matches.size() != 1U)
  {
    throw ValidationError("dependency target is ambiguous in manifest: " + request.target);
  }

  const MatchLocation match = matches.front();
  std::vector<Dependency> &dependencies = SelectSection(package, match.section);
  dependencies.erase(dependencies.begin() + static_cast<std::ptrdiff_t>(match.index));

  return CommitManifestEdit(
      request.manifest_path,
      original_manifest,
      original_lock,
      manifest,
      match.alias,
      match.package_name,
      match.section);
}

FetchCommandResult FetchDependencies(const fs::path &manifest_path, const ResolveOptions &options)
{
  if (!fs::exists(manifest_path))
  {
    throw ValidationError("manifest not found: " + manifest_path.string());
  }

  const LockGenerationResult generated = ResolveSingleVersionLockfile(manifest_path, options);
  size_t git_package_count = 0;
  size_t registry_package_count = 0;
  for (const LockPackage &package : generated.lockfile.packages)
  {
    if (package.source_kind == "git")
    {
      ++git_package_count;
    }
    else if (package.source_kind == "registry")
    {
      ++registry_package_count;
    }
  }

  return {
      .manifest_path = generated.manifest_path,
      .package_count = generated.lockfile.packages.size(),
      .git_package_count = git_package_count,
      .registry_package_count = registry_package_count,
  };
}

}  // namespace pafio
