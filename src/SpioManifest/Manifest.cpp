#include "SpioManifest/Manifest.hpp"

#include "SpioCore/Errors.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
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

const std::regex &BareKeyRegex()
{
  static const std::regex pattern("^[A-Za-z0-9_-]+$");
  return pattern;
}

bool IsNonEmptyRelativePath(const std::string &value)
{
  if (value.empty())
  {
    return false;
  }

  const fs::path path(value);
  return !path.is_absolute();
}

bool IsRegistryRootUrl(const std::string &value)
{
  return value.starts_with("file://") || value.starts_with("http://") || value.starts_with("https://");
}

const toml::table &RequireTable(const toml::table &parent, std::string_view key, std::string_view context)
{
  if (const toml::table *table = parent[key].as_table())
  {
    return *table;
  }

  throw spio::ValidationError("missing or invalid [" + std::string(key) + "] table in " + std::string(context));
}

template <typename T>
T RequireValue(const toml::table &table, std::string_view key, std::string_view context)
{
  if (const auto value = table[key].value<T>())
  {
    return *value;
  }

  throw spio::ValidationError("missing or invalid '" + std::string(key) + "' in " + std::string(context));
}

std::optional<bool> OptionalBool(const toml::table &table, std::string_view key)
{
  if (const auto value = table[key].value<bool>())
  {
    return *value;
  }
  return std::nullopt;
}

std::optional<std::string> OptionalString(const toml::table &table, std::string_view key)
{
  if (const auto value = table[key].value<std::string>())
  {
    return *value;
  }
  return std::nullopt;
}

std::vector<std::string> RequireStringArray(const toml::table &table, std::string_view key, std::string_view context, bool require_non_empty)
{
  const toml::array *array = table[key].as_array();
  if (array == nullptr)
  {
    throw spio::ValidationError("missing or invalid '" + std::string(key) + "' array in " + std::string(context));
  }

  std::vector<std::string> values;
  values.reserve(array->size());
  for (const toml::node &node : *array)
  {
    const auto value = node.value<std::string>();
    if (!value.has_value())
    {
      throw spio::ValidationError("array '" + std::string(key) + "' in " + std::string(context) + " must contain only strings");
    }
    values.push_back(*value);
  }

  if (require_non_empty && values.empty())
  {
    throw spio::ValidationError("array '" + std::string(key) + "' in " + std::string(context) + " must not be empty");
  }

  return values;
}

void ValidatePackageName(const std::string &name, std::string_view context)
{
  if (!std::regex_match(name, NameRegex()))
  {
    throw spio::ValidationError(std::string(context) + " must match namespace/name");
  }
}

void ValidateSemver(const std::string &version, std::string_view context)
{
  if (!std::regex_match(version, SemverRegex()))
  {
    throw spio::ValidationError(std::string(context) + " must be strict semver x.y.z");
  }
}

void ValidateRelativePath(const std::string &path_value, std::string_view context)
{
  if (!IsNonEmptyRelativePath(path_value))
  {
    throw spio::ValidationError(std::string(context) + " must be an explicit relative path");
  }
}

void ValidateRegistryRoot(const std::string &registry_root, std::string_view context)
{
  if (!IsRegistryRootUrl(registry_root))
  {
    throw spio::ValidationError(std::string(context) + " must use file://, http://, or https://");
  }
}

std::vector<spio::Dependency> ParseDependencies(const toml::table &doc, std::string_view table_name)
{
  const toml::table *dependencies = doc[table_name].as_table();
  if (dependencies == nullptr)
  {
    return {};
  }

  std::vector<spio::Dependency> parsed;
  parsed.reserve(dependencies->size());
  for (const auto &[alias_key, node] : *dependencies)
  {
    const std::string alias = std::string(alias_key.str());
    const toml::table *dep = node.as_table();
    if (dep == nullptr)
    {
      throw spio::ValidationError("dependency '" + alias + "' in [" + std::string(table_name) + "] must be an inline table");
    }

    const bool has_path = dep->contains("path");
    const bool has_git = dep->contains("git");
    const bool has_version = dep->contains("version");
    const bool has_registry = dep->contains("registry");
    const bool has_registry_source = has_version || has_registry;
    const int source_kind_count = static_cast<int>(has_path) + static_cast<int>(has_git) + static_cast<int>(has_registry_source);
    if (source_kind_count != 1)
    {
      throw spio::ValidationError("dependency '" + alias + "' in [" + std::string(table_name) + "] must declare exactly one source kind");
    }

    spio::Dependency dependency;
    dependency.alias = alias;
    dependency.package = OptionalString(*dep, "package");
    if (dependency.package.has_value())
    {
      ValidatePackageName(*dependency.package, "dependency '" + alias + "' package");
    }

    if (has_path)
    {
      dependency.source_kind = spio::DependencySourceKind::kPath;
      dependency.source = RequireValue<std::string>(*dep, "path", "dependency '" + alias + "'");
      ValidateRelativePath(dependency.source, "dependency '" + alias + "' path");
    }
    else if (has_git)
    {
      dependency.source_kind = spio::DependencySourceKind::kGit;
      dependency.source = RequireValue<std::string>(*dep, "git", "dependency '" + alias + "'");
      if (dependency.source.empty())
      {
        throw spio::ValidationError("dependency '" + alias + "' git must be a non-empty string");
      }
      dependency.rev = RequireValue<std::string>(*dep, "rev", "dependency '" + alias + "'");
      if (dependency.rev->empty())
      {
        throw spio::ValidationError("dependency '" + alias + "' rev must be a non-empty string");
      }
    }
    else
    {
      if (!dependency.package.has_value())
      {
        throw spio::ValidationError("dependency '" + alias + "' must declare package = \"namespace/name\" for registry sources");
      }
      dependency.source_kind = spio::DependencySourceKind::kRegistry;
      dependency.version = RequireValue<std::string>(*dep, "version", "dependency '" + alias + "'");
      ValidateSemver(*dependency.version, "dependency '" + alias + "' version");
      dependency.source = RequireValue<std::string>(*dep, "registry", "dependency '" + alias + "'");
      ValidateRegistryRoot(dependency.source, "dependency '" + alias + "' registry");
    }

    parsed.push_back(std::move(dependency));
  }

  return parsed;
}

spio::Toolchain ParseToolchain(const toml::table &doc)
{
  const toml::table &toolchain_table = RequireTable(doc, "toolchain", "package manifest");

  spio::Toolchain toolchain;
  toolchain.channel = RequireValue<std::string>(toolchain_table, "channel", "[toolchain]");
  if (toolchain.channel.empty())
  {
    throw spio::ValidationError("[toolchain].channel must be a non-empty string");
  }

  const auto implicit_std = OptionalBool(toolchain_table, "implicit-std");
  if (!implicit_std.has_value())
  {
    throw spio::ValidationError("[toolchain].implicit-std must be a boolean");
  }
  toolchain.implicit_std = *implicit_std;
  return toolchain;
}

std::optional<spio::LibTarget> ParseLib(const toml::table &doc)
{
  const toml::table *lib_table = doc["lib"].as_table();
  if (lib_table == nullptr)
  {
    return std::nullopt;
  }

  spio::LibTarget target;
  target.path = RequireValue<std::string>(*lib_table, "path", "[lib]");
  ValidateRelativePath(target.path, "[lib].path");
  return target;
}

std::vector<spio::BinTarget> ParseBins(const toml::table &doc)
{
  std::vector<spio::BinTarget> bins;
  const toml::array *bin_entries = doc["bin"].as_array();
  if (bin_entries == nullptr)
  {
    return bins;
  }

  std::set<std::string> seen_names;
  bins.reserve(bin_entries->size());
  for (const toml::node &node : *bin_entries)
  {
    const toml::table *bin = node.as_table();
    if (bin == nullptr)
    {
      throw spio::ValidationError("[[bin]] entries must be tables");
    }

    spio::BinTarget target;
    target.name = RequireValue<std::string>(*bin, "name", "[[bin]]");
    target.path = RequireValue<std::string>(*bin, "path", "[[bin]]");
    if (target.name.empty())
    {
      throw spio::ValidationError("[[bin]].name must be a non-empty string");
    }
    ValidateRelativePath(target.path, "[[bin]].path");
    if (!seen_names.insert(target.name).second)
    {
      throw spio::ValidationError("duplicate [[bin]].name in package manifest: " + target.name);
    }

    bins.push_back(std::move(target));
  }

  return bins;
}

std::vector<spio::TestTarget> ParseTests(const toml::table &doc)
{
  std::vector<spio::TestTarget> tests;
  const toml::array *test_entries = doc["test"].as_array();
  if (test_entries == nullptr)
  {
    return tests;
  }

  std::set<std::string> seen_names;
  tests.reserve(test_entries->size());
  for (const toml::node &node : *test_entries)
  {
    const toml::table *test = node.as_table();
    if (test == nullptr)
    {
      throw spio::ValidationError("[[test]] entries must be tables");
    }

    spio::TestTarget target;
    target.name = RequireValue<std::string>(*test, "name", "[[test]]");
    target.path = RequireValue<std::string>(*test, "path", "[[test]]");
    if (target.name.empty())
    {
      throw spio::ValidationError("[[test]].name must be a non-empty string");
    }
    ValidateRelativePath(target.path, "[[test]].path");
    if (!seen_names.insert(target.name).second)
    {
      throw spio::ValidationError("duplicate [[test]].name in package manifest: " + target.name);
    }

    tests.push_back(std::move(target));
  }

  return tests;
}

spio::PackageConfig ParsePackage(const toml::table &doc)
{
  const toml::table &package_table = RequireTable(doc, "package", "manifest");

  spio::PackageConfig package;
  package.name = RequireValue<std::string>(package_table, "name", "[package]");
  package.version = RequireValue<std::string>(package_table, "version", "[package]");
  package.edition = RequireValue<std::string>(package_table, "edition", "[package]");
  package.publish = OptionalBool(package_table, "publish").value_or(false);
  package.toolchain = ParseToolchain(doc);
  package.lib = ParseLib(doc);
  package.bins = ParseBins(doc);
  package.tests = ParseTests(doc);
  package.dependencies = ParseDependencies(doc, "dependencies");
  package.dev_dependencies = ParseDependencies(doc, "dev-dependencies");

  ValidatePackageName(package.name, "package.name");
  ValidateSemver(package.version, "package.version");
  if (package.edition.empty())
  {
    throw spio::ValidationError("package.edition must be an explicit string");
  }
  if (!package.lib.has_value() && package.bins.empty() && package.tests.empty())
  {
    throw spio::ValidationError("package manifests must define at least one explicit [lib], [[bin]], or [[test]] target");
  }

  return package;
}

spio::WorkspaceConfig ParseWorkspace(const toml::table &doc)
{
  const toml::table &workspace_table = RequireTable(doc, "workspace", "manifest");

  spio::WorkspaceConfig workspace;
  workspace.members = RequireStringArray(workspace_table, "members", "[workspace]", true);
  workspace.exclude = workspace_table.contains("exclude") ? RequireStringArray(workspace_table, "exclude", "[workspace]", false) : std::vector<std::string>{};
  workspace.resolver = RequireValue<std::string>(workspace_table, "resolver", "[workspace]");

  if (workspace.resolver != "1")
  {
    throw spio::ValidationError("[workspace].resolver must be \"1\"");
  }
  for (const std::string &member : workspace.members)
  {
    ValidateRelativePath(member, "[workspace].members entry");
  }
  for (const std::string &entry : workspace.exclude)
  {
    ValidateRelativePath(entry, "[workspace].exclude entry");
  }

  return workspace;
}

std::string PackageShortName(const std::string &package_name)
{
  const auto slash = package_name.find('/');
  if (slash == std::string::npos || slash + 1 >= package_name.size())
  {
    throw spio::ValidationError("package.name must match namespace/name");
  }
  return package_name.substr(slash + 1);
}

std::string QuoteTomlString(const std::string &value)
{
  std::ostringstream out;
  out << std::quoted(value);
  return out.str();
}

std::string QuoteTomlKey(const std::string &value)
{
  if (std::regex_match(value, BareKeyRegex()))
  {
    return value;
  }
  return QuoteTomlString(value);
}

template <typename T, typename Formatter>
void EmitStringArray(std::ostringstream &out, const std::string &key, const std::vector<T> &values, Formatter formatter)
{
  out << key << " = [";
  for (size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
    {
      out << ", ";
    }
    out << formatter(values[index]);
  }
  out << "]\n";
}

void EmitDependencySection(std::ostringstream &out, const std::string &section_name, const std::vector<spio::Dependency> &dependencies)
{
  if (dependencies.empty())
  {
    return;
  }

  std::vector<spio::Dependency> ordered = dependencies;
  std::sort(
      ordered.begin(),
      ordered.end(),
      [](const spio::Dependency &left, const spio::Dependency &right) {
        return left.alias < right.alias;
      });

  out << "[" << section_name << "]\n";
  for (const spio::Dependency &dependency : ordered)
  {
    out << QuoteTomlKey(dependency.alias) << " = { ";
    bool first_field = true;
    auto emit_field = [&](const std::string &label, const std::string &value) {
      if (!first_field)
      {
        out << ", ";
      }
      out << label << " = " << QuoteTomlString(value);
      first_field = false;
    };

    if (dependency.package.has_value())
    {
      emit_field("package", *dependency.package);
    }
    if (dependency.source_kind == spio::DependencySourceKind::kPath)
    {
      emit_field("path", dependency.source);
    }
    else
    {
      if (dependency.source_kind == spio::DependencySourceKind::kGit)
      {
        emit_field("git", dependency.source);
        emit_field("rev", dependency.rev.value_or(""));
      }
      else
      {
        emit_field("version", dependency.version.value_or(""));
        emit_field("registry", dependency.source);
      }
    }
    out << " }\n";
  }
}

spio::ManifestDocument BuildScaffoldManifest(const std::string &package_name, const std::string &kind)
{
  ValidatePackageName(package_name, "package.name");

  spio::PackageConfig package;
  package.name = package_name;
  package.version = "0.1.0";
  package.edition = "2026";
  package.publish = false;
  package.toolchain = {
      .channel = "nightly",
      .implicit_std = true,
  };

  if (kind == "lib")
  {
    package.lib = spio::LibTarget{.path = "src/lib.styio"};
  }
  else
  {
    package.bins.push_back(spio::BinTarget{
        .name = PackageShortName(package_name),
        .path = "src/main.styio",
    });
  }

  return spio::ManifestDocument{
      .package = package,
      .workspace = std::nullopt,
  };
}

std::string RenderLibSource()
{
  return "# add := (a: i32, b: i32) => a + b\n";
}

std::string RenderMainSource()
{
  return ">_(\"hello from native spio bootstrap\")\n";
}

}  // namespace

namespace spio
{

ManifestDocument LoadManifest(const fs::path &manifest_path)
{
  toml::table doc;
  try
  {
    doc = toml::parse_file(manifest_path.string());
  }
  catch (const toml::parse_error &err)
  {
    throw ValidationError("failed to parse manifest '" + manifest_path.string() + "': " + std::string(err.description()));
  }

  const toml::table &spio_table = RequireTable(doc, "spio", "manifest");
  const auto manifest_version = spio_table["manifest-version"].value<int64_t>();
  if (!manifest_version.has_value() || *manifest_version != 1)
  {
    throw ValidationError("spio manifest-version must be 1");
  }

  const bool has_package = doc.contains("package");
  const bool has_workspace = doc.contains("workspace");
  if (!has_package && !has_workspace)
  {
    throw ValidationError("manifest must contain [package], [workspace], or both");
  }

  ManifestDocument parsed;
  if (has_package)
  {
    parsed.package = ParsePackage(doc);
  }
  if (has_workspace)
  {
    parsed.workspace = ParseWorkspace(doc);
  }
  return parsed;
}

std::string SerializeManifestCanonical(const ManifestDocument &manifest)
{
  std::ostringstream out;
  out << "[spio]\n";
  out << "manifest-version = 1\n";

  if (manifest.package.has_value())
  {
    const PackageConfig &package = *manifest.package;
    out << "\n[package]\n";
    out << "name = " << QuoteTomlString(package.name) << "\n";
    out << "version = " << QuoteTomlString(package.version) << "\n";
    out << "edition = " << QuoteTomlString(package.edition) << "\n";
    out << "publish = " << (package.publish ? "true" : "false") << "\n";

    out << "\n[toolchain]\n";
    out << "channel = " << QuoteTomlString(package.toolchain.channel) << "\n";
    out << "implicit-std = " << (package.toolchain.implicit_std ? "true" : "false") << "\n";

    if (package.lib.has_value())
    {
      out << "\n[lib]\n";
      out << "path = " << QuoteTomlString(package.lib->path) << "\n";
    }

    if (!package.bins.empty())
    {
      std::vector<BinTarget> bins = package.bins;
      std::sort(
          bins.begin(),
          bins.end(),
          [](const BinTarget &left, const BinTarget &right) {
            return left.name < right.name;
          });

      for (const BinTarget &bin : bins)
      {
        out << "\n[[bin]]\n";
        out << "name = " << QuoteTomlString(bin.name) << "\n";
        out << "path = " << QuoteTomlString(bin.path) << "\n";
      }
    }

    if (!package.tests.empty())
    {
      std::vector<TestTarget> tests = package.tests;
      std::sort(
          tests.begin(),
          tests.end(),
          [](const TestTarget &left, const TestTarget &right) {
            return left.name < right.name;
          });

      for (const TestTarget &test : tests)
      {
        out << "\n[[test]]\n";
        out << "name = " << QuoteTomlString(test.name) << "\n";
        out << "path = " << QuoteTomlString(test.path) << "\n";
      }
    }

    if (!package.dependencies.empty())
    {
      out << "\n";
      EmitDependencySection(out, "dependencies", package.dependencies);
    }
    if (!package.dev_dependencies.empty())
    {
      out << "\n";
      EmitDependencySection(out, "dev-dependencies", package.dev_dependencies);
    }
  }

  if (manifest.workspace.has_value())
  {
    const WorkspaceConfig &workspace = *manifest.workspace;
    out << "\n[workspace]\n";
    EmitStringArray(
        out,
        "members",
        workspace.members,
        [](const std::string &value) {
          return QuoteTomlString(value);
        });
    if (!workspace.exclude.empty())
    {
      EmitStringArray(
          out,
          "exclude",
          workspace.exclude,
          [](const std::string &value) {
            return QuoteTomlString(value);
          });
    }
    out << "resolver = " << QuoteTomlString(workspace.resolver) << "\n";
  }

  return out.str();
}

std::string InferLocalPackageName(const fs::path &root)
{
  return "local/" + root.filename().string();
}

void InitializeProject(const InitOptions &options)
{
  fs::create_directories(options.root);

  const fs::path manifest_path = options.root / "spio.toml";
  const fs::path src_dir = options.root / "src";
  fs::create_directories(src_dir);

  if (fs::exists(manifest_path))
  {
    throw std::runtime_error("manifest already exists: " + manifest_path.string());
  }

  {
    std::ofstream manifest(manifest_path);
    manifest << SerializeManifestCanonical(BuildScaffoldManifest(options.package_name, options.kind));
  }

  const fs::path source_path = options.kind == "lib" ? src_dir / "lib.styio" : src_dir / "main.styio";
  std::ofstream source(source_path);
  source << (options.kind == "lib" ? RenderLibSource() : RenderMainSource());
}

}  // namespace spio
