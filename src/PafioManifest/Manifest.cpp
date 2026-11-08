#include "PafioManifest/Manifest.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>

#include "PafioCore/ContractLexicon.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioManifest/TomlString.hpp"

namespace fs = std::filesystem;

namespace
{

const std::regex &
BareKeyRegex() {
  static const std::regex pattern("^[A-Za-z0-9_-]+$");
  return pattern;
}

bool
IsNonEmptyRelativePath(const std::string &value) {
  if (value.empty()) {
    return false;
  }

  const fs::path path(value);
  return !path.is_absolute();
}

// Build target paths (lib/bin/test) must stay inside the package snapshot;
// `..` components would let a manifest point the compiler outside it. Path
// dependencies and workspace member lists deliberately keep `..` segments.
// Rejecting a root path (not only `is_absolute()`) also covers drive-relative
// Windows targets like `D:src\main.styio`, which carry a root name but no
// root directory and would escape the package root.
bool
IsContainedRelativePath(const std::string &value) {
  if (value.empty()) {
    return false;
  }
  const fs::path path(value);
  if (path.has_root_path()) {
    return false;
  }
  for (const fs::path &component : path) {
    if (component == "..") {
      return false;
    }
  }
  return true;
}

bool
IsRegistryRootUrl(const std::string &value) {
  return value.starts_with("file://") || value.starts_with("http://") || value.starts_with("https://");
}

// A URL carrying userinfo (scheme://user[:pass]@host) embeds credentials in
// the source string; they would end up serialized into pafio.toml/pafio.lock,
// publish metadata, logs, or diagnostics. Authentication must come from a
// credential provider, never from the manifest text.
void
RequireNoUrlUserinfo(const std::string &value, std::string_view context) {
  if (pafio::UrlHasUserinfo(value)) {
    throw pafio::ValidationError(
        std::string(context) + " must not embed credentials in the URL userinfo; configure authentication separately");
  }
}

const toml::table &
RequireTable(const toml::table &parent, std::string_view key, std::string_view context) {
  if (const toml::table *table = parent[key].as_table()) {
    return *table;
  }

  throw pafio::ValidationError("missing or invalid [" + std::string(key) + "] table in " + std::string(context));
}

template <typename T>
T
RequireValue(const toml::table &table, std::string_view key, std::string_view context) {
  if (const auto value = table[key].value<T>()) {
    return *value;
  }

  throw pafio::ValidationError("missing or invalid '" + std::string(key) + "' in " + std::string(context));
}

std::optional<bool>
OptionalBool(const toml::table &table, std::string_view key) {
  if (const auto value = table[key].value<bool>()) {
    return *value;
  }
  return std::nullopt;
}

std::optional<std::string>
OptionalString(const toml::table &table, std::string_view key) {
  if (const auto value = table[key].value<std::string>()) {
    return *value;
  }
  return std::nullopt;
}

std::vector<std::string>
RequireStringArray(const toml::table &table, std::string_view key, std::string_view context, bool require_non_empty) {
  const toml::array *array = table[key].as_array();
  if (array == nullptr) {
    throw pafio::ValidationError("missing or invalid '" + std::string(key) + "' array in " + std::string(context));
  }

  std::vector<std::string> values;
  values.reserve(array->size());
  for (const toml::node &node : *array) {
    const auto value = node.value<std::string>();
    if (!value.has_value()) {
      throw pafio::ValidationError("array '" + std::string(key) + "' in " + std::string(context) + " must contain only strings");
    }
    values.push_back(*value);
  }

  if (require_non_empty && values.empty()) {
    throw pafio::ValidationError("array '" + std::string(key) + "' in " + std::string(context) + " must not be empty");
  }

  return values;
}

void
ValidateRelativePath(const std::string &path_value, std::string_view context) {
  if (!IsNonEmptyRelativePath(path_value)) {
    throw pafio::ValidationError(std::string(context) + " must be an explicit relative path");
  }
}

void
ValidateContainedRelativePath(const std::string &path_value, std::string_view context) {
  if (!IsContainedRelativePath(path_value)) {
    throw pafio::ValidationError(std::string(context) + " must be an explicit relative path inside the package");
  }
}

void
ValidateRegistryRoot(const std::string &registry_root, std::string_view context) {
  if (!IsRegistryRootUrl(registry_root)) {
    throw pafio::ValidationError(std::string(context) + " must use file://, http://, or https://");
  }
  RequireNoUrlUserinfo(registry_root, context);
}

std::vector<pafio::Dependency>
ParseDependencies(const toml::table &doc, std::string_view table_name) {
  const toml::table *dependencies = doc[table_name].as_table();
  if (dependencies == nullptr) {
    return {};
  }

  std::vector<pafio::Dependency> parsed;
  parsed.reserve(dependencies->size());
  for (const auto &[alias_key, node] : *dependencies) {
    const std::string alias = std::string(alias_key.str());
    const toml::table *dep = node.as_table();
    if (dep == nullptr) {
      throw pafio::ValidationError("dependency '" + alias + "' in [" + std::string(table_name) + "] must be an inline table");
    }

    const bool has_path = dep->contains("path");
    const bool has_git = dep->contains("git");
    const bool has_version = dep->contains("version");
    const bool has_registry = dep->contains("registry");
    const bool has_registry_source = has_version || has_registry;
    const int source_kind_count = static_cast<int>(has_path) + static_cast<int>(has_git) + static_cast<int>(has_registry_source);
    if (source_kind_count != 1) {
      throw pafio::ValidationError("dependency '" + alias + "' in [" + std::string(table_name) + "] must declare exactly one source kind");
    }

    pafio::Dependency dependency;
    dependency.alias = alias;
    dependency.package = OptionalString(*dep, "package");
    if (dependency.package.has_value()) {
      pafio::RequirePackageName(*dependency.package, "dependency '" + alias + "' package");
    }

    if (has_path) {
      dependency.source_kind = pafio::DependencySourceKind::kPath;
      dependency.source = RequireValue<std::string>(*dep, "path", "dependency '" + alias + "'");
      ValidateRelativePath(dependency.source, "dependency '" + alias + "' path");
    }
    else if (has_git) {
      dependency.source_kind = pafio::DependencySourceKind::kGit;
      dependency.source = RequireValue<std::string>(*dep, "git", "dependency '" + alias + "'");
      if (dependency.source.empty()) {
        throw pafio::ValidationError("dependency '" + alias + "' git must be a non-empty string");
      }
      RequireNoUrlUserinfo(dependency.source, "dependency '" + alias + "' git");
      dependency.rev = RequireValue<std::string>(*dep, "rev", "dependency '" + alias + "'");
      pafio::RequireGitCommitId(*dependency.rev, "dependency '" + alias + "' rev");
    }
    else {
      if (!dependency.package.has_value()) {
        throw pafio::ValidationError("dependency '" + alias + "' must declare package = \"namespace/name\" for registry sources");
      }
      dependency.source_kind = pafio::DependencySourceKind::kRegistry;
      dependency.version = RequireValue<std::string>(*dep, "version", "dependency '" + alias + "'");
      pafio::RequireExactSemver(*dependency.version, "dependency '" + alias + "' version");
      dependency.source = RequireValue<std::string>(*dep, "registry", "dependency '" + alias + "'");
      ValidateRegistryRoot(dependency.source, "dependency '" + alias + "' registry");
    }

    parsed.push_back(std::move(dependency));
  }

  return parsed;
}

pafio::BuildConfig
ParseBuildConfig(const toml::table &doc) {
  const toml::table &build_table = RequireTable(doc, "build", "package manifest");

  pafio::BuildConfig build;
  const auto implicit_std = OptionalBool(build_table, "implicit-std");
  if (!implicit_std.has_value()) {
    throw pafio::ValidationError("[build].implicit-std must be a boolean");
  }
  build.implicit_std = *implicit_std;
  return build;
}

std::optional<pafio::LibTarget>
ParseLib(const toml::table &doc) {
  const toml::table *lib_table = doc["lib"].as_table();
  if (lib_table == nullptr) {
    return std::nullopt;
  }

  pafio::LibTarget target;
  target.path = RequireValue<std::string>(*lib_table, "path", "[lib]");
  ValidateContainedRelativePath(target.path, "[lib].path");
  return target;
}

std::vector<pafio::BinTarget>
ParseBins(const toml::table &doc) {
  std::vector<pafio::BinTarget> bins;
  const toml::array *bin_entries = doc["bin"].as_array();
  if (bin_entries == nullptr) {
    return bins;
  }

  std::set<std::string> seen_names;
  bins.reserve(bin_entries->size());
  for (const toml::node &node : *bin_entries) {
    const toml::table *bin = node.as_table();
    if (bin == nullptr) {
      throw pafio::ValidationError("[[bin]] entries must be tables");
    }

    pafio::BinTarget target;
    target.name = RequireValue<std::string>(*bin, "name", "[[bin]]");
    target.path = RequireValue<std::string>(*bin, "path", "[[bin]]");
    if (target.name.empty()) {
      throw pafio::ValidationError("[[bin]].name must be a non-empty string");
    }
    ValidateContainedRelativePath(target.path, "[[bin]].path");
    if (!seen_names.insert(target.name).second) {
      throw pafio::ValidationError("duplicate [[bin]].name in package manifest: " + target.name);
    }

    bins.push_back(std::move(target));
  }

  return bins;
}

std::vector<pafio::TestTarget>
ParseTests(const toml::table &doc) {
  std::vector<pafio::TestTarget> tests;
  const toml::array *test_entries = doc["test"].as_array();
  if (test_entries == nullptr) {
    return tests;
  }

  std::set<std::string> seen_names;
  tests.reserve(test_entries->size());
  for (const toml::node &node : *test_entries) {
    const toml::table *test = node.as_table();
    if (test == nullptr) {
      throw pafio::ValidationError("[[test]] entries must be tables");
    }

    pafio::TestTarget target;
    target.name = RequireValue<std::string>(*test, "name", "[[test]]");
    target.path = RequireValue<std::string>(*test, "path", "[[test]]");
    if (target.name.empty()) {
      throw pafio::ValidationError("[[test]].name must be a non-empty string");
    }
    ValidateContainedRelativePath(target.path, "[[test]].path");
    if (!seen_names.insert(target.name).second) {
      throw pafio::ValidationError("duplicate [[test]].name in package manifest: " + target.name);
    }

    tests.push_back(std::move(target));
  }

  return tests;
}

pafio::PackageConfig
ParsePackage(const toml::table &doc) {
  const toml::table &package_table = RequireTable(doc, "package", "manifest");

  pafio::PackageConfig package;
  package.name = RequireValue<std::string>(package_table, "name", "[package]");
  package.version = RequireValue<std::string>(package_table, "version", "[package]");
  package.edition = RequireValue<std::string>(package_table, "edition", "[package]");
  package.publish = OptionalBool(package_table, "publish").value_or(false);
  package.build = ParseBuildConfig(doc);
  package.lib = ParseLib(doc);
  package.bins = ParseBins(doc);
  package.tests = ParseTests(doc);
  package.dependencies = ParseDependencies(doc, "dependencies");
  package.dev_dependencies = ParseDependencies(doc, "dev-dependencies");

  pafio::RequirePackageName(package.name, "package.name");
  pafio::RequireExactSemver(package.version, "package.version");
  if (package.edition.empty()) {
    throw pafio::ValidationError("package.edition must be an explicit string");
  }
  if (!package.lib.has_value() && package.bins.empty() && package.tests.empty()) {
    throw pafio::ValidationError("package manifests must define at least one explicit [lib], [[bin]], or [[test]] target");
  }

  return package;
}

pafio::WorkspaceConfig
ParseWorkspace(const toml::table &doc) {
  const toml::table &workspace_table = RequireTable(doc, "workspace", "manifest");

  pafio::WorkspaceConfig workspace;
  workspace.members = RequireStringArray(workspace_table, "members", "[workspace]", true);
  workspace.exclude = workspace_table.contains("exclude") ? RequireStringArray(workspace_table, "exclude", "[workspace]", false) : std::vector<std::string>{};
  workspace.resolver = RequireValue<std::string>(workspace_table, "resolver", "[workspace]");

  if (workspace.resolver != "1") {
    throw pafio::ValidationError("[workspace].resolver must be \"1\"");
  }
  for (const std::string &member : workspace.members) {
    ValidateRelativePath(member, "[workspace].members entry");
  }
  for (const std::string &entry : workspace.exclude) {
    ValidateRelativePath(entry, "[workspace].exclude entry");
  }

  return workspace;
}

std::string
QuoteTomlKey(const std::string &value) {
  if (std::regex_match(value, BareKeyRegex())) {
    return value;
  }
  return pafio::QuoteTomlString(value);
}

template <typename T, typename Formatter>
void
EmitStringArray(std::ostringstream &out, const std::string &key, const std::vector<T> &values, Formatter formatter) {
  out << key << " = [";
  for (size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      out << ", ";
    }
    out << formatter(values[index]);
  }
  out << "]\n";
}

void
EmitDependencySection(std::ostringstream &out, const std::string &section_name, const std::vector<pafio::Dependency> &dependencies) {
  if (dependencies.empty()) {
    return;
  }

  std::vector<pafio::Dependency> ordered = dependencies;
  std::sort(
    ordered.begin(),
    ordered.end(),
    [](const pafio::Dependency &left, const pafio::Dependency &right)
    {
      return left.alias < right.alias;
    }
  );

  out << "[" << section_name << "]\n";
  for (const pafio::Dependency &dependency : ordered) {
    out << QuoteTomlKey(dependency.alias) << " = { ";
    bool first_field = true;
    auto emit_field = [&](const std::string &label, const std::string &value)
    {
      if (!first_field) {
        out << ", ";
      }
      out << label << " = " << pafio::QuoteTomlString(value);
      first_field = false;
    };

    if (dependency.package.has_value()) {
      emit_field("package", *dependency.package);
    }
    if (dependency.source_kind == pafio::DependencySourceKind::kPath) {
      emit_field("path", dependency.source);
    }
    else {
      if (dependency.source_kind == pafio::DependencySourceKind::kGit) {
        emit_field("git", dependency.source);
        emit_field("rev", dependency.rev.value_or(""));
      }
      else {
        emit_field("version", dependency.version.value_or(""));
        emit_field("registry", dependency.source);
      }
    }
    out << " }\n";
  }
}

pafio::ManifestDocument
BuildScaffoldManifest(const std::string &package_name, const std::string &kind) {
  pafio::RequirePackageName(package_name, "package.name");

  pafio::PackageConfig package;
  package.name = package_name;
  package.version = "0.1.0";
  package.edition = "2026";
  package.publish = false;
  package.build = {
    .implicit_std = true,
  };

  if (kind == "lib") {
    package.lib = pafio::LibTarget{.path = "src/lib.styio"};
  }
  else {
    package.bins.push_back(pafio::BinTarget{
      .name = pafio::PackageShortName(package_name),
      .path = "src/main.styio",
    });
  }

  return pafio::ManifestDocument{
    .package = package,
    .workspace = std::nullopt,
  };
}

std::string
RenderLibSource() {
  return "# add := (a: i32, b: i32) => a + b\n";
}

std::string
RenderMainSource() {
  return ">_(\"hello from native pafio bootstrap\")\n";
}

}  // namespace

namespace pafio
{

ManifestDocument
LoadManifest(const fs::path &manifest_path) {
  toml::table doc;
  try {
    doc = toml::parse_file(manifest_path.string());
  }
  catch (const toml::parse_error &err) {
    throw ValidationError("failed to parse manifest '" + manifest_path.string() + "': " + std::string(err.description()));
  }

  const toml::table &pafio_table = RequireTable(doc, "pafio", "manifest");
  const auto manifest_version = pafio_table["manifest-version"].value<int64_t>();
  if (!manifest_version.has_value() || *manifest_version != 1) {
    throw ValidationError("pafio manifest-version must be 1");
  }

  const bool has_package = doc.contains("package");
  const bool has_workspace = doc.contains("workspace");
  if (!has_package && !has_workspace) {
    throw ValidationError("manifest must contain [package], [workspace], or both");
  }

  ManifestDocument parsed;
  if (has_package) {
    parsed.package = ParsePackage(doc);
  }
  if (has_workspace) {
    parsed.workspace = ParseWorkspace(doc);
  }
  return parsed;
}

std::string
SerializeManifestCanonical(const ManifestDocument &manifest) {
  std::ostringstream out;
  out << "[pafio]\n";
  out << "manifest-version = 1\n";

  if (manifest.package.has_value()) {
    const PackageConfig &package = *manifest.package;
    out << "\n[package]\n";
    out << "name = " << pafio::QuoteTomlString(package.name) << "\n";
    out << "version = " << pafio::QuoteTomlString(package.version) << "\n";
    out << "edition = " << pafio::QuoteTomlString(package.edition) << "\n";
    out << "publish = " << (package.publish ? "true" : "false") << "\n";

    out << "\n[build]\n";
    out << "implicit-std = " << (package.build.implicit_std ? "true" : "false") << "\n";

    if (package.lib.has_value()) {
      out << "\n[lib]\n";
      out << "path = " << pafio::QuoteTomlString(package.lib->path) << "\n";
    }

    if (!package.bins.empty()) {
      std::vector<BinTarget> bins = package.bins;
      std::sort(
        bins.begin(),
        bins.end(),
        [](const BinTarget &left, const BinTarget &right)
        {
          return left.name < right.name;
        }
      );

      for (const BinTarget &bin : bins) {
        out << "\n[[bin]]\n";
        out << "name = " << pafio::QuoteTomlString(bin.name) << "\n";
        out << "path = " << pafio::QuoteTomlString(bin.path) << "\n";
      }
    }

    if (!package.tests.empty()) {
      std::vector<TestTarget> tests = package.tests;
      std::sort(
        tests.begin(),
        tests.end(),
        [](const TestTarget &left, const TestTarget &right)
        {
          return left.name < right.name;
        }
      );

      for (const TestTarget &test : tests) {
        out << "\n[[test]]\n";
        out << "name = " << pafio::QuoteTomlString(test.name) << "\n";
        out << "path = " << pafio::QuoteTomlString(test.path) << "\n";
      }
    }

    if (!package.dependencies.empty()) {
      out << "\n";
      EmitDependencySection(out, "dependencies", package.dependencies);
    }
    if (!package.dev_dependencies.empty()) {
      out << "\n";
      EmitDependencySection(out, "dev-dependencies", package.dev_dependencies);
    }
  }

  if (manifest.workspace.has_value()) {
    const WorkspaceConfig &workspace = *manifest.workspace;
    out << "\n[workspace]\n";
    EmitStringArray(
      out,
      "members",
      workspace.members,
      [](const std::string &value)
      {
        return pafio::QuoteTomlString(value);
      }
    );
    if (!workspace.exclude.empty()) {
      EmitStringArray(
        out,
        "exclude",
        workspace.exclude,
        [](const std::string &value)
        {
          return pafio::QuoteTomlString(value);
        }
      );
    }
    out << "resolver = " << pafio::QuoteTomlString(workspace.resolver) << "\n";
  }

  return out.str();
}

std::string
InferLocalPackageName(const fs::path &root) {
  return "local/" + root.filename().string();
}

void
InitializeProject(const InitOptions &options) {
  fs::create_directories(options.root);

  const fs::path manifest_path = options.root / "pafio.toml";
  const fs::path src_dir = options.root / "src";
  fs::create_directories(src_dir);

  if (fs::exists(manifest_path)) {
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

}  // namespace pafio
