#include "PafioApp/WorkflowApp.hpp"

#include "PafioCLI/Support.hpp"
#include "PafioCloud/Contract.hpp"
#include "PafioCloud/Execution.hpp"
#include "PafioCloud/Job.hpp"
#include "PafioCompat/Compat.hpp"
#include "PafioCore/Process.hpp"
#include "PafioManifest/Lockfile.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioResolve/ProjectGraphContract.hpp"
#include "PafioResolve/Resolver.hpp"
#include "PafioTool/Contract.hpp"
#include "PafioTool/Install.hpp"
#include "PafioToolchain/SourceBuild.hpp"
#include "PafioToolchain/State.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace pafio
{

std::optional<std::string> ValidateCompilePlanMaterialization(const BuildPlanResult &plan)
{
  std::vector<std::string> missing;
  const auto require_directory = [&missing](const fs::path &path, const std::string &label) {
    if (!fs::is_directory(path))
    {
      missing.push_back(label + "=" + path.string());
    }
  };
  const auto require_file = [&missing](const fs::path &path, const std::string &label) {
    if (!fs::is_regular_file(path))
    {
      missing.push_back(label + "=" + path.string());
    }
  };

  require_directory(plan.build_root, "outputs.build_root");
  require_directory(plan.artifact_dir, "outputs.artifact_dir");
  require_directory(plan.diag_dir, "outputs.diag_dir");
  require_file(plan.build_root / "receipt.json", "receipt");

  if (missing.empty())
  {
    return std::nullopt;
  }

  std::string detail = missing.front();
  for (size_t index = 1; index < missing.size(); ++index)
  {
    detail += ", " + missing[index];
  }
  return detail;
}

int HandleProjectGraph(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("project-graph");
  }

  bool local_json = as_json;
  fs::path manifest_path = "pafio.toml";
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--json")
    {
      local_json = true;
    }
    else if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "project-graph"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for project-graph: " + args[index], "project-graph"}, as_json);
    }
  }
  if (!local_json)
  {
    return EmitError({"UsageError", kExitUsage, "project-graph currently requires --json", "project-graph"}, as_json);
  }

  try
  {
    (void) LoadManifest(manifest_path);
    const ProjectToolchainState toolchain_state = LoadProjectToolchainState(manifest_path);
    const CloudExecutionPolicy cloud_policy = ResolveCloudExecutionPolicy(toolchain_state);
    const ToolStatusResult tool_status = QueryToolStatus(manifest_path);
    const ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
    if (const auto lock_policy_error = ValidateLockedPolicy(manifest_path, "project-graph", workflow_flags, resolve_options);
        lock_policy_error.has_value())
    {
      return EmitError(*lock_policy_error, as_json);
    }
    const ResolvedGraphResult graph = ResolveSingleVersionGraph(manifest_path, resolve_options);
    return EmitSuccess(
        BuildProjectGraphPayload(
            manifest_path,
            graph,
            workflow_flags,
            toolchain_state,
            cloud_policy,
            tool_status,
            resolve_options),
        true);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "project-graph"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "project-graph"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "project-graph"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "project-graph"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "project-graph"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "project-graph"}, as_json);
  }
}

int HandleCheck(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("check");
  }
  fs::path manifest_path = "pafio.toml";
  std::optional<std::string> styio_bin;
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "check"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--styio-bin requires a value", "check"}, as_json);
      }
      styio_bin = args[index];
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for check: " + args[index], "check"}, as_json);
    }
  }

  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", kExitManifest, "manifest not found: " + manifest_path.string(), "check"}, as_json);
  }

  try
  {
    LoadManifest(manifest_path);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "check"}, as_json);
  }

  const ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
  if (const auto lock_policy_error = ValidateLockedPolicy(manifest_path, "check", workflow_flags, resolve_options);
      lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }

  const fs::path lockfile_path = manifest_path.parent_path() / "pafio.lock";
  LockGenerationResult generated;
  try
  {
    generated = ResolveSingleVersionLockfile(manifest_path, resolve_options);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "check"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "check"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "check"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "check"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "check"}, as_json);
  }

  if (fs::exists(lockfile_path))
  {
    try
    {
      LoadLockfile(lockfile_path);
      if (ReadFile(lockfile_path) != SerializeLockfileCanonical(generated.lockfile))
      {
        return EmitError({"LockfileError", kExitLock, "lockfile is stale: " + lockfile_path.string(), "check"}, as_json);
      }
    }
    catch (const ValidationError &err)
    {
      return EmitError({"LockfileError", kExitLock, err.what(), "check"}, as_json);
    }
    catch (const std::exception &err)
    {
      return EmitError({"LockfileError", kExitLock, err.what(), "check"}, as_json);
    }
  }

  json compatibility_payload = nullptr;
  std::optional<fs::path> compiler;
  try
  {
    compiler = ResolveStyioBinary(styio_bin, manifest_path);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "check"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "check"}, as_json);
  }

  if (compiler.has_value())
  {
    try
    {
      const CompatibilityReport report = CheckCompilerCompatibility(*compiler);
      compatibility_payload = {
          {"binary", report.binary.string()},
          {"compiler_version", report.compiler_version},
          {"compiler_channel", report.compiler_channel},
          {"compiler_edition_max", report.compiler_edition_max},
          {"integration_phase", report.integration_phase},
          {"supported_compile_plan_versions", report.supported_compile_plan_versions},
          {"capabilities", report.capabilities},
      };
    }
    catch (const CompilerProbeError &err)
    {
      return EmitError({"CompilerSpawnError", kExitCompilerSpawn, err.what(), "check"}, as_json);
    }
    catch (const CompatibilityError &err)
    {
      return EmitError({"ContractError", kExitContract, err.what(), "check"}, as_json);
    }
    catch (const ToolError &err)
    {
      return EmitError({"ToolError", kExitToolInstall, err.what(), "check"}, as_json);
    }
  }

  return EmitSuccess(
      {
          {"command", "check"},
          {"message", "manifest and lockfile look valid: " + manifest_path.string()},
          {"manifest_path", fs::absolute(manifest_path).string()},
          {"lockfile_present", fs::exists(lockfile_path)},
          {"packages", generated.lockfile.packages.size()},
          {"compiler_checked", compiler.has_value()},
          {"locked", workflow_flags.locked},
          {"offline", workflow_flags.offline},
          {"styio", compatibility_payload},
      },
      as_json);
}

int HandlePlanCommand(
    std::string_view command_name,
    std::string_view intent,
    bool allow_lib,
    bool allow_bin,
    bool allow_test,
    const std::vector<std::string> &args,
    bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage(command_name);
  }

  ParsedPlanInvocation parsed;
  if (const std::optional<CommandError> parse_error =
          ParsePlanInvocation(command_name, intent, allow_lib, allow_bin, allow_test, args, parsed);
      parse_error.has_value())
  {
    return EmitError(*parse_error, as_json);
  }

  BuildPlanRequest &request = parsed.request;
  std::optional<std::string> &styio_bin = parsed.styio_bin;
  bool &dry_run = parsed.dry_run;
  WorkflowFlags &workflow_flags = parsed.workflow_flags;
  SourceWorkflowFlags &source_flags = parsed.source_flags;

  ProjectToolchainState toolchain_state;
  try
  {
    toolchain_state = LoadProjectToolchainState(request.manifest_path);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), std::string(command_name)}, as_json);
  }

  if (request.build_mode.empty())
  {
    request.build_mode = toolchain_state.build_mode;
  }
  const CloudExecutionPolicy cloud_policy = ResolveCloudExecutionPolicy(toolchain_state);
  const WorkflowInvocationOptions invocation_options = {
      .locked = workflow_flags.locked,
      .offline = workflow_flags.offline,
      .styio_bin = styio_bin,
      .allow_fetch = source_flags.allow_fetch,
      .assume_yes = source_flags.assume_yes,
      .non_interactive = source_flags.non_interactive,
      .source_root = source_flags.source_root,
      .source_revision = source_flags.source_revision,
  };
  try
  {
    (void) BuildCloudBuildJobRequest(command_name, request, toolchain_state, invocation_options, cloud_policy);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"UsageError", kExitUsage, std::string(command_name) + " " + std::string(err.what()), std::string(command_name)}, as_json);
  }

  const ResolveOptions resolve_options = BuildResolveOptions(request.manifest_path, workflow_flags);
  if (const auto lock_policy_error = ValidateLockedPolicy(request.manifest_path, command_name, workflow_flags, resolve_options);
      lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }
  request.offline = workflow_flags.offline;
  request.vendor_root = resolve_options.vendor_root;

  std::optional<fs::path> compiler;
  json compatibility_payload = nullptr;
  if (!dry_run)
  {
    if (toolchain_state.mode == "binary")
    {
      try
      {
        compiler = ResolveStyioBinary(styio_bin, request.manifest_path);
      }
      catch (const ToolError &err)
      {
        return EmitError({"ToolError", kExitToolInstall, err.what(), std::string(command_name)}, as_json);
      }
      catch (const CacheError &err)
      {
        return EmitError({"CacheError", kExitCache, err.what(), std::string(command_name)}, as_json);
      }

      if (!compiler.has_value())
      {
        return EmitError(
            {"UsageError", kExitUsage, std::string(command_name) + " requires --styio-bin <path>, PAFIO_STYIO_BIN, a project toolchain pin, or a managed current compiler unless --dry-run is set", std::string(command_name)},
            as_json);
      }

      try
      {
        const CompatibilityReport report = CheckCompilerCompatibility(*compiler);
        compatibility_payload = {
            {"mode", "binary"},
            {"binary", report.binary.string()},
            {"compiler_version", report.compiler_version},
            {"compiler_channel", report.compiler_channel},
            {"compiler_edition_max", report.compiler_edition_max},
            {"integration_phase", report.integration_phase},
            {"supported_compile_plan_versions", report.supported_compile_plan_versions},
            {"capabilities", report.capabilities},
        };
        if (std::find(report.supported_compile_plan_versions.begin(), report.supported_compile_plan_versions.end(), 1) ==
            report.supported_compile_plan_versions.end())
        {
          return EmitError(
              {"ContractError", kExitContract, "active compatibility matrix does not allow compile-plan v1 " + std::string(command_name) + " for this compiler", std::string(command_name)},
              as_json);
        }
        request.compiler_version = report.compiler_version;
      }
      catch (const CompilerProbeError &err)
      {
        return EmitError({"CompilerSpawnError", kExitCompilerSpawn, err.what(), std::string(command_name)}, as_json);
      }
      catch (const CompatibilityError &err)
      {
        return EmitError({"ContractError", kExitContract, err.what(), std::string(command_name)}, as_json);
      }
      catch (const ToolError &err)
      {
        return EmitError({"ToolError", kExitToolInstall, err.what(), std::string(command_name)}, as_json);
      }
    }
    else
    {
      try
      {
        const SourceBuildResult source_result = EnsureSourceBuiltStyio({
            .manifest_path = request.manifest_path,
            .channel = toolchain_state.channel,
            .build_mode = request.build_mode,
            .explicit_source_root = source_flags.source_root,
            .source_revision = source_flags.source_revision.has_value() ? source_flags.source_revision : toolchain_state.source_revision,
            .allow_fetch = source_flags.allow_fetch,
            .offline = workflow_flags.offline,
            .assume_yes = source_flags.assume_yes,
            .non_interactive = source_flags.non_interactive,
        });
        compiler = source_result.compiler_binary;
        request.compiler_version = source_result.source_revision;
        compatibility_payload = {
            {"mode", "build"},
            {"source_root", source_result.source_root.string()},
            {"compiler_binary", source_result.compiler_binary.string()},
            {"source_revision", source_result.source_revision},
            {"channel", source_result.channel},
            {"build_mode", source_result.build_mode},
            {"fetched", source_result.fetched},
            {"built", source_result.built},
        };
        if (toolchain_state.source_revision != source_result.source_revision)
        {
          (void) UpdateProjectToolchainState({
              .manifest_path = request.manifest_path,
              .source_revision = source_result.source_revision,
          });
          toolchain_state.source_revision = source_result.source_revision;
        }
      }
      catch (const ValidationError &err)
      {
        return EmitError({"ManifestError", kExitManifest, err.what(), std::string(command_name)}, as_json);
      }
      catch (const ToolError &err)
      {
        return EmitError({"ToolError", kExitToolInstall, err.what(), std::string(command_name)}, as_json);
      }
      catch (const CacheError &err)
      {
        return EmitError({"CacheError", kExitCache, err.what(), std::string(command_name)}, as_json);
      }
    }
  }

  BuildPlanResult plan;
  try
  {
    plan = WriteBuildCompilePlan(request);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), std::string(command_name)}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), std::string(command_name)}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), std::string(command_name)}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), std::string(command_name)}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), std::string(command_name)}, as_json);
  }
  catch (const PlanError &err)
  {
    return EmitError({"PlanError", kExitPlan, err.what(), std::string(command_name)}, as_json);
  }

  if (dry_run)
  {
    return EmitSuccess(
        {
            {"command", std::string(command_name)},
            {"mode", "dry-run"},
            {"message", "wrote compile-plan: " + plan.plan_path.string()},
            {"manifest_path", plan.manifest_path.string()},
            {"workspace_root", plan.workspace_root.string()},
            {"toolchain_mode", toolchain_state.mode},
            {"channel", toolchain_state.channel},
            {"plan_path", plan.plan_path.string()},
            {"build_root", plan.build_root.string()},
            {"artifact_dir", plan.artifact_dir.string()},
            {"diag_dir", plan.diag_dir.string()},
            {"cache_key", plan.cache_key},
            {"packages", plan.package_count},
            {"entry", {
                          {"package", plan.entry_package_name},
                          {"package_id", plan.entry_package_id},
                          {"target_kind", plan.entry_target_kind},
                          {"target_name", plan.entry_target_name},
                      }},
            {"profile", plan.profile_name},
            {"build_mode", plan.build_mode},
            {"intent", request.intent},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
            {"cloud", SerializeCloudExecutionPolicy(cloud_policy)},
        },
        as_json);
  }

  try
  {
    const ProcessResult result = RunProcess({
        .program = compiler->string(),
        .args = {"--compile-plan", plan.plan_path.string()},
        .search_path = false,
        .timeout = kExternalProcessBuildTimeout,
        .error_context = "compiler compile-plan execution",
    });
    if (result.exit_code == 127)
    {
      return EmitError(
          {"CompilerSpawnError", kExitCompilerSpawn, "compiler failed to execute --compile-plan", std::string(command_name)},
          as_json);
    }
    if (result.exit_code != 0)
    {
      const std::string detail = DescribeProcessFailure(result);
      return EmitError(
          {"CompilerError", kExitCompiler, "compiler failed for compile-plan " + plan.plan_path.string() + (detail.empty() ? "" : ": " + detail), std::string(command_name)},
          as_json);
    }
    if (!as_json)
    {
      if (!result.stdout_text.empty())
      {
        std::cout << result.stdout_text;
      }
      if (!result.stderr_text.empty())
      {
        std::cerr << result.stderr_text;
      }
    }
    if (const std::optional<std::string> materialization_error = ValidateCompilePlanMaterialization(plan);
        materialization_error.has_value())
    {
      return EmitError(
          {"CompilerError", kExitCompiler, "compiler completed but did not materialize compile-plan outputs: " + *materialization_error, std::string(command_name)},
          as_json);
    }
  }
  catch (const std::exception &err)
  {
    return EmitError({"CompilerSpawnError", kExitCompilerSpawn, err.what(), std::string(command_name)}, as_json);
  }

  return EmitSuccess(
      {
          {"command", std::string(command_name)},
          {"mode", "execute"},
          {"message", "completed compiler " + std::string(command_name) + " via compile-plan: " + plan.plan_path.string()},
          {"manifest_path", plan.manifest_path.string()},
          {"workspace_root", plan.workspace_root.string()},
          {"toolchain_mode", toolchain_state.mode},
          {"channel", toolchain_state.channel},
          {"plan_path", plan.plan_path.string()},
          {"build_root", plan.build_root.string()},
          {"artifact_dir", plan.artifact_dir.string()},
          {"diag_dir", plan.diag_dir.string()},
          {"cache_key", plan.cache_key},
          {"packages", plan.package_count},
          {"entry", {
                        {"package", plan.entry_package_name},
                        {"package_id", plan.entry_package_id},
                        {"target_kind", plan.entry_target_kind},
                        {"target_name", plan.entry_target_name},
                    }},
          {"profile", plan.profile_name},
          {"build_mode", plan.build_mode},
          {"intent", request.intent},
          {"locked", workflow_flags.locked},
          {"offline", workflow_flags.offline},
          {"cloud", SerializeCloudExecutionPolicy(cloud_policy)},
          {"styio", compatibility_payload},
      },
      as_json);
}

int HandleBuild(const std::vector<std::string> &args, bool as_json)
{
  return HandlePlanCommand("build", "build", true, true, false, args, as_json);
}

int HandleRun(const std::vector<std::string> &args, bool as_json)
{
  return HandlePlanCommand("run", "run", false, true, false, args, as_json);
}

int HandleTest(const std::vector<std::string> &args, bool as_json)
{
  return HandlePlanCommand("test", "test", false, false, true, args, as_json);
}

}  // namespace pafio
