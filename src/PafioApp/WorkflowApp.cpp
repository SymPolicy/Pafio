#include "PafioApp/WorkflowApp.hpp"

#include "PafioCLI/Support.hpp"
#include "PafioCompat/Compat.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/Process.hpp"
#include "PafioPlan/CompilePlan.hpp"
#include "PafioResolve/MetadataContract.hpp"
#include "PafioResolve/Resolver.hpp"
#include "PafioWorkflow/Dependencies.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

json SyncPayload(
    const pafio::SyncProjectResult &result,
    const pafio::WorkflowFlags &flags)
{
  return {
      {"git_packages", result.git_package_count},
      {"lockfile_mode", result.lockfile_mode},
      {"lockfile_path", result.graph.lockfile_path.string()},
      {"locked", flags.locked},
      {"offline", flags.offline},
      {"packages", result.package_count},
      {"registry_packages", result.registry_package_count},
      {"resolution_path", result.resolution_path.string()},
      {"status", "succeeded"},
  };
}

json TargetPayload(const pafio::BuildPlanResult &plan)
{
  return {
      {"kind", plan.entry_target_kind},
      {"name", plan.entry_target_name},
      {"package", plan.entry_package_name},
      {"package_id", plan.entry_package_id},
  };
}

json PlanPayload(const pafio::BuildPlanResult &plan)
{
  return {
      {"artifact_dir", plan.artifact_dir.string()},
      {"build_root", plan.build_root.string()},
      {"cache_key", plan.cache_key},
      {"diag_dir", plan.diag_dir.string()},
      {"path", plan.plan_path.string()},
  };
}

int EmitWorkflowFailure(
    std::string_view action,
    const pafio::CommandError &error,
    bool as_json)
{
  return pafio::EmitError(
      {
          .category = error.category,
          .code = error.code,
          .message = error.message,
          .command = std::string(action),
      },
      as_json);
}

}  // namespace

namespace pafio
{

std::optional<std::string> ValidateCompilePlanMaterialization(
    const BuildPlanResult &plan)
{
  std::vector<std::string> missing;
  const auto require_directory =
      [&missing](const fs::path &path, const std::string &label) {
        if (!fs::is_directory(path))
        {
          missing.push_back(label + "=" + path.string());
        }
      };
  const auto require_file =
      [&missing](const fs::path &path, const std::string &label) {
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

int HandleMetadata(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("metadata");
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
        return EmitError(
            {"UsageError", kExitUsage, "--manifest-path requires a value",
             "metadata"},
            as_json);
      }
      manifest_path = args[index];
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError(
          {"UsageError", kExitUsage,
           "unexpected argument for metadata: " + args[index], "metadata"},
          as_json);
    }
  }
  if (!local_json)
  {
    return EmitError(
        {"UsageError", kExitUsage, "metadata requires --json", "metadata"},
        as_json);
  }

  const ResolveOptions options =
      BuildResolveOptions(manifest_path, workflow_flags);
  if (const std::optional<CommandError> lock_error =
          ValidateLockedPolicy(
              manifest_path, "metadata", workflow_flags, options);
      lock_error.has_value())
  {
    return EmitError(*lock_error, true);
  }

  try
  {
    const ResolvedGraphResult graph =
        ResolveSingleVersionGraph(manifest_path, options);
    std::cout
        << SerializeMetadataV1(
               BuildMetadataDocument(manifest_path, graph))
               .dump()
        << '\n';
    return kExitSuccess;
  }
  catch (const ValidationError &err)
  {
    return EmitError(
        {"ManifestError", kExitManifest, err.what(), "metadata"}, true);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError(
        {"WorkspaceError", kExitWorkspace, err.what(), "metadata"}, true);
  }
  catch (const ResolutionError &err)
  {
    return EmitError(
        {"ResolutionError", kExitResolve, err.what(), "metadata"}, true);
  }
  catch (const FetchError &err)
  {
    return EmitError(
        {"FetchError", kExitFetch, err.what(), "metadata"}, true);
  }
  catch (const CacheError &err)
  {
    return EmitError(
        {"CacheError", kExitCache, err.what(), "metadata"}, true);
  }
}

int HandlePlanCommand(
    const std::string_view command_name,
    const std::string_view intent,
    const bool allow_lib,
    const bool allow_bin,
    const bool allow_test,
    const std::vector<std::string> &args,
    const bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage(command_name);
  }

  ParsedPlanInvocation parsed;
  if (const std::optional<CommandError> parse_error =
          ParsePlanInvocation(
              command_name, intent, allow_lib, allow_bin, allow_test, args,
              parsed);
      parse_error.has_value())
  {
    return EmitError(*parse_error, as_json);
  }

  BuildPlanRequest &request = parsed.request;
  request.offline = parsed.workflow_flags.offline;
  const ResolveOptions resolve_options =
      BuildResolveOptions(request.manifest_path, parsed.workflow_flags);
  request.vendor_root = resolve_options.vendor_root;

  SyncProjectResult sync_result;
  try
  {
    sync_result =
        SyncProjectDependencies(request.manifest_path, resolve_options);
  }
  catch (const ValidationError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"ManifestError", kExitManifest, err.what(), std::string(command_name)},
        as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"WorkspaceError", kExitWorkspace, err.what(),
         std::string(command_name)},
        as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"ResolutionError", kExitResolve, err.what(),
         std::string(command_name)},
        as_json);
  }
  catch (const FetchError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"FetchError", kExitFetch, err.what(), std::string(command_name)},
        as_json);
  }
  catch (const CacheError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"CacheError", kExitCache, err.what(), std::string(command_name)},
        as_json);
  }
  catch (const SyncError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"LockfileError", kExitLock, err.what(), std::string(command_name)},
        as_json);
  }

  std::optional<fs::path> compiler;
  std::optional<CompatibilityReport> compatibility;
  if (!parsed.dry_run)
  {
    compiler = ResolveStyioBinary(parsed.styio_bin);
    if (!compiler.has_value())
    {
      return EmitWorkflowFailure(
          command_name,
          {"CompilerSpawnError", kExitCompilerSpawn,
           "Styio was not found; use --styio-bin, PAFIO_STYIO_BIN, or install "
           "styio on PATH",
           std::string(command_name)},
          as_json);
    }

    try
    {
      compatibility = CheckCompilerCompatibility(*compiler);
      if (std::find(
              compatibility->supported_compile_plan_versions.begin(),
              compatibility->supported_compile_plan_versions.end(),
              1) ==
          compatibility->supported_compile_plan_versions.end())
      {
        return EmitWorkflowFailure(
            command_name,
            {"ContractError", kExitContract,
             "the discovered Styio does not support compile-plan v1",
             std::string(command_name)},
            as_json);
      }
      request.compiler_version = compatibility->compiler_version;
      request.compiler_channel = compatibility->compiler_channel;
    }
    catch (const CompilerProbeError &err)
    {
      return EmitWorkflowFailure(
          command_name,
          {"CompilerSpawnError", kExitCompilerSpawn, err.what(),
           std::string(command_name)},
          as_json);
    }
    catch (const CompatibilityError &err)
    {
      return EmitWorkflowFailure(
          command_name,
          {"ContractError", kExitContract, err.what(),
           std::string(command_name)},
          as_json);
    }
  }

  BuildPlanResult plan;
  try
  {
    plan = WriteBuildCompilePlan(request, sync_result.graph);
  }
  catch (const ValidationError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"ManifestError", kExitManifest, err.what(), std::string(command_name)},
        as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"WorkspaceError", kExitWorkspace, err.what(),
         std::string(command_name)},
        as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"ResolutionError", kExitResolve, err.what(),
         std::string(command_name)},
        as_json);
  }
  catch (const FetchError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"FetchError", kExitFetch, err.what(), std::string(command_name)},
        as_json);
  }
  catch (const CacheError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"CacheError", kExitCache, err.what(), std::string(command_name)},
        as_json);
  }
  catch (const PlanError &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"PlanError", kExitPlan, err.what(), std::string(command_name)},
        as_json);
  }

  const json sync_payload =
      SyncPayload(sync_result, parsed.workflow_flags);
  if (parsed.dry_run)
  {
    return EmitSuccess(
        {
            {"action", std::string(command_name)},
            {"command", std::string(command_name)},
            {"intent", request.intent},
            {"message", "wrote compile-plan without executing Styio: " +
                            plan.plan_path.string()},
            {"mode", "dry-run"},
            {"plan", PlanPayload(plan)},
            {"profile", plan.profile_name},
            {"status", "planned"},
            {"styio",
             {
                 {"process",
                  {
                      {"status", "not_started"},
                  }},
                 {"status", "not_run"},
             }},
            {"sync", sync_payload},
            {"target", TargetPayload(plan)},
        },
        as_json);
  }

  ProcessResult process;
  try
  {
    process = RunProcess({
        .program = ProcessPathString(*compiler),
        .args = {"--compile-plan", ProcessPathString(plan.plan_path)},
        .search_path = false,
        .timeout = std::nullopt,
        .error_context = "Styio compile-plan execution",
    });
  }
  catch (const std::exception &err)
  {
    return EmitWorkflowFailure(
        command_name,
        {"CompilerSpawnError", kExitCompilerSpawn, err.what(),
         std::string(command_name)},
        as_json);
  }

  if (process.exit_code == 127)
  {
    return EmitWorkflowFailure(
        command_name,
        {"CompilerSpawnError", kExitCompilerSpawn,
         "Styio failed to execute --compile-plan", std::string(command_name)},
        as_json);
  }
  if (process.exit_code != 0)
  {
    const std::string detail = DescribeProcessFailure(process);
    return EmitWorkflowFailure(
        command_name,
        {"CompilerError", kExitCompiler,
         "Styio failed for compile-plan " + plan.plan_path.string() +
             (detail.empty() ? "" : ": " + detail),
         std::string(command_name)},
        as_json);
  }

  if (!as_json)
  {
    if (!process.stdout_text.empty())
    {
      std::cout << process.stdout_text;
    }
    if (!process.stderr_text.empty())
    {
      std::cerr << process.stderr_text;
    }
  }
  if (const std::optional<std::string> error =
          ValidateCompilePlanMaterialization(plan);
      error.has_value())
  {
    return EmitWorkflowFailure(
        command_name,
        {"CompilerError", kExitCompiler,
         "Styio completed but did not materialize compile-plan outputs: " +
             *error,
         std::string(command_name)},
        as_json);
  }

  const json styio_payload = {
      {"binary", compatibility->binary.string()},
      {"capabilities", compatibility->capabilities},
      {"compiler_channel", compatibility->compiler_channel},
      {"compiler_edition_max", compatibility->compiler_edition_max},
      {"compiler_version", compatibility->compiler_version},
      {"integration_phase", compatibility->integration_phase},
      {"process",
       {
           {"exit_code", process.exit_code},
           {"status", "exited"},
       }},
      {"status", "succeeded"},
      {"supported_compile_plan_versions",
       compatibility->supported_compile_plan_versions},
  };
  return EmitSuccess(
      {
          {"action", std::string(command_name)},
          {"command", std::string(command_name)},
          {"intent", request.intent},
          {"message", "completed Styio " + std::string(command_name) +
                          " via compile-plan"},
          {"mode", "execute"},
          {"plan", PlanPayload(plan)},
          {"profile", plan.profile_name},
          {"status", "succeeded"},
          {"styio", styio_payload},
          {"sync", sync_payload},
          {"target", TargetPayload(plan)},
      },
      as_json);
}

int HandleCheck(const std::vector<std::string> &args, const bool as_json)
{
  return HandlePlanCommand(
      "check", "check", true, true, true, args, as_json);
}

int HandleBuild(const std::vector<std::string> &args, const bool as_json)
{
  return HandlePlanCommand(
      "build", "build", true, true, false, args, as_json);
}

int HandleRun(const std::vector<std::string> &args, const bool as_json)
{
  return HandlePlanCommand(
      "run", "run", false, true, false, args, as_json);
}

int HandleTest(const std::vector<std::string> &args, const bool as_json)
{
  return HandlePlanCommand(
      "test", "test", false, false, true, args, as_json);
}

}  // namespace pafio
