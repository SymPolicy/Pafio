#include "PafioCLI/CLI.hpp"

#include "PafioApp/CloudApp.hpp"
#include "PafioApp/PackageApp.hpp"
#include "PafioApp/ToolApp.hpp"
#include "PafioApp/WorkflowApp.hpp"
#include "PafioCLI/MachineInfoContract.hpp"
#include "PafioCLI/Support.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/Version.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

enum class ToolSubcommand
{
  Install,
  Status,
  Use,
  Pin,
};

enum class TopLevelCommand
{
  MachineInfo,
  ProjectGraph,
  Cloud,
  New,
  Init,
  Install,
  Use,
  Set,
  Check,
  Add,
  Remove,
  Sync,
  Fetch,
  Build,
  Run,
  Test,
  Lock,
  Tree,
  Vendor,
  Pack,
  Publish,
  Registry,
  Tool,
};

std::optional<ToolSubcommand> ParseToolSubcommand(std::string_view raw)
{
  if (raw == "install")
  {
    return ToolSubcommand::Install;
  }
  if (raw == "status")
  {
    return ToolSubcommand::Status;
  }
  if (raw == "use")
  {
    return ToolSubcommand::Use;
  }
  if (raw == "pin")
  {
    return ToolSubcommand::Pin;
  }
  return std::nullopt;
}

std::optional<TopLevelCommand> ParseTopLevelCommand(std::string_view raw)
{
  if (raw == "machine-info")
  {
    return TopLevelCommand::MachineInfo;
  }
  if (raw == "project-graph")
  {
    return TopLevelCommand::ProjectGraph;
  }
  if (raw == "cloud")
  {
    return TopLevelCommand::Cloud;
  }
  if (raw == "new")
  {
    return TopLevelCommand::New;
  }
  if (raw == "init")
  {
    return TopLevelCommand::Init;
  }
  if (raw == "install")
  {
    return TopLevelCommand::Install;
  }
  if (raw == "use")
  {
    return TopLevelCommand::Use;
  }
  if (raw == "set")
  {
    return TopLevelCommand::Set;
  }
  if (raw == "check")
  {
    return TopLevelCommand::Check;
  }
  if (raw == "add")
  {
    return TopLevelCommand::Add;
  }
  if (raw == "remove")
  {
    return TopLevelCommand::Remove;
  }
  if (raw == "sync")
  {
    return TopLevelCommand::Sync;
  }
  if (raw == "fetch")
  {
    return TopLevelCommand::Fetch;
  }
  if (raw == "build")
  {
    return TopLevelCommand::Build;
  }
  if (raw == "run")
  {
    return TopLevelCommand::Run;
  }
  if (raw == "test")
  {
    return TopLevelCommand::Test;
  }
  if (raw == "lock")
  {
    return TopLevelCommand::Lock;
  }
  if (raw == "tree")
  {
    return TopLevelCommand::Tree;
  }
  if (raw == "vendor")
  {
    return TopLevelCommand::Vendor;
  }
  if (raw == "pack")
  {
    return TopLevelCommand::Pack;
  }
  if (raw == "publish")
  {
    return TopLevelCommand::Publish;
  }
  if (raw == "registry")
  {
    return TopLevelCommand::Registry;
  }
  if (raw == "tool")
  {
    return TopLevelCommand::Tool;
  }
  return std::nullopt;
}

int HandleToolCommand(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return pafio::PrintCommandUsage("tool");
  }
  if (args.empty())
  {
    return pafio::EmitError(
        {"UsageError", pafio::kExitUsage, "tool requires the 'install', 'status', 'use', or 'pin' subcommand", "tool"},
        as_json);
  }

  const std::string subcommand = args.front();
  const std::vector<std::string> tail(args.begin() + 1, args.end());
  const auto parsed_subcommand = ParseToolSubcommand(subcommand);
  if (!parsed_subcommand.has_value())
  {
    return pafio::EmitError(
        {"UsageError", pafio::kExitUsage, "tool requires the 'install', 'status', 'use', or 'pin' subcommand", "tool"},
        as_json);
  }

  switch (*parsed_subcommand)
  {
    case ToolSubcommand::Install:
      return pafio::HandleToolInstall(tail, as_json);
    case ToolSubcommand::Status:
    {
      if (tail.size() == 1 && tail.front() == "--help")
      {
        return pafio::PrintCommandUsage("tool");
      }
      return pafio::HandleToolStatus(tail, as_json);
    }
    case ToolSubcommand::Use:
      return pafio::HandleToolUse(tail, as_json);
    case ToolSubcommand::Pin:
      return pafio::HandleToolPin(tail, as_json);
  }
  return pafio::EmitError(
      {"UsageError", pafio::kExitUsage, "tool requires the 'install', 'status', 'use', or 'pin' subcommand", "tool"},
      as_json);
}

}  // namespace

namespace pafio
{

int RunCli(const std::vector<std::string> &argv)
{
  bool global_json = false;
  size_t index = 0;
  while (index < argv.size())
  {
    if (argv[index] == "--help")
    {
      if (index + 1 != argv.size())
      {
        return EmitError({"UsageError", kExitUsage, "--help does not accept extra arguments before a command", "global"}, global_json);
      }
      return PrintGlobalHelp();
    }
    if (argv[index] == "--json")
    {
      global_json = true;
      ++index;
      continue;
    }
    if (argv[index] == "--version")
    {
      if (index + 1 != argv.size())
      {
        return EmitError({"UsageError", kExitUsage, "--version does not accept extra arguments", "global"}, global_json);
      }
      std::cout << "pafio " << kVersion << '\n';
      return kExitSuccess;
    }
    break;
  }

  if (index >= argv.size())
  {
    return PrintGlobalHelp();
  }

  const std::string command = argv[index++];
  const std::vector<std::string> args(argv.begin() + static_cast<std::ptrdiff_t>(index), argv.end());
  const auto parsed_command = ParseTopLevelCommand(command);
  if (!parsed_command.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "unknown command: " + command, command}, global_json);
  }

  switch (*parsed_command)
  {
    case TopLevelCommand::MachineInfo:
      if (args.size() == 1 && args.front() == "--help")
      {
        return PrintCommandUsage("machine-info");
      }
      if (!args.empty() && !(args.size() == 1 && args.front() == "--json"))
      {
        return EmitError({"UsageError", kExitUsage, "machine-info accepts only --json", "machine-info"}, global_json);
      }
      std::cout << BuildMachineInfoPayload().dump() << '\n';
      return kExitSuccess;
    case TopLevelCommand::ProjectGraph:
      return HandleProjectGraph(args, global_json);
    case TopLevelCommand::Cloud:
      return HandleCloud(args, global_json);
    case TopLevelCommand::New:
      return HandleNew(args, global_json);
    case TopLevelCommand::Init:
      return HandleInit(args, global_json);
    case TopLevelCommand::Install:
      return HandleInstall(args, global_json);
    case TopLevelCommand::Use:
      return HandleUse(args, global_json);
    case TopLevelCommand::Set:
      return HandleSet(args, global_json);
    case TopLevelCommand::Check:
      return HandleCheck(args, global_json);
    case TopLevelCommand::Add:
      return HandleAdd(args, global_json);
    case TopLevelCommand::Remove:
      return HandleRemove(args, global_json);
    case TopLevelCommand::Sync:
      return HandleSync(args, global_json);
    case TopLevelCommand::Fetch:
      return HandleFetch(args, global_json);
    case TopLevelCommand::Build:
      return HandleBuild(args, global_json);
    case TopLevelCommand::Run:
      return HandleRun(args, global_json);
    case TopLevelCommand::Test:
      return HandleTest(args, global_json);
    case TopLevelCommand::Lock:
      return HandleLock(args, global_json);
    case TopLevelCommand::Tree:
      return HandleTree(args, global_json);
    case TopLevelCommand::Vendor:
      return HandleVendor(args, global_json);
    case TopLevelCommand::Pack:
      return HandlePack(args, global_json);
    case TopLevelCommand::Publish:
      return HandlePublish(args, global_json);
    case TopLevelCommand::Registry:
      return HandleRegistry(args, global_json);
    case TopLevelCommand::Tool:
      return HandleToolCommand(args, global_json);
  }
  return EmitError({"UsageError", kExitUsage, "unknown command: " + command, command}, global_json);
}

}  // namespace pafio
