#include "PafioApp/ToolApp.hpp"

#include "PafioCLI/Support.hpp"
#include "PafioCloud/Contract.hpp"
#include "PafioCloud/Execution.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioTool/Contract.hpp"
#include "PafioTool/Install.hpp"
#include "PafioToolchain/SourceBuild.hpp"
#include "PafioToolchain/State.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <cstdlib>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace pafio
{

namespace
{

enum class ToolchainModeSelection
{
  Binary,
  Build,
};

enum class SetSubject
{
  Channel,
  Build,
  Risk,
  Lane,
  Security,
};

struct NamedToolchainModeSelection
{
  std::string_view keyword;
  ToolchainModeSelection selection;
};

struct NamedSetSubject
{
  std::string_view keyword;
  SetSubject selection;
};

constexpr std::array kToolchainModeSelections = {
    NamedToolchainModeSelection{"binary", ToolchainModeSelection::Binary},
    NamedToolchainModeSelection{"build", ToolchainModeSelection::Build},
};

// Keep CLI spelling, payload wording, and state writes aligned in one table.
constexpr std::array kSetSubjects = {
    NamedSetSubject{"channel", SetSubject::Channel},
    NamedSetSubject{"build", SetSubject::Build},
    NamedSetSubject{"risk", SetSubject::Risk},
    NamedSetSubject{"lane", SetSubject::Lane},
    NamedSetSubject{"security", SetSubject::Security},
};

template <typename NamedEntry, size_t N, typename Enum>
std::optional<Enum> ParseNamedEnum(std::string_view raw, const std::array<NamedEntry, N> &entries)
{
  const std::string normalized = NormalizeSetKeyword(std::string(raw));
  const auto it = std::find_if(
      entries.begin(),
      entries.end(),
      [&normalized](const NamedEntry &entry) {
        return entry.keyword == normalized;
      });
  if (it == entries.end())
  {
    return std::nullopt;
  }
  return it->selection;
}

std::optional<ToolchainModeSelection> ParseToolchainModeSelection(std::string_view raw)
{
  return ParseNamedEnum<NamedToolchainModeSelection, kToolchainModeSelections.size(), ToolchainModeSelection>(
      raw,
      kToolchainModeSelections);
}

std::string_view ToolchainModeSelectionName(ToolchainModeSelection selection)
{
  switch (selection)
  {
    case ToolchainModeSelection::Binary:
      return "binary";
    case ToolchainModeSelection::Build:
      return "build";
  }
  return "binary";
}

std::optional<SetSubject> ParseSetSubject(std::string_view raw)
{
  return ParseNamedEnum<NamedSetSubject, kSetSubjects.size(), SetSubject>(raw, kSetSubjects);
}

std::string_view SetSubjectName(SetSubject subject)
{
  switch (subject)
  {
    case SetSubject::Channel:
      return "channel";
    case SetSubject::Build:
      return "build";
    case SetSubject::Risk:
      return "risk";
    case SetSubject::Lane:
      return "lane";
    case SetSubject::Security:
      return "security";
  }
  return "channel";
}

json BuildProjectToolchainStatePayload(
    std::string_view command,
    std::string message,
    const ProjectToolchainState &state,
    const CloudExecutionPolicy &cloud_policy)
{
  json payload = {
      {"command", command},
      {"message", std::move(message)},
      {"manifest_path", state.manifest_path.string()},
      {"toolchain_state_path", state.state_path.string()},
      {"mode", state.mode},
      {"channel", state.channel},
      {"build_mode", state.build_mode},
      {"risk_class", state.risk_class},
      {"execution_lane", state.preferred_execution_lane},
      {"security_profile", state.security_profile},
      {"cloud", SerializeCloudExecutionPolicy(cloud_policy)},
  };
  if (state.source_revision.has_value())
  {
    payload["source_revision"] = *state.source_revision;
  }
  return payload;
}

}  // namespace

int HandleInstall(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("install");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "install requires styio[@latest]", "install"}, as_json);
  }

  const std::string target = args.front();
  const size_t at = target.find('@');
  const std::string tool = at == std::string::npos ? target : target.substr(0, at);
  const std::string requested = at == std::string::npos ? "latest" : target.substr(at + 1);
  if (tool != "styio" || requested.empty())
  {
    return EmitError({"UsageError", kExitUsage, "install supports only styio[@latest]", "install"}, as_json);
  }

  std::optional<fs::path> source_root;
  std::optional<std::string> source_revision;
  std::string channel = std::string(kChannelStable);
  std::string build_mode = std::string(kBuildModeMinimal);
  bool allow_fetch = true;
  bool offline = false;
  bool assume_yes = true;
  bool non_interactive = true;

  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--source-root")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--source-root requires a value", "install"}, as_json);
      }
      source_root = args[index];
    }
    else if (args[index] == "--source-rev")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--source-rev requires a value", "install"}, as_json);
      }
      source_revision = args[index];
    }
    else if (args[index] == "--channel")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--channel requires a value", "install"}, as_json);
      }
      channel = NormalizeSetKeyword(args[index]);
      if (!IsSupportedChannel(channel))
      {
        return EmitError({"UsageError", kExitUsage, "--channel must be stable or nightly", "install"}, as_json);
      }
    }
    else if (args[index] == "--build")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--build requires a value", "install"}, as_json);
      }
      build_mode = NormalizeSetKeyword(args[index]);
      if (!IsSupportedBuildMode(build_mode))
      {
        return EmitError({"UsageError", kExitUsage, "--build must be minimal", "install"}, as_json);
      }
    }
    else if (args[index] == "--yes")
    {
      assume_yes = true;
      non_interactive = true;
    }
    else if (args[index] == "--no-fetch")
    {
      allow_fetch = false;
    }
    else if (args[index] == "--offline")
    {
      offline = true;
      allow_fetch = false;
    }
    else if (args[index] == "--non-interactive")
    {
      non_interactive = true;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for install: " + args[index], "install"}, as_json);
    }
  }

  if (!source_revision.has_value() && requested != "latest")
  {
    source_revision = requested;
  }
  if (!source_revision.has_value())
  {
    if (const char *explicit_ref = std::getenv("PAFIO_STYIO_SOURCE_REF"); explicit_ref != nullptr && explicit_ref[0] != '\0')
    {
      source_revision = explicit_ref;
    }
    else
    {
      source_revision = "main";
    }
  }

  try
  {
    const SourceBuildResult source_result = EnsureSourceBuiltStyio({
        .channel = channel,
        .build_mode = build_mode,
        .explicit_source_root = source_root,
        .source_revision = source_revision,
        .allow_fetch = allow_fetch,
        .offline = offline,
        .assume_yes = assume_yes,
        .non_interactive = non_interactive,
    });
    const ToolInstallResult install = InstallManagedStyio({.styio_binary = source_result.compiler_binary});
    return EmitSuccess(
        {
            {"command", "install"},
            {"message", "installed styio " + requested + ": " + install.managed_binary_path.string()},
            {"package", "styio"},
            {"requested", requested},
            {"channel", install.compiler_channel},
            {"build_mode", source_result.build_mode},
            {"source_root", source_result.source_root.string()},
            {"source_revision", source_result.source_revision},
            {"source_fetched", source_result.fetched},
            {"source_built", source_result.built},
            {"compiler_binary", source_result.compiler_binary.string()},
            {"pafio_home", install.pafio_home.string()},
            {"install_root", install.install_root.string()},
            {"install_binary_path", install.install_binary_path.string()},
            {"managed_binary_path", install.managed_binary_path.string()},
            {"compiler_version", install.compiler_version},
            {"edition_max", install.compiler_edition_max},
            {"integration_phase", install.integration_phase},
            {"supported_compile_plan_versions", install.supported_compile_plan_versions},
            {"capabilities", install.capabilities},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "install"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "install"}, as_json);
  }
  catch (const CompilerProbeError &err)
  {
    return EmitError({"CompilerSpawnError", kExitCompilerSpawn, err.what(), "install"}, as_json);
  }
  catch (const CompatibilityError &err)
  {
    return EmitError({"ContractError", kExitContract, err.what(), "install"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "install"}, as_json);
  }
}

int HandleUse(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("use");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "use requires <binary|build>", "use"}, as_json);
  }

  fs::path manifest_path = "pafio.toml";
  const auto mode = ParseToolchainModeSelection(args.front());
  if (!mode.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "use requires <binary|build>", "use"}, as_json);
  }
  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "use"}, as_json);
      }
      manifest_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for use: " + args[index], "use"}, as_json);
    }
  }

  try
  {
    const ProjectToolchainState state = UpdateProjectToolchainState({
        .manifest_path = manifest_path,
        .mode = std::string(ToolchainModeSelectionName(*mode)),
    });
    const CloudExecutionPolicy cloud_policy = ResolveCloudExecutionPolicy(state);
    return EmitSuccess(
        BuildProjectToolchainStatePayload(
            "use",
            "set project toolchain mode to " + state.mode,
            state,
            cloud_policy),
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "use"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "use"}, as_json);
  }
}

int HandleSet(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("set");
  }
  if (args.size() < 2)
  {
    return EmitError({"UsageError", kExitUsage, "set requires a subject and value", "set"}, as_json);
  }

  const auto subject = ParseSetSubject(args[0]);
  if (!subject.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "set supports only 'channel', 'build', 'risk', 'lane', or 'security'", "set"}, as_json);
  }
  size_t index = 1;
  if (NormalizeSetKeyword(args[index]) == "as")
  {
    ++index;
  }
  if (index >= args.size())
  {
    return EmitError({"UsageError", kExitUsage, "set requires a value after the subject", "set"}, as_json);
  }

  const std::string value = NormalizeSetKeyword(args[index++]);
  fs::path manifest_path = "pafio.toml";
  for (; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "set"}, as_json);
      }
      manifest_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for set: " + args[index], "set"}, as_json);
    }
  }

  ToolchainStateUpdate update;
  update.manifest_path = manifest_path;
  switch (*subject)
  {
    case SetSubject::Channel:
      update.channel = value;
      break;
    case SetSubject::Build:
      update.build_mode = value;
      break;
    case SetSubject::Risk:
      update.risk_class = value;
      break;
    case SetSubject::Lane:
      update.preferred_execution_lane = value;
      break;
    case SetSubject::Security:
      update.security_profile = value;
      break;
  }

  try
  {
    const ProjectToolchainState state = UpdateProjectToolchainState(update);
    const CloudExecutionPolicy cloud_policy = ResolveCloudExecutionPolicy(state);
    return EmitSuccess(
        BuildProjectToolchainStatePayload(
            "set",
            "updated project " + std::string(SetSubjectName(*subject)) + " to " + value,
            state,
            cloud_policy),
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "set"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "set"}, as_json);
  }
}

int HandleToolStatus(const std::vector<std::string> &args, bool as_json)
{
  bool local_json = as_json;
  std::optional<fs::path> manifest_path;
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
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "tool status"}, as_json);
      }
      manifest_path = fs::path(args[index]);
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for tool status: " + args[index], "tool status"}, as_json);
    }
  }
  if (!local_json)
  {
    return EmitError({"UsageError", kExitUsage, "tool status currently requires --json", "tool status"}, as_json);
  }

  try
  {
    std::optional<ProjectToolchainState> project_state;
    std::optional<CloudExecutionPolicy> cloud_policy;
    if (manifest_path.has_value())
    {
      (void) LoadManifest(*manifest_path);
      project_state = LoadProjectToolchainState(*manifest_path);
      cloud_policy = ResolveCloudExecutionPolicy(*project_state);
    }
    const ToolStatusResult result = QueryToolStatus(manifest_path);
    return EmitSuccess(BuildToolStatusPayload(result, project_state, cloud_policy), true);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "tool status"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "tool status"}, as_json);
  }
}

int HandleToolInstall(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    std::cout << "usage: pafio tool install --styio-bin <path>\n";
    return kExitSuccess;
  }

  std::optional<fs::path> styio_binary;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--styio-bin requires a value", "tool install"}, as_json);
      }
      styio_binary = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for tool install: " + args[index], "tool install"}, as_json);
    }
  }

  if (!styio_binary.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "tool install requires --styio-bin <path>", "tool install"}, as_json);
  }

  try
  {
    const ToolInstallResult result = InstallManagedStyio({.styio_binary = *styio_binary});
    return EmitSuccess(
        {
            {"command", "tool install"},
            {"message", "installed managed styio compiler: " + result.managed_binary_path.string()},
            {"source_binary", result.source_binary.string()},
            {"pafio_home", result.pafio_home.string()},
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
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "tool install"}, as_json);
  }
  catch (const CompilerProbeError &err)
  {
    return EmitError({"CompilerSpawnError", kExitCompilerSpawn, err.what(), "tool install"}, as_json);
  }
  catch (const CompatibilityError &err)
  {
    return EmitError({"ContractError", kExitContract, err.what(), "tool install"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "tool install"}, as_json);
  }
}

int HandleToolUse(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    std::cout << "usage: pafio tool use --version <compiler-version> [--channel <channel>]\n";
    return kExitSuccess;
  }

  std::optional<std::string> compiler_version;
  std::optional<std::string> compiler_channel;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--version")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--version requires a value", "tool use"}, as_json);
      }
      compiler_version = args[index];
    }
    else if (args[index] == "--channel")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--channel requires a value", "tool use"}, as_json);
      }
      compiler_channel = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for tool use: " + args[index], "tool use"}, as_json);
    }
  }

  if (!compiler_version.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "tool use requires --version <compiler-version>", "tool use"}, as_json);
  }

  try
  {
    const ToolUseResult result = UseManagedStyio({
        .compiler_version = *compiler_version,
        .compiler_channel = compiler_channel,
    });
    return EmitSuccess(
        {
            {"command", "tool use"},
            {"message", "activated managed styio compiler: " + result.managed_binary_path.string()},
            {"pafio_home", result.pafio_home.string()},
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
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "tool use"}, as_json);
  }
  catch (const CompilerProbeError &err)
  {
    return EmitError({"CompilerSpawnError", kExitCompilerSpawn, err.what(), "tool use"}, as_json);
  }
  catch (const CompatibilityError &err)
  {
    return EmitError({"ContractError", kExitContract, err.what(), "tool use"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "tool use"}, as_json);
  }
}

int HandleToolPin(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    std::cout << "usage: pafio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]\n";
    return kExitSuccess;
  }

  ToolPinRequest request;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "tool pin"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--version")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--version requires a value", "tool pin"}, as_json);
      }
      request.compiler_version = args[index];
    }
    else if (args[index] == "--channel")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--channel requires a value", "tool pin"}, as_json);
      }
      request.compiler_channel = args[index];
    }
    else if (args[index] == "--clear")
    {
      request.clear = true;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for tool pin: " + args[index], "tool pin"}, as_json);
    }
  }

  if (request.clear && request.compiler_version.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "tool pin cannot combine --clear and --version", "tool pin"}, as_json);
  }
  if (request.clear && request.compiler_channel.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "tool pin cannot combine --clear and --channel", "tool pin"}, as_json);
  }
  if (!request.clear && !request.compiler_version.has_value())
  {
    return EmitError({"UsageError", kExitUsage, "tool pin requires --version <compiler-version> unless --clear is set", "tool pin"}, as_json);
  }

  try
  {
    const ToolPinResult result = PinManagedStyio(request);
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
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "tool pin"}, as_json);
  }
  catch (const ToolError &err)
  {
    return EmitError({"ToolError", kExitToolInstall, err.what(), "tool pin"}, as_json);
  }
  catch (const CompilerProbeError &err)
  {
    return EmitError({"CompilerSpawnError", kExitCompilerSpawn, err.what(), "tool pin"}, as_json);
  }
  catch (const CompatibilityError &err)
  {
    return EmitError({"ContractError", kExitContract, err.what(), "tool pin"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "tool pin"}, as_json);
  }
}

}  // namespace pafio
