#include "SpioCLI/CLI.hpp"

#include "SpioCompat/Compat.hpp"
#include "SpioCore/Errors.hpp"
#include "SpioCore/Version.hpp"
#include "SpioManifest/Lockfile.hpp"
#include "SpioManifest/Manifest.hpp"
#include "SpioPack/Pack.hpp"
#include "SpioPlan/CompilePlan.hpp"
#include "SpioResolve/Resolver.hpp"
#include "SpioTree/Render.hpp"
#include "SpioWorkflow/Dependencies.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

struct ChildProcessResult
{
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

json BuildMachineInfoPayload()
{
  return {
      {"tool", "spio"},
      {"version", spio::kVersion},
      {"bootstrap", true},
      {"implementation_language", "c++"},
      {"supported_manifests", json::array({1})},
      {"supported_lockfiles", json::array({1})},
      {"supported_contracts", {
                                 {"compile_plan", json::array()},
                             }},
      {"notes", json::array({
                    "native c++ phase-3 minimal resolver core",
                    "compile-plan schema is owned but not yet active",
                })},
  };
}

int EmitError(const spio::CommandError &error, bool as_json)
{
  if (as_json)
  {
    std::cerr << json({
                     {"category", error.category},
                     {"code", error.code},
                     {"message", error.message},
                     {"command", error.command},
                 })
                     .dump()
              << '\n';
  }
  else
  {
    std::cerr << "[" << error.category << ":" << error.code << "] " << error.command << ": " << error.message << '\n';
  }
  return error.code;
}

int EmitSuccess(const json &payload, bool as_json)
{
  if (as_json)
  {
    std::cout << payload.dump() << '\n';
  }
  else if (payload.contains("message"))
  {
    std::cout << payload["message"].get<std::string>() << '\n';
  }
  return spio::kExitSuccess;
}

int EmitBootstrapNotImplemented(std::string_view command, bool as_json)
{
  return EmitError(
      spio::CommandError{
          .category = "BootstrapNotImplemented",
          .code = spio::kExitNotImplemented,
          .message = "command is recognized but not implemented in the native bootstrap scaffold",
          .command = std::string(command),
      },
      as_json);
}

int PrintGlobalHelp()
{
  std::cout
      << "spio usage:\n"
      << "  spio [--help] [--version] [--json] <command> [command-args...]\n\n"
      << "commands:\n"
      << "  machine-info [--json]\n"
      << "  new <package-name> [directory] [--lib|--bin]\n"
      << "  init [--name <package-name>] [--lib|--bin]\n"
      << "  check [--manifest-path <path>] [--styio-bin <path>]\n"
      << "  add <package-name> (--path <path> | --git <source> --rev <rev>) [--alias <name>] [--dev] [--manifest-path <path>]\n"
      << "  remove <alias-or-package> [--dev] [--manifest-path <path>]\n"
      << "  fetch [--manifest-path <path>]\n"
      << "  lock [--manifest-path <path>] [--check]\n"
      << "  tree [--manifest-path <path>]\n"
      << "  build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]\n"
      << "  run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]\n"
      << "  test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]\n"
      << "  pack [--manifest-path <path>] [--package <package-name>] [--output <path>]\n"
      << "  publish\n"
      << "  tool install\n";
  return spio::kExitSuccess;
}

int PrintCommandUsage(std::string_view command)
{
  if (command == "machine-info")
  {
    std::cout << "usage: spio machine-info [--json]\n";
  }
  else if (command == "new")
  {
    std::cout << "usage: spio new <package-name> [directory] [--lib|--bin]\n";
  }
  else if (command == "init")
  {
    std::cout << "usage: spio init [--name <package-name>] [--lib|--bin]\n";
  }
  else if (command == "check")
  {
    std::cout << "usage: spio check [--manifest-path <path>] [--styio-bin <path>]\n";
  }
  else if (command == "add")
  {
    std::cout << "usage: spio add <package-name> (--path <path> | --git <source> --rev <rev>) [--alias <name>] [--dev] [--manifest-path <path>]\n";
  }
  else if (command == "remove")
  {
    std::cout << "usage: spio remove <alias-or-package> [--dev] [--manifest-path <path>]\n";
  }
  else if (command == "fetch")
  {
    std::cout << "usage: spio fetch [--manifest-path <path>]\n";
  }
  else if (command == "lock")
  {
    std::cout << "usage: spio lock [--manifest-path <path>] [--check]\n";
  }
  else if (command == "tree")
  {
    std::cout << "usage: spio tree [--manifest-path <path>]\n";
  }
  else if (command == "build")
  {
    std::cout << "usage: spio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]\n";
  }
  else if (command == "run")
  {
    std::cout << "usage: spio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]\n";
  }
  else if (command == "test")
  {
    std::cout << "usage: spio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]\n";
  }
  else if (command == "pack")
  {
    std::cout << "usage: spio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]\n";
  }
  else if (command == "tool")
  {
    std::cout << "usage: spio tool install\n";
  }
  else
  {
    std::cout << "usage: spio " << command << '\n';
  }
  return spio::kExitSuccess;
}

std::string KindFromFlags(const std::vector<std::string> &args, size_t &index)
{
  std::string kind = "bin";
  for (; index < args.size(); ++index)
  {
    if (args[index] == "--lib")
    {
      kind = "lib";
    }
    else if (args[index] == "--bin")
    {
      kind = "bin";
    }
    else
    {
      break;
    }
  }
  return kind;
}

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

ChildProcessResult RunChildProcess(const fs::path &binary, const std::vector<std::string> &args)
{
  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    throw std::runtime_error("failed to create pipes for child process execution");
  }

  const pid_t child = fork();
  if (child < 0)
  {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    throw std::runtime_error("failed to fork child process");
  }

  if (child == 0)
  {
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 2U);
    argv.push_back(const_cast<char *>(binary.c_str()));
    for (const std::string &arg : args)
    {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execv(binary.c_str(), argv.data());
    _exit(127);
  }

  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  auto read_all = [](int fd) {
    std::string text;
    std::array<char, 4096> buffer{};
    ssize_t read_size = 0;
    while ((read_size = read(fd, buffer.data(), buffer.size())) > 0)
    {
      text.append(buffer.data(), static_cast<size_t>(read_size));
    }
    close(fd);
    return text;
  };

  ChildProcessResult result;
  result.stdout_text = read_all(stdout_pipe[0]);
  result.stderr_text = read_all(stderr_pipe[0]);

  int status = 0;
  waitpid(child, &status, 0);
  if (WIFEXITED(status))
  {
    result.exit_code = WEXITSTATUS(status);
  }
  else
  {
    result.exit_code = 1;
  }
  return result;
}

std::string TrimTrailingNewline(std::string text)
{
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
  {
    text.pop_back();
  }
  return text;
}

int HandleNew(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("new");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", spio::kExitUsage, "new requires a package name", "new"}, as_json);
  }

  size_t index = 0;
  const std::string name = args[index++];
  fs::path directory;
  if (index < args.size() && !args[index].starts_with("--"))
  {
    directory = fs::path(args[index++]);
  }
  else
  {
    const auto slash = name.find('/');
    directory = slash == std::string::npos ? fs::path(name) : fs::path(name.substr(slash + 1));
  }

  const std::string kind = KindFromFlags(args, index);
  if (index != args.size())
  {
    return EmitError({"UsageError", spio::kExitUsage, "unexpected arguments for new", "new"}, as_json);
  }

  try
  {
    spio::InitializeProject({.package_name = name, .root = directory, .kind = kind});
  }
  catch (const std::exception &err)
  {
    return EmitError({"UsageError", spio::kExitUsage, err.what(), "new"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "new"},
          {"message", "initialized project at " + directory.string()},
          {"root", fs::absolute(directory).string()},
      },
      as_json);
}

int HandleInit(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("init");
  }
  std::optional<std::string> name;
  size_t index = 0;
  while (index < args.size())
  {
    if (args[index] == "--name")
    {
      ++index;
      if (index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--name requires a value", "init"}, as_json);
      }
      name = args[index++];
      continue;
    }
    break;
  }

  const std::string kind = KindFromFlags(args, index);
  if (index != args.size())
  {
    return EmitError({"UsageError", spio::kExitUsage, "unexpected arguments for init", "init"}, as_json);
  }

  const fs::path root = fs::current_path();
  const std::string package_name = name.value_or(spio::InferLocalPackageName(root));
  try
  {
    spio::InitializeProject({.package_name = package_name, .root = root, .kind = kind});
  }
  catch (const std::exception &err)
  {
    return EmitError({"UsageError", spio::kExitUsage, err.what(), "init"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "init"},
          {"message", "initialized project in " + root.string()},
          {"root", fs::absolute(root).string()},
          {"package", package_name},
      },
      as_json);
}

int HandleCheck(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("check");
  }
  fs::path manifest_path = "spio.toml";
  std::optional<std::string> styio_bin;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "check"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--styio-bin requires a value", "check"}, as_json);
      }
      styio_bin = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for check: " + args[index], "check"}, as_json);
    }
  }

  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", spio::kExitManifest, "manifest not found: " + manifest_path.string(), "check"}, as_json);
  }

  try
  {
    spio::LoadManifest(manifest_path);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "check"}, as_json);
  }

  const fs::path lockfile_path = manifest_path.parent_path() / "spio.lock";
  spio::LockGenerationResult generated;
  try
  {
    generated = spio::ResolveSingleVersionLockfile(manifest_path);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "check"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "check"}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), "check"}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), "check"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "check"}, as_json);
  }

  if (fs::exists(lockfile_path))
  {
    try
    {
      spio::LoadLockfile(lockfile_path);
      if (ReadFile(lockfile_path) != spio::SerializeLockfileCanonical(generated.lockfile))
      {
        return EmitError({"LockfileError", spio::kExitLock, "lockfile is stale: " + lockfile_path.string(), "check"}, as_json);
      }
    }
    catch (const spio::ValidationError &err)
    {
      return EmitError({"LockfileError", spio::kExitLock, err.what(), "check"}, as_json);
    }
    catch (const std::exception &err)
    {
      return EmitError({"LockfileError", spio::kExitLock, err.what(), "check"}, as_json);
    }
  }

  json compatibility_payload = nullptr;
  const std::optional<fs::path> compiler = spio::ResolveStyioBinary(styio_bin);
  if (compiler.has_value())
  {
    try
    {
      const spio::CompatibilityReport report = spio::CheckCompilerCompatibility(*compiler);
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
    catch (const spio::CompilerProbeError &err)
    {
      return EmitError({"CompilerSpawnError", spio::kExitCompilerSpawn, err.what(), "check"}, as_json);
    }
    catch (const spio::CompatibilityError &err)
    {
      return EmitError({"ContractError", spio::kExitContract, err.what(), "check"}, as_json);
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
          {"styio", compatibility_payload},
      },
      as_json);
}

int HandlePlanCommand(std::string_view command_name, std::string_view intent, bool allow_lib, bool allow_bin, bool allow_test, const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage(command_name);
  }

  spio::BuildPlanRequest request;
  request.intent = std::string(intent);
  std::optional<std::string> styio_bin;
  bool dry_run = false;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", std::string(command_name)}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--package")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--package requires a value", std::string(command_name)}, as_json);
      }
      request.package_name = args[index];
    }
    else if (args[index] == "--bin")
    {
      if (!allow_bin)
      {
        return EmitError({"UsageError", spio::kExitUsage, std::string(command_name) + " does not accept --bin", std::string(command_name)}, as_json);
      }
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--bin requires a value", std::string(command_name)}, as_json);
      }
      request.bin_name = args[index];
    }
    else if (args[index] == "--test")
    {
      if (!allow_test)
      {
        return EmitError({"UsageError", spio::kExitUsage, std::string(command_name) + " does not accept --test", std::string(command_name)}, as_json);
      }
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--test requires a value", std::string(command_name)}, as_json);
      }
      request.test_name = args[index];
    }
    else if (args[index] == "--lib")
    {
      if (!allow_lib)
      {
        return EmitError({"UsageError", spio::kExitUsage, std::string(command_name) + " does not accept --lib", std::string(command_name)}, as_json);
      }
      request.select_lib = true;
    }
    else if (args[index] == "--profile")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--profile requires a value", std::string(command_name)}, as_json);
      }
      request.profile = args[index];
    }
    else if (args[index] == "--dry-run")
    {
      dry_run = true;
    }
    else if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--styio-bin requires a value", std::string(command_name)}, as_json);
      }
      styio_bin = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for " + std::string(command_name) + ": " + args[index], std::string(command_name)}, as_json);
    }
  }

  std::optional<fs::path> compiler;
  json compatibility_payload = nullptr;
  if (!dry_run)
  {
    compiler = spio::ResolveStyioBinary(styio_bin);
    if (!compiler.has_value())
    {
      return EmitError(
          {"UsageError", spio::kExitUsage, std::string(command_name) + " requires --styio-bin <path> or SPIO_STYIO_BIN unless --dry-run is set", std::string(command_name)},
          as_json);
    }

    try
    {
      const spio::CompatibilityReport report = spio::CheckCompilerCompatibility(*compiler);
      compatibility_payload = {
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
            {"ContractError", spio::kExitContract, "published compatibility matrix does not yet allow compile-plan v1 " + std::string(command_name) + " in this spio phase", std::string(command_name)},
            as_json);
      }
      request.compiler_version = report.compiler_version;
    }
    catch (const spio::CompilerProbeError &err)
    {
      return EmitError({"CompilerSpawnError", spio::kExitCompilerSpawn, err.what(), std::string(command_name)}, as_json);
    }
    catch (const spio::CompatibilityError &err)
    {
      return EmitError({"ContractError", spio::kExitContract, err.what(), std::string(command_name)}, as_json);
    }
  }

  spio::BuildPlanResult plan;
  try
  {
    plan = spio::WriteBuildCompilePlan(request);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), std::string(command_name)}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), std::string(command_name)}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), std::string(command_name)}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), std::string(command_name)}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), std::string(command_name)}, as_json);
  }
  catch (const spio::PlanError &err)
  {
    return EmitError({"PlanError", spio::kExitPlan, err.what(), std::string(command_name)}, as_json);
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
            {"intent", request.intent},
        },
        as_json);
  }

  try
  {
    const ChildProcessResult result = RunChildProcess(*compiler, {"--compile-plan", plan.plan_path.string()});
    if (result.exit_code == 127)
    {
      return EmitError(
          {"CompilerSpawnError", spio::kExitCompilerSpawn, "compiler failed to execute --compile-plan", std::string(command_name)},
          as_json);
    }
    if (result.exit_code != 0)
    {
      const std::string detail = TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text);
      return EmitError(
          {"CompilerError", spio::kExitCompiler, "compiler failed for compile-plan " + plan.plan_path.string() + (detail.empty() ? "" : ": " + detail), std::string(command_name)},
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
  }
  catch (const std::exception &err)
  {
    return EmitError({"CompilerSpawnError", spio::kExitCompilerSpawn, err.what(), std::string(command_name)}, as_json);
  }

  return EmitSuccess(
      {
          {"command", std::string(command_name)},
          {"mode", "execute"},
          {"message", "completed compiler " + std::string(command_name) + " via compile-plan: " + plan.plan_path.string()},
          {"manifest_path", plan.manifest_path.string()},
          {"workspace_root", plan.workspace_root.string()},
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
          {"intent", request.intent},
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

int HandleAdd(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("add");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", spio::kExitUsage, "add requires a package name", "add"}, as_json);
  }

  spio::AddDependencyRequest request{
      .manifest_path = "spio.toml",
      .package_name = args.front(),
  };
  bool saw_source = false;
  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "add"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--alias")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--alias requires a value", "add"}, as_json);
      }
      request.alias = args[index];
    }
    else if (args[index] == "--dev")
    {
      request.section = spio::DependencySection::kDevDependencies;
    }
    else if (args[index] == "--path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--path requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", spio::kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_git = false;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--git")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--git requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", spio::kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_git = true;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--rev")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--rev requires a value", "add"}, as_json);
      }
      request.rev = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for add: " + args[index], "add"}, as_json);
    }
  }

  if (!saw_source)
  {
    return EmitError({"UsageError", spio::kExitUsage, "add requires either --path <path> or --git <source> --rev <rev>", "add"}, as_json);
  }

  try
  {
    const spio::DependencyCommandResult result = spio::AddDependencyAndRefreshLock(request);
    return EmitSuccess(
        {
            {"command", "add"},
            {"message", "added dependency '" + result.alias + "' and refreshed lockfile"},
            {"manifest_path", result.manifest_path.string()},
            {"lockfile_path", result.lockfile_path.string()},
            {"alias", result.alias},
            {"package", result.package_name},
            {"section", spio::DependencySectionName(result.section)},
            {"packages", result.package_count},
        },
        as_json);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "add"}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), "add"}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), "add"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "add"}, as_json);
  }
}

int HandleRemove(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("remove");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", spio::kExitUsage, "remove requires a dependency alias or package name", "remove"}, as_json);
  }

  spio::RemoveDependencyRequest request{
      .manifest_path = "spio.toml",
      .target = args.front(),
  };
  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "remove"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--dev")
    {
      request.section = spio::DependencySection::kDevDependencies;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for remove: " + args[index], "remove"}, as_json);
    }
  }

  try
  {
    const spio::DependencyCommandResult result = spio::RemoveDependencyAndRefreshLock(request);
    return EmitSuccess(
        {
            {"command", "remove"},
            {"message", "removed dependency '" + result.alias + "' and refreshed lockfile"},
            {"manifest_path", result.manifest_path.string()},
            {"lockfile_path", result.lockfile_path.string()},
            {"alias", result.alias},
            {"package", result.package_name},
            {"section", spio::DependencySectionName(result.section)},
            {"packages", result.package_count},
        },
        as_json);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "remove"}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), "remove"}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), "remove"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "remove"}, as_json);
  }
}

int HandleFetch(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("fetch");
  }

  fs::path manifest_path = "spio.toml";
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "fetch"}, as_json);
      }
      manifest_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for fetch: " + args[index], "fetch"}, as_json);
    }
  }

  try
  {
    const spio::FetchCommandResult result = spio::FetchDependencies(manifest_path);
    return EmitSuccess(
        {
            {"command", "fetch"},
            {"message", "fetched dependency sources for " + std::to_string(result.package_count) + " package(s)"},
            {"manifest_path", result.manifest_path.string()},
            {"packages", result.package_count},
            {"git_packages", result.git_package_count},
        },
        as_json);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "fetch"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "fetch"}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), "fetch"}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), "fetch"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "fetch"}, as_json);
  }
}

int HandleLock(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("lock");
  }

  fs::path manifest_path = "spio.toml";
  bool check_only = false;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "lock"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (args[index] == "--check")
    {
      check_only = true;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for lock: " + args[index], "lock"}, as_json);
    }
  }

  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", spio::kExitManifest, "manifest not found: " + manifest_path.string(), "lock"}, as_json);
  }

  spio::LockGenerationResult generated;
  try
  {
    generated = spio::ResolveSingleVersionLockfile(manifest_path);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "lock"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "lock"}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), "lock"}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), "lock"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "lock"}, as_json);
  }

  const std::string rendered = spio::SerializeLockfileCanonical(generated.lockfile);
  if (check_only)
  {
    if (!fs::exists(generated.lockfile_path))
    {
      return EmitError({"LockfileError", spio::kExitLock, "lockfile missing: " + generated.lockfile_path.string(), "lock"}, as_json);
    }

    try
    {
      if (ReadFile(generated.lockfile_path) != rendered)
      {
        return EmitError({"LockfileError", spio::kExitLock, "lockfile is stale: " + generated.lockfile_path.string(), "lock"}, as_json);
      }
    }
    catch (const std::exception &err)
    {
      return EmitError({"LockfileError", spio::kExitLock, err.what(), "lock"}, as_json);
    }

    return EmitSuccess(
        {
            {"command", "lock"},
            {"message", "lockfile is up to date: " + generated.lockfile_path.string()},
            {"manifest_path", generated.manifest_path.string()},
            {"lockfile_path", generated.lockfile_path.string()},
            {"mode", "check"},
            {"packages", generated.lockfile.packages.size()},
        },
        as_json);
  }

  try
  {
    std::ofstream out(generated.lockfile_path);
    if (!out)
    {
      throw std::runtime_error("failed to open lockfile for write: " + generated.lockfile_path.string());
    }
    out << rendered;
    if (!out.good())
    {
      throw std::runtime_error("failed to write lockfile: " + generated.lockfile_path.string());
    }
  }
  catch (const std::exception &err)
  {
    return EmitError({"LockfileError", spio::kExitLock, err.what(), "lock"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "lock"},
          {"message", "wrote lockfile: " + generated.lockfile_path.string()},
          {"manifest_path", generated.manifest_path.string()},
          {"lockfile_path", generated.lockfile_path.string()},
          {"mode", "write"},
          {"packages", generated.lockfile.packages.size()},
      },
      as_json);
}

int HandleTree(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("tree");
  }

  fs::path manifest_path = "spio.toml";
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "tree"}, as_json);
      }
      manifest_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for tree: " + args[index], "tree"}, as_json);
    }
  }

  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", spio::kExitManifest, "manifest not found: " + manifest_path.string(), "tree"}, as_json);
  }

  spio::LockGenerationResult graph;
  try
  {
    graph = spio::ResolveSingleVersionLockfile(manifest_path);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "tree"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "tree"}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), "tree"}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), "tree"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "tree"}, as_json);
  }

  if (as_json)
  {
    json packages = json::array();
    for (const spio::LockPackage &package : graph.lockfile.packages)
    {
      json item{
          {"id", package.id},
          {"name", package.name},
          {"version", package.version},
          {"source_kind", package.source_kind},
          {"dependencies", package.dependencies},
      };
      if (package.git.has_value())
      {
        item["git"] = *package.git;
      }
      if (package.rev.has_value())
      {
        item["rev"] = *package.rev;
      }
      packages.push_back(std::move(item));
    }

    std::cout << json({
                     {"command", "tree"},
                     {"manifest_path", graph.manifest_path.string()},
                     {"lockfile_path", graph.lockfile_path.string()},
                     {"resolver", graph.lockfile.resolver},
                     {"root_ids", graph.root_ids},
                     {"packages", packages},
                 })
                     .dump()
              << '\n';
    return spio::kExitSuccess;
  }

  std::cout << spio::RenderDependencyTreeText(graph);
  return spio::kExitSuccess;
}

int HandlePack(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("pack");
  }

  spio::PackRequest request;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "pack"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--package")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--package requires a value", "pack"}, as_json);
      }
      request.package_name = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--output requires a value", "pack"}, as_json);
      }
      request.output_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for pack: " + args[index], "pack"}, as_json);
    }
  }

  try
  {
    const spio::PackResult result = spio::WriteSourcePackage(request);
    return EmitSuccess(
        {
            {"command", "pack"},
            {"message", "wrote source package: " + result.archive_path.string()},
            {"manifest_path", result.manifest_path.string()},
            {"package_root", result.package_root.string()},
            {"archive_path", result.archive_path.string()},
            {"archive_prefix", result.archive_prefix},
            {"package", result.package_name},
            {"version", result.package_version},
            {"files", result.file_count},
        },
        as_json);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "pack"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "pack"}, as_json);
  }
  catch (const spio::PackError &err)
  {
    return EmitError({"PackError", spio::kExitPack, err.what(), "pack"}, as_json);
  }
}

}  // namespace

namespace spio
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
      std::cout << "spio " << kVersion << '\n';
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

  if (command == "machine-info")
  {
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
  }
  if (command == "new")
  {
    return HandleNew(args, global_json);
  }
  if (command == "init")
  {
    return HandleInit(args, global_json);
  }
  if (command == "check")
  {
    return HandleCheck(args, global_json);
  }
  if (command == "add")
  {
    return HandleAdd(args, global_json);
  }
  if (command == "remove")
  {
    return HandleRemove(args, global_json);
  }
  if (command == "fetch")
  {
    return HandleFetch(args, global_json);
  }
  if (command == "build")
  {
    return HandleBuild(args, global_json);
  }
  if (command == "run")
  {
    return HandleRun(args, global_json);
  }
  if (command == "test")
  {
    return HandleTest(args, global_json);
  }
  if (command == "lock")
  {
    return HandleLock(args, global_json);
  }
  if (command == "tree")
  {
    return HandleTree(args, global_json);
  }
  if (command == "pack")
  {
    return HandlePack(args, global_json);
  }
  if (command == "tool")
  {
    if (args.size() == 1 && args.front() == "--help")
    {
      return PrintCommandUsage("tool");
    }
    if (args.size() == 1 && args.front() == "install")
    {
      return EmitBootstrapNotImplemented("tool install", global_json);
    }
    return EmitError({"UsageError", kExitUsage, "tool requires the 'install' subcommand", "tool"}, global_json);
  }
  if (command == "publish")
  {
    return EmitBootstrapNotImplemented(command, global_json);
  }

  return EmitError({"UsageError", kExitUsage, "unknown command: " + command, command}, global_json);
}

}  // namespace spio
