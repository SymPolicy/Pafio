#include "PafioCLI/Support.hpp"

#include "PafioCore/Paths.hpp"
#include "PafioManifest/Lockfile.hpp"

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

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace pafio
{

namespace
{

struct UsageCommandEntry
{
  std::string_view name;
  std::string_view usage;
};

constexpr std::array kUsageCommands = {
    UsageCommandEntry{
        "machine-info",
        "usage: pafio machine-info [--json]\n",
    },
    UsageCommandEntry{
        "doctor",
        "usage: pafio doctor [--json] [--manifest-path <path>] [--styio-bin <path>]\n",
    },
    UsageCommandEntry{
        "metadata",
        "usage: pafio metadata --json [--manifest-path <path>] [--locked|--offline|--frozen]\n",
    },
    UsageCommandEntry{
        "new",
        "usage: pafio new <package-name> [directory] [--lib|--bin]\n",
    },
    UsageCommandEntry{
        "init",
        "usage: pafio init [--name <package-name>] [--lib|--bin]\n",
    },
    UsageCommandEntry{
        "check",
        "usage: pafio check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n",
    },
    UsageCommandEntry{
        "add",
        "usage: pafio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]\n",
    },
    UsageCommandEntry{
        "remove",
        "usage: pafio remove <alias-or-package> [--dev] [--manifest-path <path>]\n",
    },
    UsageCommandEntry{
        "sync",
        "usage: pafio sync [--manifest-path <path>] [--locked|--offline|--frozen]\n",
    },
    UsageCommandEntry{
        "tree",
        "usage: pafio tree [--manifest-path <path>]\n",
    },
    UsageCommandEntry{
        "vendor",
        "usage: pafio vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]\n",
    },
    UsageCommandEntry{
        "build",
        "usage: pafio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n",
    },
    UsageCommandEntry{
        "run",
        "usage: pafio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n",
    },
    UsageCommandEntry{
        "test",
        "usage: pafio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n",
    },
    UsageCommandEntry{
        "pack",
        "usage: pafio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]\n",
    },
    UsageCommandEntry{
        "publish",
        "usage: pafio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <http(s)-url>] [--dry-run] [--allow-plaintext-http]\n",
    },
    UsageCommandEntry{
        "registry",
        "usage:\n"
        "  pafio registry trust import <descriptor-url|descriptor-file>\n"
        "  pafio registry trust status --json\n",
    },
};

constexpr std::string_view kEmitObservableStaticSnapshotFlag = "--emit-observable-static-snapshot";
constexpr std::string_view kEmitObservableStaticSnapshotFlagWithValue = "--emit-observable-static-snapshot=";
constexpr std::string_view kObservableCapabilityFlag = "--observable-capability";
constexpr std::string_view kObservableParentSnapshotFlag = "--observable-parent-snapshot";
constexpr std::string_view kEmitRuntimeObservationFlag = "--emit-runtime-observation";
constexpr std::string_view kEmitRuntimeObservationFlagWithValue = "--emit-runtime-observation=";
constexpr std::string_view kRuntimeObservationModeFlag = "--runtime-observation-mode";
constexpr std::string_view kRuntimeObservationCapabilityFlag = "--runtime-observation-capability";
constexpr std::string_view kRuntimeObservationLaneCapacityFlag = "--runtime-observation-lane-capacity";
constexpr std::string_view kRuntimeObservationSamplingFlag = "--runtime-observation-sampling";
constexpr std::array<std::string_view, 4> kRuntimeObservationModes = {"disabled", "aggregate", "sampled", "detailed"};

// Strict decimal parse: digits only, at most nine of them, no sign, no blanks.
std::optional<int> ParseNonNegativeInt(std::string_view value)
{
  if (value.empty() || value.size() > 9)
  {
    return std::nullopt;
  }
  int parsed = 0;
  for (const char ch : value)
  {
    if (ch < '0' || ch > '9')
    {
      return std::nullopt;
    }
    parsed = parsed * 10 + (ch - '0');
  }
  return parsed;
}

std::optional<int> ParsePositiveInt(std::string_view value)
{
  const std::optional<int> parsed = ParseNonNegativeInt(value);
  if (!parsed.has_value() || *parsed < 1)
  {
    return std::nullopt;
  }
  return parsed;
}

std::optional<int> ParseSchemaVersion(const std::string &value)
{
  return ParsePositiveInt(value);
}

// <numerator>/<denominator>[@<seed>]; every part is a strict decimal integer.
std::optional<RuntimeObservationSampling> ParseRuntimeObservationSampling(std::string_view value)
{
  const size_t slash = value.find('/');
  if (slash == std::string_view::npos)
  {
    return std::nullopt;
  }
  const std::optional<int> numerator = ParsePositiveInt(value.substr(0, slash));
  if (!numerator.has_value())
  {
    return std::nullopt;
  }
  std::string_view rest = value.substr(slash + 1);
  std::optional<long long> seed;
  if (const size_t at = rest.find('@'); at != std::string_view::npos)
  {
    const std::optional<int> parsed_seed = ParseNonNegativeInt(rest.substr(at + 1));
    if (!parsed_seed.has_value())
    {
      return std::nullopt;
    }
    seed = *parsed_seed;
    rest = rest.substr(0, at);
  }
  const std::optional<int> denominator = ParsePositiveInt(rest);
  if (!denominator.has_value())
  {
    return std::nullopt;
  }
  RuntimeObservationSampling sampling;
  sampling.numerator = *numerator;
  sampling.denominator = *denominator;
  sampling.seed = seed;
  return sampling;
}

const UsageCommandEntry *FindUsageCommandEntry(std::string_view command)
{
  const auto it = std::find_if(
      kUsageCommands.begin(),
      kUsageCommands.end(),
      [command](const UsageCommandEntry &entry) {
        return entry.name == command;
      });
  return it == kUsageCommands.end() ? nullptr : &(*it);
}

}  // namespace

int EmitError(const CommandError &error, bool as_json)
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
  return kExitSuccess;
}

int PrintGlobalHelp()
{
  std::cout
      << "pafio usage:\n"
      << "  pafio [-h|--help] [--version] [--json] <command> [command-args...]\n\n"
      << "commands:\n"
      << "  machine-info [--json]\n"
      << "  doctor [--json] [--manifest-path <path>] [--styio-bin <path>]\n"
      << "  metadata --json [--manifest-path <path>] [--locked|--offline|--frozen]\n"
      << "  new <package-name> [directory] [--lib|--bin]\n"
      << "  init [--name <package-name>] [--lib|--bin]\n"
      << "  check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n"
      << "  add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]\n"
      << "  remove <alias-or-package> [--dev] [--manifest-path <path>]\n"
      << "  sync [--manifest-path <path>] [--locked|--offline|--frozen]\n"
      << "  tree [--manifest-path <path>]\n"
      << "  vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]\n"
      << "  build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n"
      << "  run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n"
      << "  test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n"
      << "  pack [--manifest-path <path>] [--package <package-name>] [--output <path>]\n"
      << "  publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <http(s)-url>] [--dry-run] [--allow-plaintext-http]\n"
      << "  registry trust import <descriptor-url|descriptor-file>\n"
      << "  registry trust status --json\n";
  return kExitSuccess;
}

int PrintCommandUsage(std::string_view command)
{
  const UsageCommandEntry *const entry = FindUsageCommandEntry(command);
  if (entry == nullptr)
  {
    std::cout << "usage: pafio " << command << '\n';
    return kExitSuccess;
  }
  std::cout << entry->usage;
  return kExitSuccess;
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

ResolveOptions BuildResolveOptions(
    const fs::path &manifest_path,
    const WorkflowFlags &flags,
    const std::optional<fs::path> &vendor_root_override)
{
  ResolveOptions options;
  options.offline = flags.offline;
  options.locked = flags.locked;
  if (vendor_root_override.has_value())
  {
    options.vendor_root = CanonicalAbsolutePath(*vendor_root_override);
  }
  else
  {
    const fs::path default_vendor_root = ProjectVendorRootForManifest(manifest_path);
    if (fs::exists(default_vendor_root))
    {
      options.vendor_root = default_vendor_root;
    }
  }
  return options;
}

std::optional<CommandError> ParsePlanInvocation(
    std::string_view command_name,
    std::string_view intent,
    bool allow_lib,
    bool allow_bin,
    bool allow_test,
    const std::vector<std::string> &args,
    ParsedPlanInvocation &parsed)
{
  parsed = {};
  parsed.request.intent = std::string(intent);
  std::vector<std::string> observable_capabilities;
  std::optional<std::string> observable_parent_snapshot;
  std::optional<std::string> runtime_observation_mode;
  std::vector<std::string> runtime_observation_capabilities;
  std::optional<int> runtime_observation_lane_capacity;
  std::optional<RuntimeObservationSampling> runtime_observation_sampling;

  try
  {
    for (size_t index = 0; index < args.size(); ++index)
    {
      if (args[index] == "--manifest-path")
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, "--manifest-path requires a value", std::string(command_name)};
        }
        parsed.request.manifest_path = args[index];
      }
      else if (args[index] == "--package")
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, "--package requires a value", std::string(command_name)};
        }
        parsed.request.package_name = args[index];
      }
      else if (args[index] == "--bin")
      {
        if (!allow_bin)
        {
          return CommandError{"UsageError", kExitUsage, std::string(command_name) + " does not accept --bin", std::string(command_name)};
        }
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, "--bin requires a value", std::string(command_name)};
        }
        parsed.request.bin_name = args[index];
      }
      else if (args[index] == "--test")
      {
        if (!allow_test)
        {
          return CommandError{"UsageError", kExitUsage, std::string(command_name) + " does not accept --test", std::string(command_name)};
        }
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, "--test requires a value", std::string(command_name)};
        }
        parsed.request.test_name = args[index];
      }
      else if (args[index] == "--lib")
      {
        if (!allow_lib)
        {
          return CommandError{"UsageError", kExitUsage, std::string(command_name) + " does not accept --lib", std::string(command_name)};
        }
        parsed.request.select_lib = true;
      }
      else if (args[index] == "--profile")
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, "--profile requires a value", std::string(command_name)};
        }
        parsed.request.profile = args[index];
      }
      else if (args[index] == "--dry-run")
      {
        parsed.dry_run = true;
      }
      else if (args[index] == "--styio-bin")
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, "--styio-bin requires a value", std::string(command_name)};
        }
        parsed.styio_bin = args[index];
      }
      else if (args[index] == kEmitObservableStaticSnapshotFlag || args[index].starts_with(kEmitObservableStaticSnapshotFlagWithValue))
      {
        ObservableStaticSnapshotRequest snapshot;
        if (parsed.request.observable_static_snapshot.has_value())
        {
          snapshot = *parsed.request.observable_static_snapshot;
        }
        if (args[index] != kEmitObservableStaticSnapshotFlag)
        {
          const std::string value = args[index].substr(kEmitObservableStaticSnapshotFlagWithValue.size());
          const std::optional<int> schema_version = ParseSchemaVersion(value);
          if (!schema_version.has_value())
          {
            return CommandError{
                "UsageError", kExitUsage,
                std::string(kEmitObservableStaticSnapshotFlag) + " requires a positive integer schema version, got: " + value,
                std::string(command_name)};
          }
          snapshot.schema_version = *schema_version;
        }
        parsed.request.observable_static_snapshot = std::move(snapshot);
      }
      else if (args[index] == kObservableCapabilityFlag)
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kObservableCapabilityFlag) + " requires a value", std::string(command_name)};
        }
        if (args[index].empty())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kObservableCapabilityFlag) + " requires a non-empty capability name", std::string(command_name)};
        }
        observable_capabilities.push_back(args[index]);
      }
      else if (args[index] == kObservableParentSnapshotFlag)
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kObservableParentSnapshotFlag) + " requires a value", std::string(command_name)};
        }
        if (args[index].empty())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kObservableParentSnapshotFlag) + " requires a non-empty path", std::string(command_name)};
        }
        observable_parent_snapshot = args[index];
      }
      else if (args[index] == kEmitRuntimeObservationFlag || args[index].starts_with(kEmitRuntimeObservationFlagWithValue))
      {
        RuntimeObservationRequest observation;
        if (parsed.request.runtime_observation.has_value())
        {
          observation = *parsed.request.runtime_observation;
        }
        if (args[index] != kEmitRuntimeObservationFlag)
        {
          const std::string value = args[index].substr(kEmitRuntimeObservationFlagWithValue.size());
          const std::optional<int> version = ParsePositiveInt(value);
          if (!version.has_value())
          {
            return CommandError{
                "UsageError", kExitUsage,
                std::string(kEmitRuntimeObservationFlag) + " requires a positive integer version, got: " + value,
                std::string(command_name)};
          }
          observation.version = *version;
        }
        parsed.request.runtime_observation = std::move(observation);
      }
      else if (args[index] == kRuntimeObservationModeFlag)
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kRuntimeObservationModeFlag) + " requires a value", std::string(command_name)};
        }
        if (std::find(kRuntimeObservationModes.begin(), kRuntimeObservationModes.end(), args[index]) == kRuntimeObservationModes.end())
        {
          return CommandError{
              "UsageError", kExitUsage,
              std::string(kRuntimeObservationModeFlag) + " requires one of disabled|aggregate|sampled|detailed, got: " + args[index],
              std::string(command_name)};
        }
        runtime_observation_mode = args[index];
      }
      else if (args[index] == kRuntimeObservationCapabilityFlag)
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kRuntimeObservationCapabilityFlag) + " requires a value", std::string(command_name)};
        }
        if (args[index].empty())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kRuntimeObservationCapabilityFlag) + " requires a non-empty capability name", std::string(command_name)};
        }
        runtime_observation_capabilities.push_back(args[index]);
      }
      else if (args[index] == kRuntimeObservationLaneCapacityFlag)
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kRuntimeObservationLaneCapacityFlag) + " requires a value", std::string(command_name)};
        }
        const std::optional<int> lane_capacity = ParsePositiveInt(args[index]);
        if (!lane_capacity.has_value())
        {
          return CommandError{
              "UsageError", kExitUsage,
              std::string(kRuntimeObservationLaneCapacityFlag) + " requires a positive integer, got: " + args[index],
              std::string(command_name)};
        }
        runtime_observation_lane_capacity = *lane_capacity;
      }
      else if (args[index] == kRuntimeObservationSamplingFlag)
      {
        if (++index >= args.size())
        {
          return CommandError{"UsageError", kExitUsage, std::string(kRuntimeObservationSamplingFlag) + " requires a value", std::string(command_name)};
        }
        const std::optional<RuntimeObservationSampling> sampling = ParseRuntimeObservationSampling(args[index]);
        if (!sampling.has_value())
        {
          return CommandError{
              "UsageError", kExitUsage,
              std::string(kRuntimeObservationSamplingFlag) + " requires <numerator>/<denominator>[@<seed>] with positive integers, got: " + args[index],
              std::string(command_name)};
        }
        runtime_observation_sampling = sampling;
      }
      else if (ConsumeWorkflowFlag(args[index], parsed.workflow_flags))
      {
        continue;
      }
      else
      {
        return CommandError{"UsageError", kExitUsage, "unexpected argument for " + std::string(command_name) + ": " + args[index], std::string(command_name)};
      }
    }
  }
  catch (const CommandError &error)
  {
    return error;
  }

  if (!observable_capabilities.empty())
  {
    if (!parsed.request.observable_static_snapshot.has_value())
    {
      return CommandError{
          "UsageError", kExitUsage,
          std::string(kObservableCapabilityFlag) + " requires " + std::string(kEmitObservableStaticSnapshotFlag),
          std::string(command_name)};
    }
    std::sort(observable_capabilities.begin(), observable_capabilities.end());
    observable_capabilities.erase(
        std::unique(observable_capabilities.begin(), observable_capabilities.end()),
        observable_capabilities.end());
    parsed.request.observable_static_snapshot->required_capabilities = std::move(observable_capabilities);
  }

  if (observable_parent_snapshot.has_value())
  {
    if (!parsed.request.observable_static_snapshot.has_value())
    {
      return CommandError{
          "UsageError", kExitUsage,
          std::string(kObservableParentSnapshotFlag) + " requires " + std::string(kEmitObservableStaticSnapshotFlag),
          std::string(command_name)};
    }
    parsed.request.observable_static_snapshot->parent_snapshot_path = fs::path(*observable_parent_snapshot);
  }

  const auto require_runtime_observation = [&](std::string_view flag) -> std::optional<CommandError> {
    if (parsed.request.runtime_observation.has_value())
    {
      return std::nullopt;
    }
    return CommandError{
        "UsageError", kExitUsage,
        std::string(flag) + " requires " + std::string(kEmitRuntimeObservationFlag),
        std::string(command_name)};
  };

  if (runtime_observation_mode.has_value())
  {
    if (auto error = require_runtime_observation(kRuntimeObservationModeFlag))
    {
      return error;
    }
    parsed.request.runtime_observation->mode = std::move(runtime_observation_mode);
  }

  if (!runtime_observation_capabilities.empty())
  {
    if (auto error = require_runtime_observation(kRuntimeObservationCapabilityFlag))
    {
      return error;
    }
    std::sort(runtime_observation_capabilities.begin(), runtime_observation_capabilities.end());
    runtime_observation_capabilities.erase(
        std::unique(runtime_observation_capabilities.begin(), runtime_observation_capabilities.end()),
        runtime_observation_capabilities.end());
    parsed.request.runtime_observation->required_capabilities = std::move(runtime_observation_capabilities);
  }

  if (runtime_observation_lane_capacity.has_value())
  {
    if (auto error = require_runtime_observation(kRuntimeObservationLaneCapacityFlag))
    {
      return error;
    }
    parsed.request.runtime_observation->lane_capacity = runtime_observation_lane_capacity;
  }

  if (runtime_observation_sampling.has_value())
  {
    if (auto error = require_runtime_observation(kRuntimeObservationSamplingFlag))
    {
      return error;
    }
    parsed.request.runtime_observation->sampling = runtime_observation_sampling;
  }

  return std::nullopt;
}

std::optional<CommandError> ValidateLockedPolicy(
    const fs::path &manifest_path,
    std::string_view command,
    const WorkflowFlags &flags,
    const ResolveOptions &resolve_options)
{
  if (!flags.locked)
  {
    return std::nullopt;
  }

  const fs::path lockfile_path = manifest_path.parent_path() / "pafio.lock";
  if (!fs::exists(lockfile_path))
  {
    return CommandError{
        .category = "LockfileError",
        .code = kExitLock,
        .message = "lockfile missing: " + lockfile_path.string(),
        .command = std::string(command),
    };
  }

  try
  {
    const LockGenerationResult generated = ResolveSingleVersionLockfile(manifest_path, resolve_options);
    if (ReadFile(lockfile_path) != SerializeLockfileCanonical(generated.lockfile))
    {
      return CommandError{
          .category = "LockfileError",
          .code = kExitLock,
          .message = "lockfile is stale: " + lockfile_path.string(),
          .command = std::string(command),
      };
    }
  }
  catch (const ValidationError &err)
  {
    return CommandError{"ManifestError", kExitManifest, err.what(), std::string(command)};
  }
  catch (const WorkspaceError &err)
  {
    return CommandError{"WorkspaceError", kExitWorkspace, err.what(), std::string(command)};
  }
  catch (const ResolutionError &err)
  {
    return CommandError{"ResolutionError", kExitResolve, err.what(), std::string(command)};
  }
  catch (const FetchError &err)
  {
    return CommandError{"FetchError", kExitFetch, err.what(), std::string(command)};
  }
  catch (const CacheError &err)
  {
    return CommandError{"CacheError", kExitCache, err.what(), std::string(command)};
  }

  return std::nullopt;
}

}  // namespace pafio
