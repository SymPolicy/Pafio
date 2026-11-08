#include "PafioCLI/CLI.hpp"

#include "PafioApp/DoctorApp.hpp"
#include "PafioApp/PackageApp.hpp"
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

enum class TopLevelCommand
{
  MachineInfo,
  Doctor,
  Metadata,
  New,
  Init,
  Check,
  Add,
  Remove,
  Sync,
  Build,
  Run,
  Test,
  Tree,
  Vendor,
  Pack,
  Publish,
  Registry,
};

std::optional<TopLevelCommand> ParseTopLevelCommand(std::string_view raw)
{
  if (raw == "machine-info")
  {
    return TopLevelCommand::MachineInfo;
  }
  if (raw == "doctor")
  {
    return TopLevelCommand::Doctor;
  }
  if (raw == "metadata")
  {
    return TopLevelCommand::Metadata;
  }
  if (raw == "new")
  {
    return TopLevelCommand::New;
  }
  if (raw == "init")
  {
    return TopLevelCommand::Init;
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
  return std::nullopt;
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
    if (argv[index] == "-h" || argv[index] == "--help")
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
    case TopLevelCommand::Doctor:
      return HandleDoctor(args, global_json);
    case TopLevelCommand::Metadata:
      return HandleMetadata(args, global_json);
    case TopLevelCommand::New:
      return HandleNew(args, global_json);
    case TopLevelCommand::Init:
      return HandleInit(args, global_json);
    case TopLevelCommand::Check:
      return HandleCheck(args, global_json);
    case TopLevelCommand::Add:
      return HandleAdd(args, global_json);
    case TopLevelCommand::Remove:
      return HandleRemove(args, global_json);
    case TopLevelCommand::Sync:
      return HandleSync(args, global_json);
    case TopLevelCommand::Build:
      return HandleBuild(args, global_json);
    case TopLevelCommand::Run:
      return HandleRun(args, global_json);
    case TopLevelCommand::Test:
      return HandleTest(args, global_json);
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
  }
  return EmitError({"UsageError", kExitUsage, "unknown command: " + command, command}, global_json);
}

}  // namespace pafio
