#include "SpioResolve/ProjectGraphContract.hpp"

#include "SpioCLI/Support.hpp"
#include "SpioCloud/Contract.hpp"
#include "SpioCloud/Job.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioManifest/Lockfile.hpp"
#include "SpioTool/Contract.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

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

json BuildPackagePayload(const spio::ResolvedPackage &package)
{
  json payload = {
      {"id", package.id},
      {"manifest_path", package.manifest_path.string()},
      {"root_dir", package.root_dir.string()},
      {"name", package.package.name},
      {"version", package.package.version},
      {"edition", package.package.edition},
      {"publish", package.package.publish},
      {"source_kind", package.source_kind},
      {"dependencies", package.dependencies},
  };
  if (package.git.has_value())
  {
    payload["git"] = *package.git;
  }
  if (package.rev.has_value())
  {
    payload["rev"] = *package.rev;
  }
  if (package.registry.has_value())
  {
    payload["registry"] = *package.registry;
  }
  if (package.sha256.has_value())
  {
    payload["sha256"] = *package.sha256;
  }
  if (package.snapshot_root.has_value())
  {
    payload["snapshot_root"] = package.snapshot_root->string();
  }

  json aliases = json::array();
  for (const auto &alias : package.dependency_aliases)
  {
    aliases.push_back({
        {"alias", alias.alias},
        {"package_id", alias.package_id},
    });
  }
  payload["dependency_aliases"] = aliases;
  return payload;
}

json BuildTargetPayload(const spio::ResolvedPackage &package)
{
  json payload = {
      {"package_id", package.id},
      {"lib", nullptr},
      {"bins", json::array()},
      {"tests", json::array()},
  };
  if (package.package.lib.has_value())
  {
    payload["lib"] = spio::CanonicalAbsolutePath(package.root_dir / package.package.lib->path).string();
  }
  for (const auto &bin : package.package.bins)
  {
    payload["bins"].push_back({
        {"name", bin.name},
        {"path", spio::CanonicalAbsolutePath(package.root_dir / bin.path).string()},
    });
  }
  for (const auto &test : package.package.tests)
  {
    payload["tests"].push_back({
        {"name", test.name},
        {"path", spio::CanonicalAbsolutePath(package.root_dir / test.path).string()},
    });
  }
  return payload;
}

json BuildDependencyEdgesPayload(const spio::ResolvedGraphResult &graph)
{
  json edges = json::array();
  for (const auto &package : graph.packages)
  {
    for (const auto &alias : package.dependency_aliases)
    {
      edges.push_back({
          {"from", package.id},
          {"to", alias.package_id},
          {"alias", alias.alias},
      });
    }
  }
  return edges;
}

json BuildPackageDistributionPayload(const spio::ResolvedGraphResult &graph)
{
  int path_count = 0;
  int git_count = 0;
  int registry_count = 0;
  int workspace_like = 0;
  for (const auto &package : graph.packages)
  {
    if (package.source_kind == "path")
    {
      ++path_count;
    }
    else if (package.source_kind == "git")
    {
      ++git_count;
    }
    else if (package.source_kind == "registry")
    {
      ++registry_count;
    }
    if (package.manifest_path.string().starts_with(graph.manifest_path.parent_path().string()))
    {
      ++workspace_like;
    }
  }
  return {
      {"root_package_count", static_cast<int>(graph.root_ids.size())},
      {"total_package_count", static_cast<int>(graph.packages.size())},
      {"path_packages", path_count},
      {"git_packages", git_count},
      {"registry_packages", registry_count},
      {"workspace_local_packages", workspace_like},
  };
}

json BuildSourceStatePayload(
    const fs::path &manifest_path,
    const spio::ProjectToolchainState &state,
    const fs::path &spio_home)
{
  json payload = {
      {"toolchain_mode", state.mode},
      {"source_origin", spio::ResolveSourceOriginBaseline()},
      {"source_state_root", spio::SourceStateRoot(spio_home).string()},
  };
  if (state.source_revision.has_value())
  {
    payload["source_revision"] = *state.source_revision;
  }
  if (const char *env_source_root = std::getenv("SPIO_STYIO_SOURCE_ROOT"); env_source_root != nullptr &&
                                                             env_source_root[0] != '\0')
  {
    payload["explicit_source_root"] = spio::CanonicalAbsolutePath(env_source_root).string();
  }
  payload["project_state_root"] = spio::ProjectStateRootForManifest(manifest_path).string();
  return payload;
}

}  // namespace

namespace spio
{

json BuildProjectGraphPayload(
    const fs::path &manifest_path,
    const ResolvedGraphResult &graph,
    const WorkflowFlags &workflow_flags,
    const ProjectToolchainState &toolchain_state,
    const CloudExecutionPolicy &cloud_policy,
    const ToolStatusResult &tool_status,
    const ResolveOptions &resolve_options)
{
  json packages = json::array();
  json targets = json::array();
  for (const auto &package : graph.packages)
  {
    packages.push_back(BuildPackagePayload(package));
    targets.push_back(BuildTargetPayload(package));
  }

  const fs::path lockfile_path = manifest_path.parent_path() / "spio.lock";
  const bool lock_exists = fs::exists(lockfile_path);
  bool lock_fresh = false;
  if (lock_exists)
  {
    const LockGenerationResult generated = ResolveSingleVersionLockfile(manifest_path, resolve_options);
    lock_fresh = (ReadFile(lockfile_path) == SerializeLockfileCanonical(generated.lockfile));
  }

  return {
      {"command", "project-graph"},
      {"manifest_path", CanonicalAbsolutePath(manifest_path).string()},
      {"root_ids", graph.root_ids},
      {"packages", packages},
      {"dependencies", BuildDependencyEdgesPayload(graph)},
      {"targets", targets},
      {"toolchain", {
                        {"mode", toolchain_state.mode},
                        {"channel", toolchain_state.channel},
                        {"build_mode", toolchain_state.build_mode},
                        {"risk_class", toolchain_state.risk_class},
                        {"preferred_execution_lane", toolchain_state.preferred_execution_lane},
                        {"security_profile", toolchain_state.security_profile},
                        {"cloud", SerializeCloudExecutionPolicy(cloud_policy)},
                    }},
      {"managed_toolchains", BuildToolStatusPayload(tool_status, toolchain_state, cloud_policy).at("managed_toolchains")},
      {"lock_state", {
                         {"path", lockfile_path.string()},
                         {"exists", lock_exists},
                         {"fresh", lock_fresh},
                         {"locked", workflow_flags.locked},
                         {"offline", workflow_flags.offline},
                     }},
      {"source_state", BuildSourceStatePayload(manifest_path, toolchain_state, tool_status.spio_home)},
      {"package_distribution", BuildPackageDistributionPayload(graph)},
  };
}

}  // namespace spio
