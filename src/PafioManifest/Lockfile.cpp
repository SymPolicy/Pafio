#include "PafioManifest/Lockfile.hpp"

#include "PafioCore/Errors.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>

#include <toml++/toml.h>

namespace fs = std::filesystem;

namespace
{

const std::regex &NameRegex()
{
  static const std::regex pattern("^[a-z0-9][a-z0-9_-]*/[a-z0-9][a-z0-9_-]*$");
  return pattern;
}

const std::regex &SemverRegex()
{
  static const std::regex pattern("^\\d+\\.\\d+\\.\\d+$");
  return pattern;
}

const std::regex &Sha256Regex()
{
  static const std::regex pattern("^[0-9a-f]{64}$");
  return pattern;
}

template <typename T>
T RequireValue(const toml::table &table, std::string_view key, std::string_view context)
{
  if (const auto value = table[key].value<T>())
  {
    return *value;
  }

  throw pafio::ValidationError("missing or invalid '" + std::string(key) + "' in " + std::string(context));
}

std::vector<std::string> ParseStringArray(const toml::table &table, std::string_view key, std::string_view context)
{
  const toml::array *array = table[key].as_array();
  if (array == nullptr)
  {
    throw pafio::ValidationError("missing or invalid '" + std::string(key) + "' array in " + std::string(context));
  }

  std::vector<std::string> values;
  values.reserve(array->size());
  for (const toml::node &node : *array)
  {
    const auto value = node.value<std::string>();
    if (!value.has_value())
    {
      throw pafio::ValidationError("array '" + std::string(key) + "' in " + std::string(context) + " must contain only strings");
    }
    values.push_back(*value);
  }
  return values;
}

std::optional<std::string> OptionalString(const toml::table &table, std::string_view key)
{
  if (const auto value = table[key].value<std::string>())
  {
    return *value;
  }
  return std::nullopt;
}

void ValidatePackageName(const std::string &name, std::string_view context)
{
  if (!std::regex_match(name, NameRegex()))
  {
    throw pafio::ValidationError(std::string(context) + " must match namespace/name");
  }
}

void ValidateSemver(const std::string &version, std::string_view context)
{
  if (!std::regex_match(version, SemverRegex()))
  {
    throw pafio::ValidationError(std::string(context) + " must be strict semver x.y.z");
  }
}

void ValidateSourceKind(const std::string &source_kind)
{
  static const std::set<std::string> allowed = {"workspace", "path", "git", "registry"};
  if (!allowed.contains(source_kind))
  {
    throw pafio::ValidationError("lockfile package source-kind must be one of workspace, path, git, or registry");
  }
}

void ValidateSha256(const std::string &sha256, std::string_view context)
{
  if (!std::regex_match(sha256, Sha256Regex()))
  {
    throw pafio::ValidationError(std::string(context) + " must be a lowercase sha256 hex digest");
  }
}

std::string QuoteTomlString(const std::string &value)
{
  std::ostringstream out;
  out << std::quoted(value);
  return out.str();
}

}  // namespace

namespace pafio
{

LockfileDocument LoadLockfile(const fs::path &lockfile_path)
{
  toml::table doc;
  try
  {
    doc = toml::parse_file(lockfile_path.string());
  }
  catch (const toml::parse_error &err)
  {
    throw ValidationError("failed to parse lockfile '" + lockfile_path.string() + "': " + std::string(err.description()));
  }

  const auto lock_version = doc["lock-version"].value<int64_t>();
  if (!lock_version.has_value() || *lock_version != 1)
  {
    throw ValidationError("lock-version must be 1");
  }

  const toml::table *metadata = doc["metadata"].as_table();
  if (metadata == nullptr)
  {
    throw ValidationError("missing or invalid [metadata] table");
  }

  LockfileDocument lockfile;
  lockfile.generated_by = RequireValue<std::string>(*metadata, "generated-by", "[metadata]");
  lockfile.resolver = RequireValue<std::string>(*metadata, "resolver", "[metadata]");
  if (lockfile.generated_by.empty())
  {
    throw ValidationError("[metadata].generated-by must be a non-empty string");
  }
  if (lockfile.resolver != "single-version-v1")
  {
    throw ValidationError("[metadata].resolver must be single-version-v1");
  }

  const toml::array *packages = doc["package"].as_array();
  if (packages == nullptr)
  {
    return lockfile;
  }

  lockfile.packages.reserve(packages->size());
  for (const toml::node &node : *packages)
  {
    const toml::table *package = node.as_table();
    if (package == nullptr)
    {
      throw ValidationError("lockfile package entry must be a table");
    }

    LockPackage parsed;
    parsed.id = RequireValue<std::string>(*package, "id", "[[package]]");
    parsed.name = RequireValue<std::string>(*package, "name", "[[package]]");
    parsed.version = RequireValue<std::string>(*package, "version", "[[package]]");
    parsed.source_kind = RequireValue<std::string>(*package, "source-kind", "[[package]]");
    parsed.git = OptionalString(*package, "git");
    parsed.rev = OptionalString(*package, "rev");
    parsed.registry = OptionalString(*package, "registry");
    parsed.sha256 = OptionalString(*package, "sha256");
    parsed.dependencies = ParseStringArray(*package, "dependencies", "[[package]]");

    if (parsed.id.empty())
    {
      throw ValidationError("[[package]].id must be a non-empty string");
    }
    ValidatePackageName(parsed.name, "lockfile package name");
    ValidateSemver(parsed.version, "lockfile package version");
    ValidateSourceKind(parsed.source_kind);
    if (parsed.source_kind == "git")
    {
      if (!parsed.git.has_value() || parsed.git->empty())
      {
        throw ValidationError("[[package]].git must be a non-empty string when source-kind = \"git\"");
      }
      if (!parsed.rev.has_value() || parsed.rev->empty())
      {
        throw ValidationError("[[package]].rev must be a non-empty string when source-kind = \"git\"");
      }
      if (parsed.registry.has_value() || parsed.sha256.has_value())
      {
        throw ValidationError("[[package]].registry and [[package]].sha256 are only valid when source-kind = \"registry\"");
      }
    }
    else if (parsed.source_kind == "registry")
    {
      if (!parsed.registry.has_value() || parsed.registry->empty())
      {
        throw ValidationError("[[package]].registry must be a non-empty string when source-kind = \"registry\"");
      }
      if (!parsed.sha256.has_value())
      {
        throw ValidationError("[[package]].sha256 must be present when source-kind = \"registry\"");
      }
      ValidateSha256(*parsed.sha256, "[[package]].sha256");
      if (parsed.git.has_value() || parsed.rev.has_value())
      {
        throw ValidationError("[[package]].git and [[package]].rev are only valid when source-kind = \"git\"");
      }
    }
    else if (parsed.git.has_value() || parsed.rev.has_value() || parsed.registry.has_value() || parsed.sha256.has_value())
    {
      throw ValidationError("[[package]].git/rev are only valid for git packages and [[package]].registry/sha256 are only valid for registry packages");
    }

    lockfile.packages.push_back(std::move(parsed));
  }

  return lockfile;
}

std::string SerializeLockfileCanonical(const LockfileDocument &lockfile)
{
  std::ostringstream out;
  out << "lock-version = 1\n";
  out << "\n[metadata]\n";
  out << "generated-by = " << QuoteTomlString(lockfile.generated_by) << "\n";
  out << "resolver = " << QuoteTomlString(lockfile.resolver) << "\n";

  std::vector<LockPackage> packages = lockfile.packages;
  std::sort(
      packages.begin(),
      packages.end(),
      [](const LockPackage &left, const LockPackage &right) {
        return left.id < right.id;
      });

  for (const LockPackage &package : packages)
  {
    out << "\n[[package]]\n";
    out << "id = " << QuoteTomlString(package.id) << "\n";
    out << "name = " << QuoteTomlString(package.name) << "\n";
    out << "version = " << QuoteTomlString(package.version) << "\n";
    out << "source-kind = " << QuoteTomlString(package.source_kind) << "\n";
    if (package.source_kind == "git")
    {
      out << "git = " << QuoteTomlString(package.git.value_or("")) << "\n";
      out << "rev = " << QuoteTomlString(package.rev.value_or("")) << "\n";
    }
    else if (package.source_kind == "registry")
    {
      out << "registry = " << QuoteTomlString(package.registry.value_or("")) << "\n";
      out << "sha256 = " << QuoteTomlString(package.sha256.value_or("")) << "\n";
    }

    std::vector<std::string> dependencies = package.dependencies;
    std::sort(dependencies.begin(), dependencies.end());
    out << "dependencies = [";
    for (size_t index = 0; index < dependencies.size(); ++index)
    {
      if (index > 0)
      {
        out << ", ";
      }
      out << QuoteTomlString(dependencies[index]);
    }
    out << "]\n";
  }

  return out.str();
}

}  // namespace pafio
