#include "PafioApp/CloudApp.hpp"

#include "PafioApp/WorkflowApp.hpp"

#include "PafioCLI/Support.hpp"
#include "PafioCloud/Contract.hpp"
#include "PafioCloud/Execution.hpp"
#include "PafioCloud/Job.hpp"
#include "PafioToolchain/State.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace pafio
{

namespace
{

enum class CloudSubcommand
{
  Status,
  Plan,
};

enum class CloudPlanAction
{
  Build,
  Run,
  Test,
};

std::optional<CloudSubcommand> ParseCloudSubcommand(std::string_view raw)
{
  const std::string normalized = NormalizeSetKeyword(std::string(raw));
  if (normalized == "status")
  {
    return CloudSubcommand::Status;
  }
  if (normalized == "plan")
  {
    return CloudSubcommand::Plan;
  }
  return std::nullopt;
}

std::optional<CloudPlanAction> ParseCloudPlanAction(std::string_view raw)
{
  const std::string normalized = NormalizeSetKeyword(std::string(raw));
  if (normalized == "build")
  {
    return CloudPlanAction::Build;
  }
  if (normalized == "run")
  {
    return CloudPlanAction::Run;
  }
  if (normalized == "test")
  {
    return CloudPlanAction::Test;
  }
  return std::nullopt;
}

std::string_view CloudPlanActionName(CloudPlanAction action)
{
  switch (action)
  {
    case CloudPlanAction::Build:
      return "build";
    case CloudPlanAction::Run:
      return "run";
    case CloudPlanAction::Test:
      return "test";
  }
  return "build";
}

bool CloudPlanAllowsLibTarget(CloudPlanAction action)
{
  return action == CloudPlanAction::Build;
}

bool CloudPlanAllowsBinTarget(CloudPlanAction action)
{
  return action != CloudPlanAction::Test;
}

bool CloudPlanAllowsTestTarget(CloudPlanAction action)
{
  return action == CloudPlanAction::Test;
}

}  // namespace

int HandleCloudStatus(const std::vector<std::string> &args, bool as_json)
{
  bool local_json = as_json;
  fs::path manifest_path = "pafio.toml";
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
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "cloud status"}, as_json);
      }
      manifest_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for cloud status: " + args[index], "cloud status"}, as_json);
    }
  }
  if (!local_json)
  {
    return EmitError({"UsageError", kExitUsage, "cloud status currently requires --json", "cloud status"}, as_json);
  }

  try
  {
    const ProjectToolchainState state = LoadProjectToolchainState(manifest_path);
    const CloudExecutionPolicy cloud_policy = ResolveCloudExecutionPolicy(state);
    return EmitSuccess(BuildCloudStatusPayload(state, cloud_policy), true);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "cloud status"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "cloud status"}, as_json);
  }
}

int HandleCloudPlan(const std::vector<std::string> &args, bool as_json)
{
  bool local_json = as_json;
  if (!args.empty() && args.front() == "--json")
  {
    local_json = true;
  }
  if (!local_json)
  {
    return EmitError({"UsageError", kExitUsage, "cloud plan currently requires --json", "cloud plan"}, as_json);
  }

  size_t index = 0;
  if (index < args.size() && args[index] == "--json")
  {
    ++index;
  }
  if (index >= args.size())
  {
    return EmitError({"UsageError", kExitUsage, "cloud plan requires <build|run|test>", "cloud plan"}, as_json);
  }

  const auto action = ParseCloudPlanAction(args[index++]);
  if (!action.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "cloud plan requires one of: build, run, test", "cloud plan"}, as_json);
  }

  const std::vector<std::string> invocation_args(args.begin() + static_cast<std::ptrdiff_t>(index), args.end());
  ParsedPlanInvocation parsed;
  const std::string action_name(CloudPlanActionName(*action));
  if (const std::optional<CommandError> parse_error =
          ParsePlanInvocation(
              action_name,
              action_name,
              CloudPlanAllowsLibTarget(*action),
              CloudPlanAllowsBinTarget(*action),
              CloudPlanAllowsTestTarget(*action),
              invocation_args,
              parsed);
      parse_error.has_value())
  {
    return EmitError(*parse_error, as_json);
  }

  try
  {
    const ProjectToolchainState toolchain_state = LoadProjectToolchainState(parsed.request.manifest_path);
    if (parsed.request.build_mode.empty())
    {
      parsed.request.build_mode = toolchain_state.build_mode;
    }
    const CloudExecutionPolicy cloud_policy = ResolveCloudExecutionPolicy(toolchain_state);
    const WorkflowInvocationOptions invocation_options = {
        .locked = parsed.workflow_flags.locked,
        .offline = parsed.workflow_flags.offline,
        .styio_bin = parsed.styio_bin,
        .allow_fetch = parsed.source_flags.allow_fetch,
        .assume_yes = parsed.source_flags.assume_yes,
        .non_interactive = parsed.source_flags.non_interactive,
        .source_root = parsed.source_flags.source_root,
        .source_revision = parsed.source_flags.source_revision,
    };
    const CloudBuildJobRequest request =
        BuildCloudBuildJobRequest(action_name, parsed.request, toolchain_state, invocation_options, cloud_policy);
    return EmitSuccess(
        {
            {"command", "cloud plan"},
            {"message", "rendered a control-plane build job request for " + action_name},
            {"job_request", BuildCloudBuildJobRequestPayload(request)},
        },
        true);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "cloud plan"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "cloud plan"}, as_json);
  }
}

int HandleCloud(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("cloud");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "cloud requires a subcommand", "cloud"}, as_json);
  }

  const std::string subcommand = NormalizeSetKeyword(args.front());
  std::vector<std::string> tail(args.begin() + 1, args.end());
  const auto parsed_subcommand = ParseCloudSubcommand(subcommand);
  if (!parsed_subcommand.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "cloud supports only the 'status' and 'plan' subcommands", "cloud"}, as_json);
  }

  switch (*parsed_subcommand)
  {
    case CloudSubcommand::Status:
    {
      if (tail.size() == 1 && tail.front() == "--help")
      {
        return PrintCommandUsage("cloud");
      }
      return HandleCloudStatus(tail, as_json);
    }
    case CloudSubcommand::Plan:
    {
      if (tail.size() == 1 && tail.front() == "--help")
      {
        return PrintCommandUsage("cloud");
      }
      return HandleCloudPlan(tail, as_json);
    }
  }
  return EmitError({"UsageError", kExitUsage, "cloud supports only the 'status' and 'plan' subcommands", "cloud"}, as_json);
}

}  // namespace pafio
