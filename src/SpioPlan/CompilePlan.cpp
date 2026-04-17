#include "SpioPlan/CompilePlan.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Version.hpp"
#include "SpioResolve/Resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

enum class VisitState
{
  kUnseen,
  kActive,
  kDone,
};

struct ProfileConfig
{
  std::string name;
  int opt_level = 0;
  bool debug = false;
  bool lto = false;
};

struct SelectedTarget
{
  const spio::ResolvedPackage *package = nullptr;
  std::string kind;
  std::string name;
  fs::path file;
};

void ValidateIntent(std::string_view intent)
{
  if (intent != "check" && intent != "build" && intent != "run" && intent != "test")
  {
    throw spio::PlanError("unsupported compile-plan intent for the current native core: " + std::string(intent));
  }
}

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
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

std::string ReadFile(const fs::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    throw spio::PlanError("failed to read file required for compile-plan: " + path.string());
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string Join(const std::vector<std::string> &parts, std::string_view separator)
{
  std::ostringstream out;
  for (size_t index = 0; index < parts.size(); ++index)
  {
    if (index > 0)
    {
      out << separator;
    }
    out << parts[index];
  }
  return out.str();
}

fs::path AbsoluteTargetPath(const spio::ResolvedPackage &package, const std::string &relative_path)
{
  return CanonicalAbsolutePath(package.root_dir / relative_path);
}

void RequireExistingSourceFile(const fs::path &path, std::string_view context)
{
  if (!fs::exists(path))
  {
    throw spio::PlanError(std::string(context) + " source file not found: " + path.string());
  }
  if (!fs::is_regular_file(path))
  {
    throw spio::PlanError(std::string(context) + " source path is not a file: " + path.string());
  }
}

const spio::ResolvedPackage &RequirePackageById(
    const std::unordered_map<std::string, const spio::ResolvedPackage *> &packages_by_id,
    const std::string &package_id)
{
  const auto found = packages_by_id.find(package_id);
  if (found == packages_by_id.end())
  {
    throw spio::PlanError("resolver graph references unknown package id: " + package_id);
  }
  return *found->second;
}

std::vector<const spio::ResolvedPackage *> CollectRootPackages(const spio::ResolvedGraphResult &graph)
{
  std::unordered_map<std::string, const spio::ResolvedPackage *> packages_by_id;
  for (const spio::ResolvedPackage &package : graph.packages)
  {
    packages_by_id.emplace(package.id, &package);
  }

  std::vector<const spio::ResolvedPackage *> roots;
  roots.reserve(graph.root_ids.size());
  for (const std::string &root_id : graph.root_ids)
  {
    roots.push_back(&RequirePackageById(packages_by_id, root_id));
  }
  std::sort(
      roots.begin(),
      roots.end(),
      [](const spio::ResolvedPackage *left, const spio::ResolvedPackage *right) {
        return left->id < right->id;
      });
  return roots;
}

const spio::ResolvedPackage &ResolveEntryPackage(const spio::BuildPlanRequest &request, const spio::ResolvedGraphResult &graph)
{
  const std::vector<const spio::ResolvedPackage *> roots = CollectRootPackages(graph);
  if (roots.empty())
  {
    throw spio::PlanError("compile-plan generation requires at least one root package");
  }

  if (request.package_name.has_value())
  {
    std::vector<const spio::ResolvedPackage *> matches;
    for (const spio::ResolvedPackage *candidate : roots)
    {
      if (candidate->package.name == *request.package_name)
      {
        matches.push_back(candidate);
      }
    }
    if (matches.empty())
    {
      throw spio::PlanError("selected package is not a root package of the active manifest: " + *request.package_name);
    }
    return *matches.front();
  }

  for (const spio::ResolvedPackage *candidate : roots)
  {
    if (candidate->manifest_path == graph.manifest_path)
    {
      return *candidate;
    }
  }

  if (roots.size() == 1U)
  {
    return *roots.front();
  }

  throw spio::PlanError("workspace build is ambiguous; select a root package with --package <namespace/name>");
}

SelectedTarget ResolveEntryTarget(const spio::BuildPlanRequest &request, const spio::ResolvedPackage &package)
{
  const bool require_binary = request.intent == "run";
  const bool require_test = request.intent == "test";
  if (request.select_lib && request.bin_name.has_value())
  {
    throw spio::PlanError(request.intent + " target selection cannot combine --lib and --bin");
  }
  if (request.select_lib && request.test_name.has_value())
  {
    throw spio::PlanError(request.intent + " target selection cannot combine --lib and --test");
  }
  if (request.bin_name.has_value() && request.test_name.has_value())
  {
    throw spio::PlanError(request.intent + " target selection cannot combine --bin and --test");
  }

  if (request.select_lib)
  {
    if (require_binary)
    {
      throw spio::PlanError("run requires a binary target and does not accept --lib");
    }
    if (require_test)
    {
      throw spio::PlanError("test requires an explicit [[test]] target and does not accept --lib");
    }
    if (!package.package.lib.has_value())
    {
      throw spio::PlanError("selected package does not define [lib]: " + package.package.name);
    }
    const fs::path file = AbsoluteTargetPath(package, package.package.lib->path);
    RequireExistingSourceFile(file, "library");
    return {
        .package = &package,
        .kind = "lib",
        .name = package.package.name,
        .file = file,
    };
  }

  if (request.bin_name.has_value())
  {
    for (const spio::BinTarget &bin : package.package.bins)
    {
      if (bin.name == *request.bin_name)
      {
        const fs::path file = AbsoluteTargetPath(package, bin.path);
        RequireExistingSourceFile(file, "binary");
        return {
            .package = &package,
            .kind = "bin",
            .name = bin.name,
            .file = file,
        };
      }
    }
    throw spio::PlanError("selected package does not define [[bin]].name = \"" + *request.bin_name + "\"");
  }

  if (request.test_name.has_value())
  {
    for (const spio::TestTarget &test : package.package.tests)
    {
      if (test.name == *request.test_name)
      {
        const fs::path file = AbsoluteTargetPath(package, test.path);
        RequireExistingSourceFile(file, "test");
        return {
            .package = &package,
            .kind = "test",
            .name = test.name,
            .file = file,
        };
      }
    }
    throw spio::PlanError("selected package does not define [[test]].name = \"" + *request.test_name + "\"");
  }

  const bool has_lib = package.package.lib.has_value();
  const size_t bin_count = package.package.bins.size();
  const size_t test_count = package.package.tests.size();
  if (require_test)
  {
    if (test_count == 1U)
    {
      const spio::TestTarget &test = package.package.tests.front();
      const fs::path file = AbsoluteTargetPath(package, test.path);
      RequireExistingSourceFile(file, "test");
      return {
          .package = &package,
          .kind = "test",
          .name = test.name,
          .file = file,
      };
    }
    if (test_count == 0U)
    {
      throw spio::PlanError("selected package does not define any [[test]] target: " + package.package.name);
    }
    throw spio::PlanError("test target is ambiguous for package '" + package.package.name + "'; select --test <name>");
  }
  if (require_binary)
  {
    if (bin_count == 1U)
    {
      const spio::BinTarget &bin = package.package.bins.front();
      const fs::path file = AbsoluteTargetPath(package, bin.path);
      RequireExistingSourceFile(file, "binary");
      return {
          .package = &package,
          .kind = "bin",
          .name = bin.name,
          .file = file,
      };
    }
    if (bin_count == 0U)
    {
      throw spio::PlanError("selected package does not define any [[bin]] target: " + package.package.name);
    }
    throw spio::PlanError("run target is ambiguous for package '" + package.package.name + "'; select --bin <name>");
  }
  if (has_lib && bin_count == 0U)
  {
    const fs::path file = AbsoluteTargetPath(package, package.package.lib->path);
    RequireExistingSourceFile(file, "library");
    return {
        .package = &package,
        .kind = "lib",
        .name = package.package.name,
        .file = file,
    };
  }
  if (!has_lib && bin_count == 1U)
  {
    const spio::BinTarget &bin = package.package.bins.front();
    const fs::path file = AbsoluteTargetPath(package, bin.path);
    RequireExistingSourceFile(file, "binary");
    return {
        .package = &package,
        .kind = "bin",
        .name = bin.name,
        .file = file,
    };
  }
  if (request.intent == "check" && !has_lib && bin_count == 0U && test_count == 1U)
  {
    const spio::TestTarget &test = package.package.tests.front();
    const fs::path file = AbsoluteTargetPath(package, test.path);
    RequireExistingSourceFile(file, "test");
    return {
        .package = &package,
        .kind = "test",
        .name = test.name,
        .file = file,
    };
  }

  std::string selection_hint = "--lib or --bin <name>";
  if (request.intent == "run")
  {
    selection_hint = "--bin <name>";
  }
  else if (request.intent == "test")
  {
    selection_hint = "--test <name>";
  }
  else if (request.intent == "check")
  {
    selection_hint = "--lib, --bin <name>, or --test <name>";
  }

  throw spio::PlanError(
      request.intent + " target is ambiguous for package '" + package.package.name + "'; select " + selection_hint);
}

ProfileConfig ResolveProfile(std::string_view profile_name)
{
  if (profile_name == "dev")
  {
    return {
        .name = "dev",
        .opt_level = 0,
        .debug = true,
        .lto = false,
    };
  }
  if (profile_name == "release")
  {
    return {
        .name = "release",
        .opt_level = 3,
        .debug = false,
        .lto = true,
    };
  }
  throw spio::PlanError("unsupported build profile: " + std::string(profile_name));
}

void ValidateUniformToolchain(const spio::ResolvedGraphResult &graph)
{
  if (graph.packages.empty())
  {
    throw spio::PlanError("compile-plan generation requires at least one resolved package");
  }

  const spio::ResolvedPackage &baseline = graph.packages.front();
  for (const spio::ResolvedPackage &package : graph.packages)
  {
    if (package.package.edition != baseline.package.edition)
    {
      throw spio::PlanError(
          "compile-plan v1 requires a uniform package.edition across the resolved graph");
    }
    if (package.package.toolchain.channel != baseline.package.toolchain.channel)
    {
      throw spio::PlanError(
          "compile-plan v1 requires a uniform [toolchain].channel across the resolved graph");
    }
    if (package.package.toolchain.implicit_std != baseline.package.toolchain.implicit_std)
    {
      throw spio::PlanError(
          "compile-plan v1 requires a uniform [toolchain].implicit-std across the resolved graph");
    }
  }
}

std::string BuildStdPackageId(const spio::ResolvedPackage &entry_package)
{
  const std::string prefix = entry_package.package.toolchain.implicit_std ? "builtin:std@" : "builtin:std-disabled@";
  return prefix + entry_package.package.toolchain.channel + "/" + entry_package.package.edition;
}

std::vector<std::string> BuildPackageOrder(const spio::ResolvedGraphResult &graph)
{
  std::unordered_map<std::string, const spio::ResolvedPackage *> packages_by_id;
  for (const spio::ResolvedPackage &package : graph.packages)
  {
    packages_by_id.emplace(package.id, &package);
  }

  std::unordered_map<std::string, VisitState> visit_state;
  std::vector<std::string> order;
  std::vector<std::string> stack;
  order.reserve(graph.packages.size());

  std::function<void(const std::string &)> visit = [&](const std::string &package_id) {
    const VisitState state = visit_state[package_id];
    if (state == VisitState::kDone)
    {
      return;
    }
    if (state == VisitState::kActive)
    {
      const auto cycle_start = std::find(stack.begin(), stack.end(), package_id);
      std::vector<std::string> cycle(cycle_start, stack.end());
      cycle.push_back(package_id);
      throw spio::PlanError("compile-plan requires an acyclic package graph: " + Join(cycle, " -> "));
    }

    visit_state[package_id] = VisitState::kActive;
    stack.push_back(package_id);

    const spio::ResolvedPackage &package = RequirePackageById(packages_by_id, package_id);
    for (const std::string &dependency_id : package.dependencies)
    {
      visit(dependency_id);
    }

    stack.pop_back();
    visit_state[package_id] = VisitState::kDone;
    order.push_back(package_id);
  };

  std::vector<std::string> package_ids;
  package_ids.reserve(graph.packages.size());
  for (const spio::ResolvedPackage &package : graph.packages)
  {
    package_ids.push_back(package.id);
  }
  std::sort(package_ids.begin(), package_ids.end());
  for (const std::string &package_id : package_ids)
  {
    visit(package_id);
  }
  return order;
}

std::string BuildSourceHash(const spio::ResolvedGraphResult &graph)
{
  std::vector<fs::path> files;
  for (const spio::ResolvedPackage &package : graph.packages)
  {
    files.push_back(package.manifest_path);
    if (package.package.lib.has_value())
    {
      files.push_back(AbsoluteTargetPath(package, package.package.lib->path));
    }
    for (const spio::BinTarget &bin : package.package.bins)
    {
      files.push_back(AbsoluteTargetPath(package, bin.path));
    }
    for (const spio::TestTarget &test : package.package.tests)
    {
      files.push_back(AbsoluteTargetPath(package, test.path));
    }
  }

  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());

  std::string digest_input;
  for (const fs::path &path : files)
  {
    RequireExistingSourceFile(path, "compile-plan");
    digest_input.append(path.generic_string());
    digest_input.push_back('\0');
    digest_input.append(ReadFile(path));
    digest_input.push_back('\0');
  }
  return Hex64(Fnv1a64(digest_input));
}

json BuildTargetsJson(const spio::ResolvedPackage &package)
{
  json bins = json::array();
  std::vector<spio::BinTarget> ordered_bins = package.package.bins;
  std::sort(
      ordered_bins.begin(),
      ordered_bins.end(),
      [](const spio::BinTarget &left, const spio::BinTarget &right) {
        return left.name < right.name;
      });
  for (const spio::BinTarget &bin : ordered_bins)
  {
    const fs::path absolute_path = AbsoluteTargetPath(package, bin.path);
    RequireExistingSourceFile(absolute_path, "binary");
    bins.push_back({
        {"name", bin.name},
        {"path", absolute_path.string()},
    });
  }

  json tests = json::array();
  std::vector<spio::TestTarget> ordered_tests = package.package.tests;
  std::sort(
      ordered_tests.begin(),
      ordered_tests.end(),
      [](const spio::TestTarget &left, const spio::TestTarget &right) {
        return left.name < right.name;
      });
  for (const spio::TestTarget &test : ordered_tests)
  {
    const fs::path absolute_path = AbsoluteTargetPath(package, test.path);
    RequireExistingSourceFile(absolute_path, "test");
    tests.push_back({
        {"name", test.name},
        {"path", absolute_path.string()},
    });
  }

  json targets{
      {"lib", nullptr},
      {"bins", std::move(bins)},
  };
  if (!tests.empty())
  {
    targets["tests"] = std::move(tests);
  }
  if (package.package.lib.has_value())
  {
    const fs::path absolute_path = AbsoluteTargetPath(package, package.package.lib->path);
    RequireExistingSourceFile(absolute_path, "library");
    targets["lib"] = absolute_path.string();
  }
  return targets;
}

json BuildPackageJson(const spio::ResolvedPackage &package)
{
  json dependency_aliases = json::object();
  for (const spio::ResolvedDependencyAlias &alias : package.dependency_aliases)
  {
    dependency_aliases[alias.alias] = alias.package_id;
  }

  json record{
      {"id", package.id},
      {"name", package.package.name},
      {"version", package.package.version},
      {"root_dir", package.root_dir.string()},
      {"manifest_path", package.manifest_path.string()},
      {"source_kind", package.source_kind},
      {"targets", BuildTargetsJson(package)},
      {"dependency_aliases", std::move(dependency_aliases)},
  };
  return record;
}

}  // namespace

namespace spio
{

BuildPlanResult WriteBuildCompilePlan(const BuildPlanRequest &request)
{
  if (!fs::exists(request.manifest_path))
  {
    throw PlanError("manifest not found: " + request.manifest_path.string());
  }
  ValidateIntent(request.intent);

  ResolveOptions resolve_options;
  resolve_options.offline = request.offline;
  resolve_options.vendor_root = request.vendor_root;
  const ResolvedGraphResult graph = ResolveSingleVersionGraph(request.manifest_path, resolve_options);
  ValidateUniformToolchain(graph);

  const ResolvedPackage &entry_package = ResolveEntryPackage(request, graph);
  const SelectedTarget entry_target = ResolveEntryTarget(request, entry_package);
  const ProfileConfig profile = ResolveProfile(request.profile);
  const std::vector<std::string> package_order = BuildPackageOrder(graph);
  const std::string source_hash = BuildSourceHash(graph);
  const std::string compiler_version = request.compiler_version.value_or("unbound");

  const std::string cache_material =
      "compiler=" + compiler_version +
      ";compile-plan=1" +
      ";intent=" + request.intent +
      ";edition=" + entry_package.package.edition +
      ";profile=" + profile.name +
      ";target=" + entry_package.id + ":" + entry_target.kind + ":" + entry_target.name +
      ";source=" + source_hash;
  const std::string cache_key = Hex64(Fnv1a64(cache_material));

  const fs::path workspace_root = CanonicalAbsolutePath(graph.manifest_path.parent_path());
  const fs::path build_root = workspace_root / ".spio" / "build" / cache_key;
  const fs::path artifact_dir = build_root / "artifacts";
  const fs::path diag_dir = build_root / "diag";
  const fs::path plan_path = build_root / "plan.json";

  json packages = json::array();
  for (const ResolvedPackage &package : graph.packages)
  {
    packages.push_back(BuildPackageJson(package));
  }

  json plan{
      {"plan_version", 1},
      {"generated_by", {
                           {"tool", "spio"},
                           {"version", std::string(kVersion)},
                       }},
      {"intent", request.intent},
      {"workspace_root", workspace_root.string()},
      {"entry", {
                    {"package_id", entry_package.id},
                    {"target_kind", entry_target.kind},
                    {"target_name", entry_target.name},
                    {"file", entry_target.file.string()},
                }},
      {"toolchain", {
                        {"channel", entry_package.package.toolchain.channel},
                        {"edition", entry_package.package.edition},
                        {"implicit_std", entry_package.package.toolchain.implicit_std},
                        {"std_package_id", BuildStdPackageId(entry_package)},
                    }},
      {"profile", {
                       {"name", profile.name},
                       {"opt_level", profile.opt_level},
                       {"debug", profile.debug},
                       {"lto", profile.lto},
                   }},
      {"packages", std::move(packages)},
      {"resolution", {
                         {"resolver", "single-version-v1"},
                         {"package_order", package_order},
                     }},
      {"outputs", {
                      {"build_root", build_root.string()},
                      {"artifact_dir", artifact_dir.string()},
                      {"diag_dir", diag_dir.string()},
                  }},
      {"emit", {
                   {"error_format", "jsonl"},
                   {"ast", false},
                   {"styio_ir", false},
                   {"llvm_ir", false},
               }},
  };

  fs::create_directories(artifact_dir);
  fs::create_directories(diag_dir);

  const std::string plan_json = plan.dump(2) + "\n";
  std::ofstream out(plan_path);
  if (!out)
  {
    throw PlanError("failed to open compile-plan for write: " + plan_path.string());
  }
  out << plan_json;
  if (!out.good())
  {
    throw PlanError("failed to write compile-plan: " + plan_path.string());
  }

  return {
      .manifest_path = graph.manifest_path,
      .workspace_root = workspace_root,
      .build_root = build_root,
      .artifact_dir = artifact_dir,
      .diag_dir = diag_dir,
      .plan_path = plan_path,
      .cache_key = cache_key,
      .plan_json = plan_json,
      .entry_package_id = entry_package.id,
      .entry_package_name = entry_package.package.name,
      .entry_target_kind = entry_target.kind,
      .entry_target_name = entry_target.name,
      .profile_name = profile.name,
      .package_count = graph.packages.size(),
  };
}

}  // namespace spio
