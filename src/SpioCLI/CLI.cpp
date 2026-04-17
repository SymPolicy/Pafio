#include "SpioCLI/CLI.hpp"

#include "SpioCompat/Compat.hpp"
#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Version.hpp"
#include "SpioManifest/Lockfile.hpp"
#include "SpioManifest/Manifest.hpp"
#include "SpioPack/Pack.hpp"
#include "SpioPlan/CompilePlan.hpp"
#include "SpioPublish/Publish.hpp"
#include "SpioRegistryServer/Publish.hpp"
#include "SpioResolve/Resolver.hpp"
#include "SpioSecurity/RegistrySecurity.hpp"
#include "SpioTool/Install.hpp"
#include "SpioTree/Render.hpp"
#include "SpioVendor/Vendor.hpp"
#include "SpioWorkflow/Dependencies.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <toml++/toml.h>

#include <fcntl.h>
#include <poll.h>
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

struct WorkflowFlags
{
  bool locked = false;
  bool offline = false;
};

void CloseFd(int &fd)
{
  if (fd >= 0)
  {
    close(fd);
    fd = -1;
  }
}

bool SetNonBlocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void DrainReadablePipe(int &fd, std::string &text)
{
  std::array<char, 4096> buffer{};
  while (fd >= 0)
  {
    const ssize_t read_size = read(fd, buffer.data(), buffer.size());
    if (read_size > 0)
    {
      text.append(buffer.data(), static_cast<size_t>(read_size));
      continue;
    }
    if (read_size == 0)
    {
      CloseFd(fd);
      return;
    }
    if (errno == EINTR)
    {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return;
    }
    CloseFd(fd);
    return;
  }
}

void ReapChildBestEffort(const pid_t child)
{
  int status = 0;
  while (waitpid(child, &status, 0) < 0)
  {
    if (errno != EINTR)
    {
      return;
    }
  }
}

int WaitForChildExit(const pid_t child)
{
  int status = 0;
  while (waitpid(child, &status, 0) < 0)
  {
    if (errno != EINTR)
    {
      return 1;
    }
  }
  if (WIFEXITED(status))
  {
    return WEXITSTATUS(status);
  }
  return 1;
}

std::string TrimAsciiWhitespace(std::string value)
{
  const auto is_space = [](const unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
  };

  while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
  {
    value.erase(value.begin());
  }
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
  {
    value.pop_back();
  }
  return value;
}

std::optional<std::string> NormalizeRegistryHeader(std::string value)
{
  const size_t separator = value.find(':');
  if (separator == std::string::npos)
  {
    return std::nullopt;
  }

  std::string name = TrimAsciiWhitespace(value.substr(0U, separator));
  std::string header_value = TrimAsciiWhitespace(value.substr(separator + 1U));
  if (name.empty() || header_value.empty())
  {
    return std::nullopt;
  }
  return name + ": " + header_value;
}

json BuildMachineInfoPayload()
{
  return {
      {"tool", "spio"},
      {"version", spio::kVersion},
      {"bootstrap", true},
      {"active_integration_phase", "compile-plan-live"},
      {"implementation_language", "c++"},
      {"supported_manifests", json::array({1})},
      {"supported_lockfiles", json::array({1})},
      {"supported_contracts", {
                                 {"machine_info", json::array({1})},
                                 {"compile_plan", json::array({1})},
                                 {"project_graph", json::array({1})},
                                 {"toolchain_state", json::array({1})},
                                 {"workflow_success_payloads", json::array({1})},
                             }},
      {"supported_contract_versions", {
                                          {"machine_info", json::array({1})},
                                          {"compile_plan", json::array({1})},
                                          {"project_graph", json::array({1})},
                                          {"toolchain_state", json::array({1})},
                                          {"workflow_success_payloads", json::array({1})},
                                      }},
      {"supported_adapter_modes", json::array({"cli"})},
      {"feature_flags", {
                            {"live_compile_plan_handoff", true},
                            {"project_graph_payload", true},
                            {"toolchain_state_payload", true},
                            {"workflow_success_payloads", true},
                            {"runtime_event_payload", false},
                        }},
      {"notes", json::array({
                    "native c++ phase-3 minimal resolver core",
                    "compile-plan v1 is active for published styio handoff",
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
      << "  check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen]\n"
      << "  project-graph [--manifest-path <path>] [--styio-bin <path>] [--json]\n"
      << "  add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]\n"
      << "  remove <alias-or-package> [--dev] [--manifest-path <path>]\n"
      << "  fetch [--manifest-path <path>] [--locked|--offline|--frozen]\n"
      << "  lock [--manifest-path <path>] [--check] [--offline]\n"
      << "  tree [--manifest-path <path>]\n"
      << "  vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]\n"
      << "  build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]\n"
      << "  run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]\n"
      << "  test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]\n"
      << "  pack [--manifest-path <path>] [--package <package-name>] [--output <path>]\n"
      << "  publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <path-or-url>] [--registry-profile <name>] [--registry-policy-file <path>] [--registry-header <name:value>] [--dry-run]\n"
      << "  tool status [--manifest-path <path>] [--styio-bin <path>] [--json]\n"
      << "  tool install --styio-bin <path>\n"
      << "  tool use --version <compiler-version> [--channel <channel>]\n"
      << "  tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]\n";
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
    std::cout << "usage: spio check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen]\n";
  }
  else if (command == "project-graph")
  {
    std::cout << "usage: spio project-graph [--manifest-path <path>] [--styio-bin <path>] [--json]\n";
  }
  else if (command == "add")
  {
    std::cout << "usage: spio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]\n";
  }
  else if (command == "remove")
  {
    std::cout << "usage: spio remove <alias-or-package> [--dev] [--manifest-path <path>]\n";
  }
  else if (command == "fetch")
  {
    std::cout << "usage: spio fetch [--manifest-path <path>] [--locked|--offline|--frozen]\n";
  }
  else if (command == "lock")
  {
    std::cout << "usage: spio lock [--manifest-path <path>] [--check] [--offline]\n";
  }
  else if (command == "tree")
  {
    std::cout << "usage: spio tree [--manifest-path <path>]\n";
  }
  else if (command == "vendor")
  {
    std::cout << "usage: spio vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]\n";
  }
  else if (command == "build")
  {
    std::cout << "usage: spio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]\n";
  }
  else if (command == "run")
  {
    std::cout << "usage: spio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]\n";
  }
  else if (command == "test")
  {
    std::cout << "usage: spio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]\n";
  }
  else if (command == "pack")
  {
    std::cout << "usage: spio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]\n";
  }
  else if (command == "publish")
  {
    std::cout << "usage: spio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <path-or-url>] [--registry-profile <name>] [--registry-policy-file <path>] [--registry-header <name:value>] [--dry-run]\n";
  }
  else if (command == "tool")
  {
    std::cout << "usage:\n";
    std::cout << "  spio tool status [--manifest-path <path>] [--styio-bin <path>] [--json]\n";
    std::cout << "  spio tool install --styio-bin <path>\n";
    std::cout << "  spio tool use --version <compiler-version> [--channel <channel>]\n";
    std::cout << "  spio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]\n";
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

bool IsHttpRegistryRoot(const std::string &value)
{
  return value.starts_with("http://") || value.starts_with("https://");
}

bool IsFileRegistryRoot(const std::string &value)
{
  return value.starts_with("file://");
}

fs::path FileRegistryUrlToPath(const std::string &value)
{
  return fs::path(value.substr(std::string("file://").size()));
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
  if (!SetNonBlocking(stdout_pipe[0]) || !SetNonBlocking(stderr_pipe[0]))
  {
    CloseFd(stdout_pipe[0]);
    CloseFd(stderr_pipe[0]);
    ReapChildBestEffort(child);
    throw std::runtime_error("failed to configure child process pipes");
  }

  ChildProcessResult result;
  std::array<pollfd, 2> poll_fds{};
  while (stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0)
  {
    nfds_t poll_count = 0;
    int stdout_index = -1;
    int stderr_index = -1;
    if (stdout_pipe[0] >= 0)
    {
      stdout_index = static_cast<int>(poll_count);
      poll_fds[poll_count++] = pollfd{stdout_pipe[0], static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
    }
    if (stderr_pipe[0] >= 0)
    {
      stderr_index = static_cast<int>(poll_count);
      poll_fds[poll_count++] = pollfd{stderr_pipe[0], static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
    }

    int ready = 0;
    while ((ready = poll(poll_fds.data(), poll_count, -1)) < 0)
    {
      if (errno != EINTR)
      {
        CloseFd(stdout_pipe[0]);
        CloseFd(stderr_pipe[0]);
        ReapChildBestEffort(child);
        throw std::runtime_error("failed to read child process output");
      }
    }
    if (ready == 0)
    {
      continue;
    }
    if (stdout_index >= 0 && poll_fds[stdout_index].revents != 0)
    {
      DrainReadablePipe(stdout_pipe[0], result.stdout_text);
    }
    if (stderr_index >= 0 && poll_fds[stderr_index].revents != 0)
    {
      DrainReadablePipe(stderr_pipe[0], result.stderr_text);
    }
  }
  result.exit_code = WaitForChildExit(child);
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

bool ConsumeWorkflowFlag(const std::string &argument, WorkflowFlags &flags)
{
  if (argument == "--locked")
  {
    flags.locked = true;
    return true;
  }
  if (argument == "--offline")
  {
    flags.offline = true;
    return true;
  }
  if (argument == "--frozen")
  {
    flags.locked = true;
    flags.offline = true;
    return true;
  }
  return false;
}

spio::ResolveOptions BuildResolveOptions(
    const fs::path &manifest_path,
    const WorkflowFlags &flags,
    const std::optional<fs::path> &vendor_root_override = std::nullopt)
{
  spio::ResolveOptions options;
  options.offline = flags.offline;
  if (vendor_root_override.has_value())
  {
    options.vendor_root = spio::CanonicalAbsolutePath(*vendor_root_override);
  }
  else
  {
    const fs::path default_vendor_root = spio::ProjectVendorRootForManifest(manifest_path);
    if (fs::exists(default_vendor_root))
    {
      options.vendor_root = default_vendor_root;
    }
  }
  return options;
}

std::optional<spio::CommandError> ValidateLockedPolicy(
    const fs::path &manifest_path,
    std::string_view command,
    const WorkflowFlags &flags,
    const spio::ResolveOptions &resolve_options)
{
  if (!flags.locked)
  {
    return std::nullopt;
  }

  const fs::path lockfile_path = manifest_path.parent_path() / "spio.lock";
  if (!fs::exists(lockfile_path))
  {
    return spio::CommandError{
        .category = "LockfileError",
        .code = spio::kExitLock,
        .message = "lockfile missing: " + lockfile_path.string(),
        .command = std::string(command),
    };
  }

  try
  {
    const spio::LockGenerationResult generated = spio::ResolveSingleVersionLockfile(manifest_path, resolve_options);
    if (ReadFile(lockfile_path) != spio::SerializeLockfileCanonical(generated.lockfile))
    {
      return spio::CommandError{
          .category = "LockfileError",
          .code = spio::kExitLock,
          .message = "lockfile is stale: " + lockfile_path.string(),
          .command = std::string(command),
      };
    }
  }
  catch (const spio::ValidationError &err)
  {
    return spio::CommandError{"ManifestError", spio::kExitManifest, err.what(), std::string(command)};
  }
  catch (const spio::WorkspaceError &err)
  {
    return spio::CommandError{"WorkspaceError", spio::kExitWorkspace, err.what(), std::string(command)};
  }
  catch (const spio::ResolutionError &err)
  {
    return spio::CommandError{"ResolutionError", spio::kExitResolve, err.what(), std::string(command)};
  }
  catch (const spio::FetchError &err)
  {
    return spio::CommandError{"FetchError", spio::kExitFetch, err.what(), std::string(command)};
  }
  catch (const spio::CacheError &err)
  {
    return spio::CommandError{"CacheError", spio::kExitCache, err.what(), std::string(command)};
  }

  return std::nullopt;
}

struct ParsedToolchainPinPreview
{
  std::optional<std::string> channel;
  std::optional<std::string> version;
};

struct ToolchainPreview
{
  std::string source = "unavailable";
  std::string detail = "No toolchain pin or managed compiler was resolved.";
  std::optional<fs::path> pin_path;
  std::optional<std::string> channel;
  std::optional<std::string> version;
  std::optional<fs::path> candidate_binary;
};

ParsedToolchainPinPreview ParseToolchainPinPreview(const fs::path &pin_path)
{
  ParsedToolchainPinPreview parsed;
  if (!fs::exists(pin_path))
  {
    return parsed;
  }

  toml::table doc = toml::parse_file(pin_path.string());
  const toml::table *styio_table = doc["styio"].as_table();
  if (styio_table == nullptr)
  {
    return parsed;
  }
  if (const auto channel = styio_table->get_as<std::string>("channel"))
  {
    parsed.channel = channel->get();
  }
  if (const auto version = styio_table->get_as<std::string>("version"))
  {
    parsed.version = version->get();
  }
  return parsed;
}

ToolchainPreview BuildToolchainPreview(
    const std::optional<fs::path> &manifest_path,
    const std::optional<std::string> &explicit_styio_bin,
    const std::optional<std::string> &manifest_channel)
{
  ToolchainPreview preview;

  if (explicit_styio_bin.has_value() && !explicit_styio_bin->empty())
  {
    preview.source = "environment";
    preview.detail = "CLI selected an explicit styio binary through --styio-bin.";
    preview.candidate_binary = spio::CanonicalAbsolutePath(*explicit_styio_bin);
    preview.channel = manifest_channel;
    return preview;
  }

  if (const char *env = std::getenv("SPIO_STYIO_BIN"); env != nullptr && env[0] != '\0')
  {
    preview.source = "environment";
    preview.detail = "SPIO_STYIO_BIN selects the active styio compiler.";
    preview.candidate_binary = spio::CanonicalAbsolutePath(env);
    preview.channel = manifest_channel;
    return preview;
  }

  if (manifest_path.has_value())
  {
    if (const std::optional<fs::path> pin_path = spio::FindProjectToolchainPinPath(*manifest_path); pin_path.has_value())
    {
      preview.source = "project-pin";
      preview.pin_path = *pin_path;
      try
      {
        const ParsedToolchainPinPreview pin = ParseToolchainPinPreview(*pin_path);
        preview.channel = pin.channel.has_value() ? pin.channel : manifest_channel;
        preview.version = pin.version;
        if (preview.channel.has_value() && preview.version.has_value())
        {
          const fs::path spio_home = spio::ResolveSpioHome();
          const fs::path candidate = spio::ManagedStyioBinaryPath(
              spio::ManagedStyioInstallRoot(spio_home, *preview.channel, *preview.version));
          if (fs::exists(candidate))
          {
            preview.detail =
                "Project toolchain pin resolved to managed styio " + *preview.channel + "/" + *preview.version + ".";
            preview.candidate_binary = candidate;
          }
          else
          {
            preview.detail =
                "Project toolchain pin requires a managed styio install that is not present: " +
                *preview.channel + "/" + *preview.version + ".";
          }
        }
        else
        {
          preview.detail = "Project toolchain pin is present but channel/version could not be fully resolved.";
        }
      }
      catch (const std::exception &err)
      {
        preview.detail = std::string("Project toolchain pin could not be parsed: ") + err.what();
      }
      return preview;
    }
  }

  try
  {
    if (const std::optional<fs::path> spio_home = spio::ResolveOptionalSpioHome(); spio_home.has_value())
    {
      const fs::path current_root = spio::ManagedStyioCurrentRoot(*spio_home);
      const fs::path current_binary = spio::ManagedStyioBinaryPath(current_root);
      const fs::path current_metadata = spio::ManagedStyioMetadataPath(current_root);
      if (fs::exists(current_binary))
      {
        preview.source = "managed-current";
        preview.detail = "Managed current styio compiler is available through SPIO_HOME.";
        preview.candidate_binary = current_binary;
        if (fs::exists(current_metadata))
        {
          try
          {
            const json metadata = json::parse(ReadFile(current_metadata));
            if (metadata.contains("channel") && metadata["channel"].is_string())
            {
              preview.channel = metadata["channel"].get<std::string>();
            }
            if (metadata.contains("compiler_version") && metadata["compiler_version"].is_string())
            {
              preview.version = metadata["compiler_version"].get<std::string>();
            }
          }
          catch (const std::exception &)
          {
          }
        }
        return preview;
      }
    }
  }
  catch (const std::exception &)
  {
  }

  if (manifest_channel.has_value())
  {
    preview.source = "unknown";
    preview.detail = "Manifest declares a toolchain channel but no project pin or managed compiler resolved.";
    preview.channel = manifest_channel;
    return preview;
  }

  return preview;
}

json BuildDependencyPayload(
    const std::string &source_package_name,
    const spio::Dependency &dependency,
    bool is_dev_dependency)
{
  const auto source_kind_name = [&]() -> std::string {
    switch (dependency.source_kind)
    {
      case spio::DependencySourceKind::kPath:
        return "path";
      case spio::DependencySourceKind::kGit:
        return "git";
      case spio::DependencySourceKind::kRegistry:
        return "registry";
    }
    return "unknown";
  };

  std::string requirement;
  bool is_workspace_reference = false;
  json path_source = nullptr;
  json git_source = nullptr;
  json registry_root = nullptr;
  switch (dependency.source_kind)
  {
    case spio::DependencySourceKind::kPath:
      requirement = "path:" + dependency.source;
      is_workspace_reference = true;
      path_source = dependency.source;
      break;
    case spio::DependencySourceKind::kGit:
      requirement = "git:" + dependency.source;
      git_source = dependency.source;
      if (dependency.rev.has_value())
      {
        requirement += "#" + *dependency.rev;
      }
      break;
    case spio::DependencySourceKind::kRegistry:
      requirement = "registry:" + dependency.source;
      registry_root = dependency.source;
      if (dependency.version.has_value())
      {
        requirement += "@" + *dependency.version;
      }
      break;
  }

  return {
      {"source_package_name", source_package_name},
      {"dependency_name", dependency.alias},
      {"kind", is_dev_dependency ? "dev" : "runtime"},
      {"requirement", requirement},
      {"is_workspace_reference", is_workspace_reference},
      {"source_kind", source_kind_name()},
      {"package", dependency.package.has_value() ? json(*dependency.package) : json(nullptr)},
      {"path", path_source},
      {"git", git_source},
      {"rev", dependency.rev.has_value() ? json(*dependency.rev) : json(nullptr)},
      {"registry", registry_root},
      {"version", dependency.version.has_value() ? json(*dependency.version) : json(nullptr)},
      {"publish_blocking", dependency.source_kind != spio::DependencySourceKind::kRegistry},
  };
}

std::string RegistryTransportName(const std::string &registry_root)
{
  if (registry_root.rfind("https://", 0U) == 0U)
  {
    return "https";
  }
  if (registry_root.rfind("http://", 0U) == 0U)
  {
    return "http";
  }
  if (registry_root.rfind("file://", 0U) == 0U)
  {
    return "file";
  }
  return "unknown";
}

void AppendUniqueReason(std::vector<std::string> &reasons, const std::string &reason)
{
  if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end())
  {
    reasons.push_back(reason);
  }
}

struct RegistrySourceAggregate
{
  std::string transport = "unknown";
  size_t dependency_refs = 0U;
  std::set<std::string> package_names;
};

json BuildPackageDistributionEntryPayload(
    const fs::path &package_manifest_path,
    const spio::PackageConfig &package,
    std::map<std::string, RegistrySourceAggregate> &registry_sources)
{
  size_t runtime_registry_dependencies = 0U;
  size_t runtime_path_dependencies = 0U;
  size_t runtime_git_dependencies = 0U;
  size_t dev_registry_dependencies = 0U;
  size_t dev_path_dependencies = 0U;
  size_t dev_git_dependencies = 0U;
  std::vector<std::string> blocking_reasons;

  if (!package.publish)
  {
    blocking_reasons.push_back("package publish = false");
  }

  auto accumulate_dependency = [&](const spio::Dependency &dependency, const std::string &section_name, bool is_dev_dependency) {
    auto &registry_counter = is_dev_dependency ? dev_registry_dependencies : runtime_registry_dependencies;
    auto &path_counter = is_dev_dependency ? dev_path_dependencies : runtime_path_dependencies;
    auto &git_counter = is_dev_dependency ? dev_git_dependencies : runtime_git_dependencies;

    switch (dependency.source_kind)
    {
      case spio::DependencySourceKind::kPath:
        ++path_counter;
        AppendUniqueReason(
            blocking_reasons,
            "dependency in [" + section_name + "] uses a local path or workspace source: " + dependency.alias);
        return;
      case spio::DependencySourceKind::kGit:
        ++git_counter;
        AppendUniqueReason(
            blocking_reasons,
            "dependency in [" + section_name + "] uses a git source: " + dependency.alias);
        return;
      case spio::DependencySourceKind::kRegistry:
        ++registry_counter;
        if (dependency.source.empty())
        {
          AppendUniqueReason(
              blocking_reasons,
              "registry dependency in [" + section_name + "] is missing registry = \"<url>\": " + dependency.alias);
        }
        else
        {
          RegistrySourceAggregate &aggregate = registry_sources[dependency.source];
          aggregate.transport = RegistryTransportName(dependency.source);
          ++aggregate.dependency_refs;
          aggregate.package_names.insert(package.name);
        }
        if (!dependency.package.has_value() || dependency.package->empty())
        {
          AppendUniqueReason(
              blocking_reasons,
              "registry dependency in [" + section_name + "] is missing package = \"namespace/name\": " + dependency.alias);
        }
        if (!dependency.version.has_value() || dependency.version->empty())
        {
          AppendUniqueReason(
              blocking_reasons,
              "registry dependency in [" + section_name + "] is missing version = \"x.y.z\": " + dependency.alias);
        }
        return;
    }
  };

  for (const spio::Dependency &dependency : package.dependencies)
  {
    accumulate_dependency(dependency, "dependencies", false);
  }
  for (const spio::Dependency &dependency : package.dev_dependencies)
  {
    accumulate_dependency(dependency, "dev-dependencies", true);
  }

  return {
      {"package_name", package.name},
      {"manifest_path", package_manifest_path.string()},
      {"publish_enabled", package.publish},
      {"publish_ready", package.publish && blocking_reasons.empty()},
      {"blocking_reasons", blocking_reasons},
      {"runtime_registry_dependencies", runtime_registry_dependencies},
      {"runtime_path_dependencies", runtime_path_dependencies},
      {"runtime_git_dependencies", runtime_git_dependencies},
      {"dev_registry_dependencies", dev_registry_dependencies},
      {"dev_path_dependencies", dev_path_dependencies},
      {"dev_git_dependencies", dev_git_dependencies},
  };
}

json BuildPackageDistributionPayload(
    const std::vector<json> &package_distribution_packages,
    const std::map<std::string, RegistrySourceAggregate> &registry_sources)
{
  size_t publishable_packages = 0U;
  size_t blocked_packages = 0U;
  for (const json &package : package_distribution_packages)
  {
    if (package.value("publish_ready", false))
    {
      ++publishable_packages;
    }
    else
    {
      ++blocked_packages;
    }
  }

  json registry_source_list = json::array();
  for (const auto &[registry_root, aggregate] : registry_sources)
  {
    registry_source_list.push_back({
        {"registry_root", registry_root},
        {"transport", aggregate.transport},
        {"dependency_refs", aggregate.dependency_refs},
        {"packages", std::vector<std::string>(aggregate.package_names.begin(), aggregate.package_names.end())},
    });
  }

  return {
      {"schema_version", 1},
      {"packages", package_distribution_packages},
      {"registry_sources", registry_source_list},
      {"publishable_packages", publishable_packages},
      {"blocked_packages", blocked_packages},
  };
}

std::optional<size_t> ReadVendoredGitSnapshotCount(const fs::path &metadata_path)
{
  if (!fs::exists(metadata_path))
  {
    return std::nullopt;
  }

  try
  {
    const json metadata = json::parse(ReadFile(metadata_path));
    if (!metadata.contains("git_snapshots") || !metadata.at("git_snapshots").is_array())
    {
      return std::nullopt;
    }
    return metadata.at("git_snapshots").size();
  }
  catch (const std::exception &)
  {
    return std::nullopt;
  }
}

json BuildSourceStatePayload(
    const fs::path &vendor_root,
    const size_t declared_git_dependencies,
    const size_t declared_registry_dependencies)
{
  const fs::path vendor_metadata_path = vendor_root / "spio-vendor.json";
  const bool vendor_present = fs::exists(vendor_root);
  const bool vendor_metadata_present = fs::exists(vendor_metadata_path);
  const size_t vendored_git_snapshots =
      ReadVendoredGitSnapshotCount(vendor_metadata_path).value_or(0U);

  const std::optional<fs::path> spio_home = spio::ResolveOptionalSpioHome();
  json git_cache = {
      {"repos_root", nullptr},
      {"checkouts_root", nullptr},
      {"repos_present", false},
      {"checkouts_present", false},
  };
  json registry_cache = {
      {"cache_root", nullptr},
      {"index_root", nullptr},
      {"blob_root", nullptr},
      {"checkout_root", nullptr},
      {"index_present", false},
      {"blobs_present", false},
      {"checkouts_present", false},
  };

  if (spio_home.has_value())
  {
    const fs::path git_repos_root = spio::GitMirrorCacheRoot(*spio_home);
    const fs::path git_checkouts_root = spio::GitCheckoutCacheRoot(*spio_home);
    git_cache = {
        {"repos_root", git_repos_root.string()},
        {"checkouts_root", git_checkouts_root.string()},
        {"repos_present", fs::exists(git_repos_root)},
        {"checkouts_present", fs::exists(git_checkouts_root)},
    };

    const fs::path registry_cache_root = spio::RegistryCacheRoot(*spio_home);
    const fs::path registry_index_root = spio::RegistryIndexCacheRoot(*spio_home);
    const fs::path registry_blob_root = spio::RegistryBlobCacheRoot(*spio_home);
    const fs::path registry_checkout_root = spio::RegistryCheckoutRoot(*spio_home);
    registry_cache = {
        {"cache_root", registry_cache_root.string()},
        {"index_root", registry_index_root.string()},
        {"blob_root", registry_blob_root.string()},
        {"checkout_root", registry_checkout_root.string()},
        {"index_present", fs::exists(registry_index_root)},
        {"blobs_present", fs::exists(registry_blob_root)},
        {"checkouts_present", fs::exists(registry_checkout_root)},
    };
  }

  return {
      {"schema_version", 1},
      {"spio_home", spio_home.has_value() ? json(spio_home->string()) : json(nullptr)},
      {"declared_git_dependencies", declared_git_dependencies},
      {"declared_registry_dependencies", declared_registry_dependencies},
      {"git_cache", git_cache},
      {"registry_cache", registry_cache},
      {"vendor", {
                     {"vendor_root", vendor_root.string()},
                     {"metadata_path", vendor_metadata_path.string()},
                     {"vendor_present", vendor_present},
                     {"metadata_present", vendor_metadata_present},
                     {"git_snapshots", vendored_git_snapshots},
                 }},
  };
}

json BuildTargetPayload(
    const std::string &package_name,
    const fs::path &root_dir,
    std::string_view kind,
    const std::string &name,
    const std::string &relative_path)
{
  const fs::path absolute_file_path = spio::CanonicalAbsolutePath(root_dir / relative_path);
  return {
      {"id", package_name + ":" + std::string(kind) + ":" + name},
      {"package_name", package_name},
      {"kind", std::string(kind)},
      {"name", name},
      {"file_path", absolute_file_path.string()},
  };
}

json BuildCompilerHandshakePayload(
    const fs::path &binary,
    const spio::CompatibilityReport &report);

json ProbeCompilerMachineInfo(const fs::path &binary)
{
  const ChildProcessResult result = RunChildProcess(binary, {"--machine-info=json"});
  if (result.exit_code != 0)
  {
    throw std::runtime_error(
        "compiler machine-info probe failed with exit code " + std::to_string(result.exit_code));
  }

  const json parsed = json::parse(result.stdout_text);
  if (!parsed.is_object())
  {
    throw std::runtime_error("compiler machine-info did not produce a JSON object");
  }
  return parsed;
}

json BuildCompilerHandshakePayloadFromMachineInfo(
    const fs::path &binary,
    const json &machine_info,
    const std::optional<spio::CompatibilityReport> &report = std::nullopt)
{
  json supported_contract_versions = json::object();
  if (machine_info.contains("supported_contract_versions") &&
      machine_info["supported_contract_versions"].is_object())
  {
    supported_contract_versions = machine_info["supported_contract_versions"];
  }
  else if (machine_info.contains("supported_contracts") &&
           machine_info["supported_contracts"].is_object())
  {
    supported_contract_versions = machine_info["supported_contracts"];
  }
  else if (report.has_value())
  {
    supported_contract_versions = {
        {"compile_plan", report->supported_compile_plan_versions},
    };
  }

  json supported_adapter_modes = json::array();
  if (machine_info.contains("supported_adapter_modes") &&
      machine_info["supported_adapter_modes"].is_array())
  {
    supported_adapter_modes = machine_info["supported_adapter_modes"];
  }

  json feature_flags = json::object();
  if (machine_info.contains("feature_flags") && machine_info["feature_flags"].is_object())
  {
    feature_flags = machine_info["feature_flags"];
  }
  else if (report.has_value())
  {
    feature_flags = {
        {"compile_plan_consumer",
         std::find(report->supported_compile_plan_versions.begin(),
                   report->supported_compile_plan_versions.end(),
                   1) != report->supported_compile_plan_versions.end()},
    };
  }

  json capabilities = json::array();
  if (machine_info.contains("capabilities") && machine_info["capabilities"].is_array())
  {
    capabilities = machine_info["capabilities"];
  }
  else if (report.has_value())
  {
    capabilities = report->capabilities;
  }

  const std::string default_compiler_version =
      report.has_value() ? report->compiler_version : std::string("unknown");
  const std::string default_channel =
      report.has_value() ? report->compiler_channel : std::string("unknown");
  const std::string default_integration_phase =
      report.has_value() ? report->integration_phase : std::string("unknown");

  return {
      {"binary_path", binary.string()},
      {"tool", machine_info.value("tool", std::string("styio"))},
      {"compiler_version", machine_info.value("compiler_version", default_compiler_version)},
      {"channel", machine_info.value("channel", default_channel)},
      {"variant", machine_info.value("variant", std::string("unknown"))},
      {"capabilities", capabilities},
      {"supported_contract_versions", supported_contract_versions},
      {"integration_phase",
       machine_info.value(
           "integration_phase",
           machine_info.value("active_integration_phase", default_integration_phase))},
      {"supported_adapter_modes", supported_adapter_modes},
      {"feature_flags", feature_flags},
  };
}

json BuildCompilerHandshakePayload(
    const fs::path &binary,
    const spio::CompatibilityReport &report)
{
  try
  {
    return BuildCompilerHandshakePayloadFromMachineInfo(
        binary,
        ProbeCompilerMachineInfo(binary),
        report);
  }
  catch (const std::exception &)
  {
    return BuildCompilerHandshakePayloadFromMachineInfo(binary, json::object(), report);
  }
}

json BuildManagedToolchainsPayload()
{
  json installed = json::array();
  json payload = {
      {"spio_home", nullptr},
      {"current_binary", nullptr},
      {"current_metadata_path", nullptr},
      {"installed", installed},
  };

  const std::optional<fs::path> spio_home = spio::ResolveOptionalSpioHome();
  if (!spio_home.has_value())
  {
    return payload;
  }

  payload["spio_home"] = spio_home->string();
  const fs::path styio_root = spio::ManagedStyioRoot(*spio_home);
  const fs::path current_root = spio::ManagedStyioCurrentRoot(*spio_home);
  const fs::path current_binary = spio::ManagedStyioBinaryPath(current_root);
  const fs::path current_metadata = spio::ManagedStyioMetadataPath(current_root);
  if (fs::exists(current_binary))
  {
    payload["current_binary"] = current_binary.string();
  }
  if (fs::exists(current_metadata))
  {
    payload["current_metadata_path"] = current_metadata.string();
  }

  if (!fs::exists(styio_root))
  {
    return payload;
  }

  for (const fs::directory_entry &channel_entry : fs::directory_iterator(styio_root))
  {
    if (!channel_entry.is_directory())
    {
      continue;
    }
    const std::string channel = channel_entry.path().filename().string();
    if (channel == "current")
    {
      continue;
    }

    for (const fs::directory_entry &version_entry : fs::directory_iterator(channel_entry.path()))
    {
      if (!version_entry.is_directory())
      {
        continue;
      }
      const fs::path install_root = version_entry.path();
      const fs::path install_binary = spio::ManagedStyioBinaryPath(install_root);
      const fs::path install_metadata = spio::ManagedStyioMetadataPath(install_root);
      if (!fs::exists(install_binary))
      {
        continue;
      }
      installed.push_back({
          {"channel", channel},
          {"compiler_version", version_entry.path().filename().string()},
          {"install_root", install_root.string()},
          {"install_binary_path", install_binary.string()},
          {"install_metadata_path", fs::exists(install_metadata) ? json(install_metadata.string()) : json(nullptr)},
      });
    }
  }

  payload["installed"] = std::move(installed);
  return payload;
}

json BuildProjectPinStatePayload(const std::optional<fs::path> &manifest_path)
{
  if (!manifest_path.has_value())
  {
    return nullptr;
  }

  const std::optional<fs::path> pin_path = spio::FindProjectToolchainPinPath(*manifest_path);
  if (!pin_path.has_value())
  {
    return nullptr;
  }

  json payload = {
      {"path", pin_path->string()},
      {"channel", nullptr},
      {"version", nullptr},
      {"install_root", nullptr},
      {"install_binary_path", nullptr},
      {"install_present", false},
  };

  try
  {
    const ParsedToolchainPinPreview parsed = ParseToolchainPinPreview(*pin_path);
    if (parsed.channel.has_value())
    {
      payload["channel"] = *parsed.channel;
    }
    if (parsed.version.has_value())
    {
      payload["version"] = *parsed.version;
    }
    if (parsed.channel.has_value() && parsed.version.has_value())
    {
      const fs::path spio_home = spio::ResolveSpioHome();
      const fs::path install_root =
          spio::ManagedStyioInstallRoot(spio_home, *parsed.channel, *parsed.version);
      const fs::path install_binary = spio::ManagedStyioBinaryPath(install_root);
      payload["install_root"] = install_root.string();
      payload["install_binary_path"] = install_binary.string();
      payload["install_present"] = fs::exists(install_binary);
    }
  }
  catch (const std::exception &)
  {
  }

  return payload;
}

json BuildOptionalCompilerPayload(
    const std::optional<fs::path> &binary,
    std::optional<std::string> &out_error)
{
  out_error.reset();
  if (!binary.has_value())
  {
    return nullptr;
  }

  try
  {
    const spio::CompatibilityReport report = spio::CheckCompilerCompatibility(*binary);
    return BuildCompilerHandshakePayload(*binary, report);
  }
  catch (const std::exception &err)
  {
    try
    {
      return BuildCompilerHandshakePayloadFromMachineInfo(
          *binary,
          ProbeCompilerMachineInfo(*binary));
    }
    catch (const std::exception &probe_err)
    {
      out_error = std::string(err.what()) + "; machine-info probe failed: " + probe_err.what();
      return nullptr;
    }
  }
}

json ReadJsonArtifactOrNull(const fs::path &path)
{
  if (!fs::exists(path))
  {
    return nullptr;
  }

  try
  {
    return json::parse(ReadFile(path));
  }
  catch (const std::exception &)
  {
    return nullptr;
  }
}

json ReadJsonLinesArtifact(const fs::path &path)
{
  json lines = json::array();
  if (!fs::exists(path))
  {
    return lines;
  }

  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line))
  {
    const std::string trimmed = TrimAsciiWhitespace(line);
    if (trimmed.empty())
    {
      continue;
    }

    try
    {
      lines.push_back(json::parse(trimmed));
    }
    catch (const std::exception &)
    {
      lines.push_back({
          {"raw", trimmed},
      });
    }
  }

  return lines;
}

json BuildWorkflowSuccessPayload(
    std::string_view command_name,
    const spio::BuildPlanResult &plan,
    const WorkflowFlags &workflow_flags,
    const std::string &intent,
    const json &compatibility_payload,
    const ChildProcessResult &result)
{
  const fs::path receipt_path = plan.build_root / "receipt.json";
  const fs::path diagnostics_path = plan.diag_dir / "diagnostics.jsonl";
  return {
      {"command", std::string(command_name)},
      {"mode", "execute"},
      {"workflow_payload_version", 1},
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
      {"intent", intent},
      {"locked", workflow_flags.locked},
      {"offline", workflow_flags.offline},
      {"styio", compatibility_payload},
      {"receipt_path", receipt_path.string()},
      {"receipt", ReadJsonArtifactOrNull(receipt_path)},
      {"diagnostics_path", diagnostics_path.string()},
      {"diagnostics", ReadJsonLinesArtifact(diagnostics_path)},
      {"stdout", result.stdout_text},
      {"stderr", result.stderr_text},
  };
}

json BuildWorkflowFailurePayload(
    std::string_view command_name,
    const fs::path &compiler,
    const spio::BuildPlanResult &plan,
    const WorkflowFlags &workflow_flags,
    const std::string &intent,
    const json &compatibility_payload,
    const ChildProcessResult &result,
    const std::string &message)
{
  const fs::path receipt_path = plan.build_root / "receipt.json";
  const fs::path diagnostics_path = plan.diag_dir / "diagnostics.jsonl";
  return {
      {"category", "CompilerError"},
      {"code", spio::kExitCompiler},
      {"message", message},
      {"command", std::string(command_name)},
      {"child_program", compiler.string()},
      {"child_exit_code", result.exit_code},
      {"mode", "execute"},
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
      {"intent", intent},
      {"locked", workflow_flags.locked},
      {"offline", workflow_flags.offline},
      {"styio", compatibility_payload},
      {"receipt_path", receipt_path.string()},
      {"receipt", ReadJsonArtifactOrNull(receipt_path)},
      {"diagnostics_path", diagnostics_path.string()},
      {"diagnostics", ReadJsonLinesArtifact(diagnostics_path)},
      {"stdout", result.stdout_text},
      {"stderr", result.stderr_text},
  };
}

int EmitWorkflowFailure(const json &payload, bool as_json)
{
  if (as_json)
  {
    std::cerr << payload.dump() << '\n';
    return payload.value("code", spio::kExitCompiler);
  }

  if (payload.contains("stdout") && payload["stdout"].is_string())
  {
    const std::string stdout_text = payload["stdout"].get<std::string>();
    if (!stdout_text.empty())
    {
      std::cout << stdout_text;
    }
  }
  if (payload.contains("stderr") && payload["stderr"].is_string())
  {
    const std::string stderr_text = payload["stderr"].get<std::string>();
    if (!stderr_text.empty())
    {
      std::cerr << stderr_text;
    }
  }

  const std::string category = payload.value("category", "CompilerError");
  const int code = payload.value("code", spio::kExitCompiler);
  const std::string command = payload.value("command", "build");
  const std::string message = payload.value("message", "compiler failed");
  std::cerr << "[" << category << ":" << code << "] " << command << ": " << message << '\n';

  if (payload.contains("diagnostics_path") && payload["diagnostics_path"].is_string())
  {
    std::cerr << "diagnostics: " << payload["diagnostics_path"].get<std::string>() << '\n';
  }
  if (payload.contains("receipt") && !payload["receipt"].is_null() &&
      payload.contains("receipt_path") && payload["receipt_path"].is_string())
  {
    std::cerr << "receipt: " << payload["receipt_path"].get<std::string>() << '\n';
  }
  return code;
}

int HandleProjectGraph(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("project-graph");
  }

  fs::path manifest_path = "spio.toml";
  std::optional<std::string> styio_bin;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "project-graph"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--styio-bin requires a value", "project-graph"}, as_json);
      }
      styio_bin = args[index];
    }
    else if (args[index] == "--json")
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for project-graph: " + args[index], "project-graph"}, as_json);
    }
  }

  manifest_path = spio::CanonicalAbsolutePath(manifest_path);
  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", spio::kExitManifest, "manifest not found: " + manifest_path.string(), "project-graph"}, as_json);
  }

  const fs::path workspace_root = spio::CanonicalAbsolutePath(manifest_path.parent_path());
  const fs::path lockfile_path = spio::CanonicalAbsolutePath(workspace_root / "spio.lock");
  const fs::path vendor_root = spio::ProjectVendorRootForManifest(manifest_path);
  const fs::path build_root = spio::ProjectStateRootForManifest(manifest_path) / "build";
  const fs::path styio_toml = spio::CanonicalAbsolutePath(workspace_root / "styio.toml");
  const fs::path dot_styio_toml = spio::CanonicalAbsolutePath(workspace_root / ".styio.toml");

  spio::ManifestDocument root_manifest;
  try
  {
    root_manifest = spio::LoadManifest(manifest_path);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "project-graph"}, as_json);
  }

  json packages = json::array();
  json dependencies = json::array();
  json targets = json::array();
  std::vector<json> package_distribution_packages;
  std::map<std::string, RegistrySourceAggregate> registry_sources;
  size_t declared_git_dependencies = 0U;
  size_t declared_registry_dependencies = 0U;
  std::set<std::string> editor_files;
  std::vector<std::string> workspace_members;
  std::vector<std::string> notes;

  const auto append_package = [&](const fs::path &package_manifest_path, bool is_workspace_member) {
    const spio::ManifestDocument manifest = spio::LoadManifest(package_manifest_path);
    if (!manifest.package.has_value())
    {
      return;
    }

    const spio::PackageConfig &package = *manifest.package;
    const fs::path package_root = spio::CanonicalAbsolutePath(package_manifest_path.parent_path());
    json package_targets = json::array();
    json package_dependencies = json::array();

    if (package.lib.has_value())
    {
      const json target = BuildTargetPayload(package.name, package_root, "lib", package.name, package.lib->path);
      package_targets.push_back(target);
      targets.push_back(target);
      editor_files.insert(target.at("file_path").get<std::string>());
    }
    for (const spio::BinTarget &bin : package.bins)
    {
      const json target = BuildTargetPayload(package.name, package_root, "bin", bin.name, bin.path);
      package_targets.push_back(target);
      targets.push_back(target);
      editor_files.insert(target.at("file_path").get<std::string>());
    }
    for (const spio::TestTarget &test : package.tests)
    {
      const json target = BuildTargetPayload(package.name, package_root, "test", test.name, test.path);
      package_targets.push_back(target);
      targets.push_back(target);
      editor_files.insert(target.at("file_path").get<std::string>());
    }

    for (const spio::Dependency &dependency : package.dependencies)
    {
      if (dependency.source_kind == spio::DependencySourceKind::kGit)
      {
        ++declared_git_dependencies;
      }
      else if (dependency.source_kind == spio::DependencySourceKind::kRegistry)
      {
        ++declared_registry_dependencies;
      }
      const json dep = BuildDependencyPayload(package.name, dependency, false);
      package_dependencies.push_back(dep);
      dependencies.push_back(dep);
    }
    for (const spio::Dependency &dependency : package.dev_dependencies)
    {
      if (dependency.source_kind == spio::DependencySourceKind::kGit)
      {
        ++declared_git_dependencies;
      }
      else if (dependency.source_kind == spio::DependencySourceKind::kRegistry)
      {
        ++declared_registry_dependencies;
      }
      const json dep = BuildDependencyPayload(package.name, dependency, true);
      package_dependencies.push_back(dep);
      dependencies.push_back(dep);
    }

    packages.push_back({
        {"package_name", package.name},
        {"version", package.version},
        {"root_path", package_root.string()},
        {"manifest_path", package_manifest_path.string()},
        {"targets", package_targets},
        {"dependencies", package_dependencies},
        {"is_workspace_member", is_workspace_member},
        {"publish_enabled", package.publish},
    });
    package_distribution_packages.push_back(
        BuildPackageDistributionEntryPayload(
            package_manifest_path,
            package,
            registry_sources));
  };

  try
  {
    append_package(manifest_path, false);
    if (root_manifest.workspace.has_value())
    {
      workspace_members = root_manifest.workspace->members;
      for (const std::string &member : root_manifest.workspace->members)
      {
        const fs::path member_manifest_path = spio::CanonicalAbsolutePath(workspace_root / member / "spio.toml");
        if (!fs::exists(member_manifest_path))
        {
          throw spio::WorkspaceError("workspace member manifest not found: " + member_manifest_path.string());
        }
        append_package(member_manifest_path, true);
      }
    }
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "project-graph"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "project-graph"}, as_json);
  }

  std::string lock_state = "missing";
  if (fs::exists(lockfile_path))
  {
    lock_state = "unknown";
    try
    {
      const spio::ResolveOptions resolve_options = BuildResolveOptions(
          manifest_path,
          WorkflowFlags{
              .locked = false,
              .offline = true,
          });
      const spio::LockGenerationResult generated = spio::ResolveSingleVersionLockfile(manifest_path, resolve_options);
      lock_state = ReadFile(lockfile_path) == spio::SerializeLockfileCanonical(generated.lockfile) ? "fresh" : "stale";
    }
    catch (const std::exception &err)
    {
      notes.push_back(std::string("lock freshness could not be resolved offline: ") + err.what());
    }
  }

  const std::optional<std::string> manifest_channel =
      root_manifest.package.has_value() ? std::optional<std::string>(root_manifest.package->toolchain.channel) : std::nullopt;
  ToolchainPreview toolchain = BuildToolchainPreview(std::optional<fs::path>(manifest_path), styio_bin, manifest_channel);
  json active_compiler = nullptr;
  if (toolchain.candidate_binary.has_value())
  {
    try
    {
      const spio::CompatibilityReport report = spio::CheckCompilerCompatibility(*toolchain.candidate_binary);
      active_compiler = BuildCompilerHandshakePayload(*toolchain.candidate_binary, report);
      if (!toolchain.channel.has_value())
      {
        toolchain.channel = report.compiler_channel;
      }
      if (!toolchain.version.has_value())
      {
        toolchain.version = report.compiler_version;
      }
    }
    catch (const std::exception &err)
    {
      notes.push_back(std::string("active compiler compatibility check failed: ") + err.what());
    }
  }
  else if (toolchain.source != "unavailable")
  {
    notes.push_back(std::string("active compiler is unresolved: ") + toolchain.detail);
  }

  std::vector<std::string> editor_file_list(editor_files.begin(), editor_files.end());
  if (editor_file_list.empty())
  {
    editor_file_list.push_back(spio::CanonicalAbsolutePath(workspace_root / "src" / "main.styio").string());
  }

  const bool has_root_package = root_manifest.package.has_value();
  const bool has_workspace = root_manifest.workspace.has_value() && !root_manifest.workspace->members.empty();
  const std::string project_kind = has_workspace ? (has_root_package ? "combined-root" : "workspace") : "package";
  const std::string title = has_root_package ? root_manifest.package->name : "Workspace Project";

  return EmitSuccess(
      {
          {"command", "project-graph"},
          {"schema_version", 1},
          {"id", manifest_path.string()},
          {"title", title},
          {"kind", project_kind},
          {"workspace_root", workspace_root.string()},
          {"workspace_members", workspace_members},
          {"manifest_path", manifest_path.string()},
          {"lockfile_path", lockfile_path.string()},
          {"toolchain_pin_path", toolchain.pin_path.has_value() ? json(toolchain.pin_path->string()) : json(nullptr)},
          {"styio_config_path",
           fs::exists(styio_toml) ? json(styio_toml.string())
                                  : (fs::exists(dot_styio_toml) ? json(dot_styio_toml.string()) : json(nullptr))},
          {"vendor_root", vendor_root.string()},
          {"build_root", build_root.string()},
          {"packages", packages},
          {"dependencies", dependencies},
          {"targets", targets},
          {"editor_files", editor_file_list},
          {"toolchain", {
                            {"source", toolchain.source},
                            {"detail", toolchain.detail},
                            {"pin_path", toolchain.pin_path.has_value() ? json(toolchain.pin_path->string()) : json(nullptr)},
                            {"channel", toolchain.channel.has_value() ? json(*toolchain.channel) : json(nullptr)},
                            {"version", toolchain.version.has_value() ? json(*toolchain.version) : json(nullptr)},
                        }},
          {"lock_state", lock_state},
          {"vendor_state", fs::exists(vendor_root) ? "present" : "missing"},
          {"active_compiler", active_compiler},
          {"managed_toolchains", BuildManagedToolchainsPayload()},
          {"package_distribution", BuildPackageDistributionPayload(package_distribution_packages, registry_sources)},
          {"source_state", BuildSourceStatePayload(vendor_root, declared_git_dependencies, declared_registry_dependencies)},
          {"notes", notes},
      },
      as_json);
}

int HandleToolStatus(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    std::cout << "usage: spio tool status [--manifest-path <path>] [--styio-bin <path>] [--json]\n";
    return spio::kExitSuccess;
  }

  std::optional<fs::path> manifest_path;
  std::optional<fs::path> workspace_root;
  std::optional<std::string> manifest_channel;
  std::optional<std::string> styio_bin;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "tool status"}, as_json);
      }
      manifest_path = spio::CanonicalAbsolutePath(args[index]);
    }
    else if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--styio-bin requires a value", "tool status"}, as_json);
      }
      styio_bin = args[index];
    }
    else if (args[index] == "--json")
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for tool status: " + args[index], "tool status"}, as_json);
    }
  }

  if (manifest_path.has_value())
  {
    if (!fs::exists(*manifest_path))
    {
      return EmitError({"ManifestError", spio::kExitManifest, "manifest not found: " + manifest_path->string(), "tool status"}, as_json);
    }

    workspace_root = spio::CanonicalAbsolutePath(manifest_path->parent_path());
    try
    {
      const spio::ManifestDocument manifest = spio::LoadManifest(*manifest_path);
      if (manifest.package.has_value())
      {
        manifest_channel = manifest.package->toolchain.channel;
      }
    }
    catch (const spio::ValidationError &err)
    {
      return EmitError({"ManifestError", spio::kExitManifest, err.what(), "tool status"}, as_json);
    }
    catch (const spio::WorkspaceError &err)
    {
      return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "tool status"}, as_json);
    }
  }

  ToolchainPreview toolchain = BuildToolchainPreview(manifest_path, styio_bin, manifest_channel);
  json project_pin = BuildProjectPinStatePayload(manifest_path);
  json managed_toolchains = BuildManagedToolchainsPayload();

  std::vector<std::string> notes;
  std::optional<std::string> active_compiler_error;
  json active_compiler = BuildOptionalCompilerPayload(toolchain.candidate_binary, active_compiler_error);
  if (active_compiler_error.has_value())
  {
    notes.push_back(std::string("active compiler compatibility check failed: ") + *active_compiler_error);
  }
  else if (!toolchain.candidate_binary.has_value() && toolchain.source != "unavailable")
  {
    notes.push_back(std::string("active compiler is unresolved: ") + toolchain.detail);
  }

  std::optional<fs::path> current_binary;
  if (const std::optional<fs::path> spio_home = spio::ResolveOptionalSpioHome(); spio_home.has_value())
  {
    const fs::path candidate = spio::ManagedStyioBinaryPath(spio::ManagedStyioCurrentRoot(*spio_home));
    if (fs::exists(candidate))
    {
      current_binary = candidate;
    }
  }

  std::optional<std::string> current_compiler_error;
  json current_compiler = BuildOptionalCompilerPayload(current_binary, current_compiler_error);
  if (current_compiler_error.has_value())
  {
    notes.push_back(std::string("managed current compiler compatibility check failed: ") + *current_compiler_error);
  }

  return EmitSuccess(
      {
          {"command", "tool status"},
          {"schema_version", 1},
          {"message", "resolved spio toolchain environment state"},
          {"manifest_path", manifest_path.has_value() ? json(manifest_path->string()) : json(nullptr)},
          {"workspace_root", workspace_root.has_value() ? json(workspace_root->string()) : json(nullptr)},
          {"toolchain", {
                            {"source", toolchain.source},
                            {"detail", toolchain.detail},
                            {"pin_path", toolchain.pin_path.has_value() ? json(toolchain.pin_path->string()) : json(nullptr)},
                            {"channel", toolchain.channel.has_value() ? json(*toolchain.channel) : json(nullptr)},
                            {"version", toolchain.version.has_value() ? json(*toolchain.version) : json(nullptr)},
                            {"candidate_binary_path",
                             toolchain.candidate_binary.has_value() ? json(toolchain.candidate_binary->string()) : json(nullptr)},
                        }},
          {"project_pin", project_pin},
          {"active_compiler", active_compiler},
          {"active_compiler_error", active_compiler_error.has_value() ? json(*active_compiler_error) : json(nullptr)},
          {"current_compiler", current_compiler},
          {"current_compiler_error", current_compiler_error.has_value() ? json(*current_compiler_error) : json(nullptr)},
          {"managed_toolchains", managed_toolchains},
          {"notes", notes},
      },
      as_json);
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
  WorkflowFlags workflow_flags;
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
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
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

  const spio::ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
  if (const auto lock_policy_error = ValidateLockedPolicy(manifest_path, "check", workflow_flags, resolve_options); lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }

  const fs::path lockfile_path = manifest_path.parent_path() / "spio.lock";
  spio::LockGenerationResult generated;
  try
  {
    generated = spio::ResolveSingleVersionLockfile(manifest_path, resolve_options);
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
  std::optional<fs::path> compiler;
  try
  {
    compiler = spio::ResolveStyioBinary(styio_bin, manifest_path);
  }
  catch (const spio::ToolError &err)
  {
    return EmitError({"ToolError", spio::kExitToolInstall, err.what(), "check"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "check"}, as_json);
  }

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
    catch (const spio::ToolError &err)
    {
      return EmitError({"ToolError", spio::kExitToolInstall, err.what(), "check"}, as_json);
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
  WorkflowFlags workflow_flags;
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
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for " + std::string(command_name) + ": " + args[index], std::string(command_name)}, as_json);
    }
  }

  const spio::ResolveOptions resolve_options = BuildResolveOptions(request.manifest_path, workflow_flags);
  if (const auto lock_policy_error = ValidateLockedPolicy(request.manifest_path, command_name, workflow_flags, resolve_options); lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }
  request.offline = workflow_flags.offline;
  request.vendor_root = resolve_options.vendor_root;

  std::optional<fs::path> compiler;
  json compatibility_payload = nullptr;
  if (!dry_run)
  {
    try
    {
      compiler = spio::ResolveStyioBinary(styio_bin, request.manifest_path);
    }
    catch (const spio::ToolError &err)
    {
      return EmitError({"ToolError", spio::kExitToolInstall, err.what(), std::string(command_name)}, as_json);
    }
    catch (const spio::CacheError &err)
    {
      return EmitError({"CacheError", spio::kExitCache, err.what(), std::string(command_name)}, as_json);
    }

    if (!compiler.has_value())
    {
      return EmitError(
          {"UsageError", spio::kExitUsage, std::string(command_name) + " requires --styio-bin <path>, SPIO_STYIO_BIN, a project toolchain pin, or a managed current compiler unless --dry-run is set", std::string(command_name)},
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
    catch (const spio::ToolError &err)
    {
      return EmitError({"ToolError", spio::kExitToolInstall, err.what(), std::string(command_name)}, as_json);
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
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }

  ChildProcessResult result;
  try
  {
    result = RunChildProcess(*compiler, {"--compile-plan", plan.plan_path.string()});
    if (result.exit_code == 127)
    {
      return EmitError(
          {"CompilerSpawnError", spio::kExitCompilerSpawn, "compiler failed to execute --compile-plan", std::string(command_name)},
          as_json);
    }
    if (result.exit_code != 0)
    {
      const std::string detail = TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text);
      const std::string message =
          "compiler failed for compile-plan " + plan.plan_path.string() + (detail.empty() ? "" : ": " + detail);
      return EmitWorkflowFailure(
          BuildWorkflowFailurePayload(
              command_name,
              *compiler,
              plan,
              workflow_flags,
              request.intent,
              compatibility_payload,
              result,
              message),
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
      BuildWorkflowSuccessPayload(
          command_name,
          plan,
          workflow_flags,
          request.intent,
          compatibility_payload,
          result),
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
    else if (args[index] == "--registry")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--registry requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", spio::kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_registry = true;
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
    else if (args[index] == "--version")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--version requires a value", "add"}, as_json);
      }
      request.version = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for add: " + args[index], "add"}, as_json);
    }
  }

  if (!saw_source)
  {
    return EmitError(
        {"UsageError", spio::kExitUsage, "add requires --path <path>, --git <source> --rev <rev>, or --registry <url> --version <x.y.z>", "add"},
        as_json);
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
            {"source_kind", request.use_git ? "git" : (request.use_registry ? "registry" : "path")},
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
  WorkflowFlags workflow_flags;
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
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for fetch: " + args[index], "fetch"}, as_json);
    }
  }

  const spio::ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
  if (const auto lock_policy_error = ValidateLockedPolicy(manifest_path, "fetch", workflow_flags, resolve_options); lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }

  try
  {
    const spio::FetchCommandResult result = spio::FetchDependencies(manifest_path, resolve_options);
    return EmitSuccess(
        {
            {"command", "fetch"},
            {"message", "fetched dependency sources for " + std::to_string(result.package_count) + " package(s)"},
            {"manifest_path", result.manifest_path.string()},
            {"packages", result.package_count},
            {"git_packages", result.git_package_count},
            {"registry_packages", result.registry_package_count},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
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
  WorkflowFlags workflow_flags;
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
    else if (args[index] == "--offline")
    {
      workflow_flags.offline = true;
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
  const spio::ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
  try
  {
    generated = spio::ResolveSingleVersionLockfile(manifest_path, resolve_options);
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
            {"offline", workflow_flags.offline},
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
          {"offline", workflow_flags.offline},
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
      if (package.registry.has_value())
      {
        item["registry"] = *package.registry;
      }
      if (package.sha256.has_value())
      {
        item["sha256"] = *package.sha256;
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

int HandleVendor(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("vendor");
  }

  spio::VendorRequest request;
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "vendor"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--output requires a value", "vendor"}, as_json);
      }
      request.output_path = fs::path(args[index]);
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for vendor: " + args[index], "vendor"}, as_json);
    }
  }

  const spio::ResolveOptions resolve_options = BuildResolveOptions(request.manifest_path, workflow_flags, request.output_path);
  if (const auto lock_policy_error = ValidateLockedPolicy(request.manifest_path, "vendor", workflow_flags, resolve_options); lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }
  request.offline = workflow_flags.offline;

  try
  {
    const spio::VendorResult result = spio::WriteVendorTree(request);
    return EmitSuccess(
        {
            {"command", "vendor"},
            {"message", "materialized vendored dependency snapshots under " + result.vendor_root.string()},
            {"manifest_path", result.manifest_path.string()},
            {"vendor_root", result.vendor_root.string()},
            {"metadata_path", result.metadata_path.string()},
            {"packages", result.package_count},
            {"git_snapshots", result.git_snapshot_count},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "vendor"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "vendor"}, as_json);
  }
  catch (const spio::ResolutionError &err)
  {
    return EmitError({"ResolutionError", spio::kExitResolve, err.what(), "vendor"}, as_json);
  }
  catch (const spio::FetchError &err)
  {
    return EmitError({"FetchError", spio::kExitFetch, err.what(), "vendor"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "vendor"}, as_json);
  }
  catch (const spio::VendorError &err)
  {
    return EmitError({"VendorError", spio::kExitVendor, err.what(), "vendor"}, as_json);
  }
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

int HandlePublish(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("publish");
  }

  spio::PublishRequest request;
  std::optional<std::string> registry_root;
  std::optional<std::string> registry_profile;
  std::optional<fs::path> registry_policy_file;
  std::vector<std::string> registry_headers;
  bool dry_run = false;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "publish"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--package")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--package requires a value", "publish"}, as_json);
      }
      request.package_name = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--output requires a value", "publish"}, as_json);
      }
      request.output_path = args[index];
    }
    else if (args[index] == "--registry")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--registry requires a value", "publish"}, as_json);
      }
      registry_root = args[index];
    }
    else if (args[index] == "--registry-profile")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--registry-profile requires a value", "publish"}, as_json);
      }
      registry_profile = args[index];
    }
    else if (args[index] == "--registry-policy-file")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--registry-policy-file requires a value", "publish"}, as_json);
      }
      registry_policy_file = args[index];
    }
    else if (args[index] == "--registry-header")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--registry-header requires a value", "publish"}, as_json);
      }
      const std::optional<std::string> normalized = NormalizeRegistryHeader(args[index]);
      if (!normalized.has_value())
      {
        return EmitError(
            {"UsageError", spio::kExitUsage, "--registry-header must match <name:value> with non-empty name and value", "publish"},
            as_json);
      }
      registry_headers.push_back(*normalized);
    }
    else if (args[index] == "--dry-run")
    {
      dry_run = true;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for publish: " + args[index], "publish"}, as_json);
    }
  }

  if (dry_run && !registry_headers.empty())
  {
    return EmitError(
        {"UsageError", spio::kExitUsage, "--registry-header is not valid with --dry-run", "publish"},
        as_json);
  }
  if (dry_run && registry_policy_file.has_value())
  {
    return EmitError(
        {"UsageError", spio::kExitUsage, "--registry-policy-file is not valid with --dry-run", "publish"},
        as_json);
  }
  if (dry_run && registry_profile.has_value())
  {
    return EmitError(
        {"UsageError", spio::kExitUsage, "--registry-profile is not valid with --dry-run", "publish"},
        as_json);
  }
  if (registry_profile.has_value() && registry_policy_file.has_value())
  {
    return EmitError(
        {"UsageError", spio::kExitUsage, "--registry-profile cannot be combined with --registry-policy-file", "publish"},
        as_json);
  }

  if (!dry_run)
  {
    if (!registry_root.has_value())
    {
      return EmitError(
          {"UsageError", spio::kExitUsage, "publish requires --registry <path-or-url> unless --dry-run is set", "publish"},
          as_json);
    }

    try
    {
      if (IsHttpRegistryRoot(*registry_root))
      {
        const spio::RegistryWriteSecurityDecision security = spio::ResolveRegistryWriteSecurity({
            .registry_root = *registry_root,
            .profile_name = registry_profile,
            .policy_file = registry_policy_file,
            .explicit_request_headers = registry_headers,
        });
        const spio::HttpRegistryPublishResult result = spio::PublishToHttpRegistry({
            .publish_request = request,
            .registry_root = security.registry_root,
            .request_headers = security.request_headers,
        });
        json payload = {
            {"command", "publish"},
            {"mode", "publish"},
            {"transport", "http"},
            {"message", "published package into remote registry: " + result.registry_entry_url},
            {"manifest_path", result.candidate.manifest_path.string()},
            {"package_root", result.candidate.package_root.string()},
            {"archive_path", result.candidate.archive_path.string()},
            {"package", result.candidate.package_name},
            {"version", result.candidate.package_version},
            {"dependencies", result.candidate.dependency_count},
            {"dev_dependencies", result.candidate.dev_dependency_count},
            {"registry_root", result.registry_root},
            {"registry_marker_url", result.registry_marker_url},
            {"registry_blob_url", result.registry_blob_url},
            {"registry_entry_url", result.registry_entry_url},
            {"registry_security_provider", security.provider_name},
            {"registry_write_security_mode", security.mode},
            {"registry_header_count", security.request_headers.size()},
            {"sha256", result.archive_sha256},
            {"size_bytes", result.archive_size_bytes},
            {"published_at", result.published_at_utc},
        };
        if (security.profile_name.has_value())
        {
          payload["registry_profile"] = *security.profile_name;
        }
        return EmitSuccess(payload, as_json);
      }

      const fs::path filesystem_registry_root =
          IsFileRegistryRoot(*registry_root) ? FileRegistryUrlToPath(*registry_root) : fs::path(*registry_root);
      if (registry_policy_file.has_value())
      {
        return EmitError(
            {
                "UsageError",
                spio::kExitUsage,
                "--registry-policy-file is only valid for http:// or https:// registry roots",
                "publish",
            },
            as_json);
      }
      if (registry_profile.has_value())
      {
        return EmitError(
            {
                "UsageError",
                spio::kExitUsage,
                "--registry-profile is only valid for http:// or https:// registry roots",
                "publish",
            },
            as_json);
      }
      if (!registry_headers.empty())
      {
        return EmitError(
            {"UsageError", spio::kExitUsage, "--registry-header is only valid for http:// or https:// registry roots", "publish"},
            as_json);
      }
      const spio::RegistryPublishResult result = spio::PublishToFilesystemRegistry({
          .publish_request = request,
          .registry_root = filesystem_registry_root,
      });
      return EmitSuccess(
          {
              {"command", "publish"},
              {"mode", "publish"},
              {"transport", "filesystem"},
              {"message", "published package into local filesystem registry: " + result.registry_entry_path.string()},
              {"manifest_path", result.candidate.manifest_path.string()},
              {"package_root", result.candidate.package_root.string()},
              {"archive_path", result.candidate.archive_path.string()},
              {"package", result.candidate.package_name},
              {"version", result.candidate.package_version},
              {"dependencies", result.candidate.dependency_count},
              {"dev_dependencies", result.candidate.dev_dependency_count},
              {"registry_root", result.registry_root.string()},
              {"registry_marker_path", result.registry_marker_path.string()},
              {"registry_blob_path", result.registry_blob_path.string()},
              {"registry_entry_path", result.registry_entry_path.string()},
              {"sha256", result.archive_sha256},
              {"size_bytes", result.archive_size_bytes},
              {"published_at", result.published_at_utc},
          },
          as_json);
    }
    catch (const spio::ValidationError &err)
    {
      return EmitError({"ManifestError", spio::kExitManifest, err.what(), "publish"}, as_json);
    }
    catch (const spio::WorkspaceError &err)
    {
      return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "publish"}, as_json);
    }
    catch (const spio::PackError &err)
    {
      return EmitError({"PackError", spio::kExitPack, err.what(), "publish"}, as_json);
    }
    catch (const spio::PublishError &err)
    {
      return EmitError({"PublishError", spio::kExitPublish, err.what(), "publish"}, as_json);
    }
  }

  try
  {
    const spio::PublishResult result = spio::PreparePublishCandidate(request);
    return EmitSuccess(
        {
            {"command", "publish"},
            {"mode", "dry-run"},
            {"message", "prepared publish candidate: " + result.archive_path.string()},
            {"manifest_path", result.manifest_path.string()},
            {"package_root", result.package_root.string()},
            {"archive_path", result.archive_path.string()},
            {"package", result.package_name},
            {"version", result.package_version},
            {"dependencies", result.dependency_count},
            {"dev_dependencies", result.dev_dependency_count},
        },
        as_json);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "publish"}, as_json);
  }
  catch (const spio::WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", spio::kExitWorkspace, err.what(), "publish"}, as_json);
  }
  catch (const spio::PackError &err)
  {
    return EmitError({"PackError", spio::kExitPack, err.what(), "publish"}, as_json);
  }
  catch (const spio::PublishError &err)
  {
    return EmitError({"PublishError", spio::kExitPublish, err.what(), "publish"}, as_json);
  }
}

int HandleToolInstall(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    std::cout << "usage: spio tool install --styio-bin <path>\n";
    return spio::kExitSuccess;
  }

  std::optional<fs::path> styio_binary;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--styio-bin requires a value", "tool install"}, as_json);
      }
      styio_binary = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for tool install: " + args[index], "tool install"}, as_json);
    }
  }

  if (!styio_binary.has_value())
  {
    return EmitError({"UsageError", spio::kExitUsage, "tool install requires --styio-bin <path>", "tool install"}, as_json);
  }

  try
  {
    const spio::ToolInstallResult result = spio::InstallManagedStyio({.styio_binary = *styio_binary});
    return EmitSuccess(
        {
            {"command", "tool install"},
            {"message", "installed managed styio compiler: " + result.managed_binary_path.string()},
            {"source_binary", result.source_binary.string()},
            {"spio_home", result.spio_home.string()},
            {"install_root", result.install_root.string()},
            {"install_binary_path", result.install_binary_path.string()},
            {"install_metadata_path", result.install_metadata_path.string()},
            {"current_root", result.current_root.string()},
            {"managed_binary_path", result.managed_binary_path.string()},
            {"current_metadata_path", result.current_metadata_path.string()},
            {"compiler_version", result.compiler_version},
            {"channel", result.compiler_channel},
            {"edition_max", result.compiler_edition_max},
            {"integration_phase", result.integration_phase},
            {"supported_compile_plan_versions", result.supported_compile_plan_versions},
            {"capabilities", result.capabilities},
        },
        as_json);
  }
  catch (const spio::ToolError &err)
  {
    return EmitError({"ToolError", spio::kExitToolInstall, err.what(), "tool install"}, as_json);
  }
  catch (const spio::CompilerProbeError &err)
  {
    return EmitError({"CompilerSpawnError", spio::kExitCompilerSpawn, err.what(), "tool install"}, as_json);
  }
  catch (const spio::CompatibilityError &err)
  {
    return EmitError({"ContractError", spio::kExitContract, err.what(), "tool install"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "tool install"}, as_json);
  }
}

int HandleToolUse(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    std::cout << "usage: spio tool use --version <compiler-version> [--channel <channel>]\n";
    return spio::kExitSuccess;
  }

  std::optional<std::string> compiler_version;
  std::optional<std::string> compiler_channel;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--version")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--version requires a value", "tool use"}, as_json);
      }
      compiler_version = args[index];
    }
    else if (args[index] == "--channel")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--channel requires a value", "tool use"}, as_json);
      }
      compiler_channel = args[index];
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for tool use: " + args[index], "tool use"}, as_json);
    }
  }

  if (!compiler_version.has_value())
  {
    return EmitError({"UsageError", spio::kExitUsage, "tool use requires --version <compiler-version>", "tool use"}, as_json);
  }

  try
  {
    const spio::ToolUseResult result = spio::UseManagedStyio({
        .compiler_version = *compiler_version,
        .compiler_channel = compiler_channel,
    });
    return EmitSuccess(
        {
            {"command", "tool use"},
            {"message", "activated managed styio compiler: " + result.managed_binary_path.string()},
            {"spio_home", result.spio_home.string()},
            {"install_root", result.install_root.string()},
            {"install_binary_path", result.install_binary_path.string()},
            {"install_metadata_path", result.install_metadata_path.string()},
            {"current_root", result.current_root.string()},
            {"managed_binary_path", result.managed_binary_path.string()},
            {"current_metadata_path", result.current_metadata_path.string()},
            {"compiler_version", result.compiler_version},
            {"channel", result.compiler_channel},
            {"edition_max", result.compiler_edition_max},
            {"integration_phase", result.integration_phase},
            {"supported_compile_plan_versions", result.supported_compile_plan_versions},
            {"capabilities", result.capabilities},
        },
        as_json);
  }
  catch (const spio::ToolError &err)
  {
    return EmitError({"ToolError", spio::kExitToolInstall, err.what(), "tool use"}, as_json);
  }
  catch (const spio::CompilerProbeError &err)
  {
    return EmitError({"CompilerSpawnError", spio::kExitCompilerSpawn, err.what(), "tool use"}, as_json);
  }
  catch (const spio::CompatibilityError &err)
  {
    return EmitError({"ContractError", spio::kExitContract, err.what(), "tool use"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "tool use"}, as_json);
  }
}

int HandleToolPin(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    std::cout << "usage: spio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]\n";
    return spio::kExitSuccess;
  }

  spio::ToolPinRequest request;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--manifest-path requires a value", "tool pin"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--version")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--version requires a value", "tool pin"}, as_json);
      }
      request.compiler_version = args[index];
    }
    else if (args[index] == "--channel")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", spio::kExitUsage, "--channel requires a value", "tool pin"}, as_json);
      }
      request.compiler_channel = args[index];
    }
    else if (args[index] == "--clear")
    {
      request.clear = true;
    }
    else
    {
      return EmitError({"UsageError", spio::kExitUsage, "unexpected argument for tool pin: " + args[index], "tool pin"}, as_json);
    }
  }

  if (request.clear && request.compiler_version.has_value())
  {
    return EmitError({"UsageError", spio::kExitUsage, "tool pin cannot combine --clear and --version", "tool pin"}, as_json);
  }
  if (request.clear && request.compiler_channel.has_value())
  {
    return EmitError({"UsageError", spio::kExitUsage, "tool pin cannot combine --clear and --channel", "tool pin"}, as_json);
  }
  if (!request.clear && !request.compiler_version.has_value())
  {
    return EmitError({"UsageError", spio::kExitUsage, "tool pin requires --version <compiler-version> unless --clear is set", "tool pin"}, as_json);
  }

  try
  {
    const spio::ToolPinResult result = spio::PinManagedStyio(request);
    if (result.cleared)
    {
      return EmitSuccess(
          {
              {"command", "tool pin"},
              {"message", "cleared project toolchain pin: " + result.pin_path.string()},
              {"manifest_path", result.manifest_path.string()},
              {"pin_path", result.pin_path.string()},
              {"mode", "clear"},
          },
          as_json);
    }

    return EmitSuccess(
        {
            {"command", "tool pin"},
            {"message", "pinned project compiler to managed styio " + *result.compiler_channel + "/" + *result.compiler_version},
            {"manifest_path", result.manifest_path.string()},
            {"pin_path", result.pin_path.string()},
            {"mode", "write"},
            {"compiler_version", *result.compiler_version},
            {"channel", *result.compiler_channel},
            {"install_root", result.install_root->string()},
            {"install_binary_path", result.install_binary_path->string()},
        },
        as_json);
  }
  catch (const spio::ValidationError &err)
  {
    return EmitError({"ManifestError", spio::kExitManifest, err.what(), "tool pin"}, as_json);
  }
  catch (const spio::ToolError &err)
  {
    return EmitError({"ToolError", spio::kExitToolInstall, err.what(), "tool pin"}, as_json);
  }
  catch (const spio::CompilerProbeError &err)
  {
    return EmitError({"CompilerSpawnError", spio::kExitCompilerSpawn, err.what(), "tool pin"}, as_json);
  }
  catch (const spio::CompatibilityError &err)
  {
    return EmitError({"ContractError", spio::kExitContract, err.what(), "tool pin"}, as_json);
  }
  catch (const spio::CacheError &err)
  {
    return EmitError({"CacheError", spio::kExitCache, err.what(), "tool pin"}, as_json);
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
  if (command == "project-graph")
  {
    return HandleProjectGraph(args, global_json);
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
  if (command == "vendor")
  {
    return HandleVendor(args, global_json);
  }
  if (command == "pack")
  {
    return HandlePack(args, global_json);
  }
  if (command == "publish")
  {
    return HandlePublish(args, global_json);
  }
  if (command == "tool")
  {
    if (args.size() == 1 && args.front() == "--help")
    {
      return PrintCommandUsage("tool");
    }
    if (!args.empty() && args.front() == "install")
    {
      return HandleToolInstall(
          std::vector<std::string>(args.begin() + 1, args.end()),
          global_json);
    }
    if (!args.empty() && args.front() == "status")
    {
      return HandleToolStatus(
          std::vector<std::string>(args.begin() + 1, args.end()),
          global_json);
    }
    if (!args.empty() && args.front() == "use")
    {
      return HandleToolUse(
          std::vector<std::string>(args.begin() + 1, args.end()),
          global_json);
    }
    if (!args.empty() && args.front() == "pin")
    {
      return HandleToolPin(
          std::vector<std::string>(args.begin() + 1, args.end()),
          global_json);
    }
    return EmitError({"UsageError", kExitUsage, "tool requires the 'status', 'install', 'use', or 'pin' subcommand", "tool"}, global_json);
  }
  return EmitError({"UsageError", kExitUsage, "unknown command: " + command, command}, global_json);
}

}  // namespace spio
