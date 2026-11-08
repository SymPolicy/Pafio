#include "PafioCompat/Compat.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>
#include <toml++/toml.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

// The contract file contracts/compat/styio-support.toml is the single
// authority; CMake bakes its contents into this literal at configure time so
// dev-tree and installed binaries always evaluate the same matrix.
constexpr std::string_view kEmbeddedStyioSupportToml =
#include "EmbeddedStyioSupportToml.inc"
;

std::tuple<int, int, int> ParseSemver(const std::string &version)
{
  std::stringstream in(version);
  std::string major;
  std::string minor;
  std::string patch;
  if (!std::getline(in, major, '.') || !std::getline(in, minor, '.') || !std::getline(in, patch, '.'))
  {
    throw pafio::CompatibilityError("invalid compiler version in handshake: " + version);
  }

  return {
      std::stoi(major),
      std::stoi(minor),
      std::stoi(patch),
  };
}

int ParseEdition(const std::string &edition)
{
  try
  {
    return std::stoi(edition);
  }
  catch (const std::exception &)
  {
    throw pafio::CompatibilityError("invalid compiler edition_max in handshake: " + edition);
  }
}

toml::table LoadCompatMatrix()
{
  try
  {
    return toml::parse(kEmbeddedStyioSupportToml, std::string_view{"embedded styio-support.toml"});
  }
  catch (const toml::parse_error &err)
  {
    throw pafio::CompatibilityError("failed to parse embedded compatibility matrix: " + std::string(err.description()));
  }
}

json ProbeMachineInfo(const fs::path &binary)
{
  const pafio::ProcessResult result = pafio::RunProcess<pafio::CompilerProbeError>({
      .program = pafio::ProcessPathString(binary),
      .args = {"--machine-info=json"},
      .search_path = false,
      .timeout = pafio::kExternalProcessProbeTimeout,
      .error_context = "compiler probe process",
  });
  if (result.exit_code != 0)
  {
    const std::string detail = pafio::DescribeProcessFailure(result);
    throw pafio::CompilerProbeError("compiler '" + binary.string() + "' rejected --machine-info=json" + (detail.empty() ? "" : ": " + detail));
  }

  json payload;
  try
  {
    payload = json::parse(result.stdout_text);
  }
  catch (const json::parse_error &)
  {
    throw pafio::CompatibilityError("compiler '" + binary.string() + "' returned invalid machine-info JSON");
  }

  static const std::array required_fields = {
      "tool",
      "compiler_version",
      "channel",
      "active_integration_phase",
      "supported_contracts",
      "supported_contract_versions",
      "supported_adapter_modes",
      "feature_flags",
      "capabilities",
      "edition_max",
  };
  for (const char *field : required_fields)
  {
    if (!payload.contains(field))
    {
      throw pafio::CompatibilityError("compiler handshake missing required field: " + std::string(field));
    }
  }
  if (!payload["supported_contracts"].is_object())
  {
    throw pafio::CompatibilityError("compiler handshake field 'supported_contracts' must be an object");
  }
  if (!payload["capabilities"].is_array())
  {
    throw pafio::CompatibilityError("compiler handshake field 'capabilities' must be an array");
  }
  if (!payload["active_integration_phase"].is_string() || payload["active_integration_phase"].get<std::string>().empty())
  {
    throw pafio::CompatibilityError("compiler handshake field 'active_integration_phase' must be a non-empty string");
  }
  if (!payload["supported_contract_versions"].is_object())
  {
    throw pafio::CompatibilityError("compiler handshake field 'supported_contract_versions' must be an object");
  }
  if (!payload["supported_adapter_modes"].is_array())
  {
    throw pafio::CompatibilityError("compiler handshake field 'supported_adapter_modes' must be an array");
  }
  if (!payload["feature_flags"].is_object())
  {
    throw pafio::CompatibilityError("compiler handshake field 'feature_flags' must be an object");
  }
  if (payload["tool"] != "styio")
  {
    throw pafio::CompatibilityError("compiler handshake field 'tool' must equal 'styio'");
  }

  return payload;
}

const toml::table &FindMatchingEntry(const json &machine_info, const toml::table &compat_doc)
{
  const auto compiler_version = ParseSemver(machine_info["compiler_version"].get<std::string>());
  const std::string compiler_channel = machine_info["channel"].get<std::string>();

  const toml::array *entries = compat_doc["supported_styio"].as_array();
  if (entries == nullptr)
  {
    throw pafio::CompatibilityError("compatibility matrix is missing supported_styio entries");
  }

  for (const toml::node &node : *entries)
  {
    const toml::table *entry = node.as_table();
    if (entry == nullptr)
    {
      continue;
    }

    const auto min_version = ParseSemver(entry->at_path("min").value<std::string>().value_or(""));
    const auto max_version = ParseSemver(entry->at_path("max_exclusive").value<std::string>().value_or(""));
    const std::string channel = entry->at_path("channel").value<std::string>().value_or("");
    if (!(min_version <= compiler_version && compiler_version < max_version))
    {
      continue;
    }
    if (channel != compiler_channel)
    {
      continue;
    }
    return *entry;
  }

  throw pafio::CompatibilityError("compiler version/channel is outside the published pafio compatibility matrix");
}

std::vector<std::string> LoadStringArray(const toml::table &table, const std::string &key)
{
  std::vector<std::string> values;
  const toml::array *array = table[key].as_array();
  if (array == nullptr)
  {
    return values;
  }

  values.reserve(array->size());
  for (const toml::node &node : *array)
  {
    const auto value = node.value<std::string>();
    if (value.has_value())
    {
      values.push_back(*value);
    }
  }
  return values;
}

std::vector<int> LoadIntArray(const toml::table &table, const std::string &key)
{
  std::vector<int> values;
  const toml::array *array = table[key].as_array();
  if (array == nullptr)
  {
    return values;
  }

  values.reserve(array->size());
  for (const toml::node &node : *array)
  {
    const auto value = node.value<int64_t>();
    if (value.has_value())
    {
      values.push_back(static_cast<int>(*value));
    }
  }
  return values;
}

bool IsExecutable(const fs::path &path)
{
#if defined(_WIN32)
  return _waccess(path.c_str(), 0) == 0;
#else
  return access(path.string().c_str(), X_OK) == 0;
#endif
}

std::optional<fs::path> FindStyioOnPath()
{
  const char *path_value = std::getenv("PATH");
  if (path_value == nullptr || *path_value == '\0')
  {
    return std::nullopt;
  }

#if defined(_WIN32)
  constexpr char kPathSeparator = ';';
  constexpr std::array<std::string_view, 2> kNames{"styio.exe", "styio"};
#else
  constexpr char kPathSeparator = ':';
  constexpr std::array<std::string_view, 1> kNames{"styio"};
#endif

  std::stringstream entries(path_value);
  std::string entry;
  while (std::getline(entries, entry, kPathSeparator))
  {
    if (entry.empty())
    {
      continue;
    }
    for (const std::string_view name : kNames)
    {
      const fs::path candidate = fs::path(entry) / name;
      if (fs::is_regular_file(candidate) && IsExecutable(candidate))
      {
        return pafio::CanonicalAbsolutePath(candidate);
      }
    }
  }
  return std::nullopt;
}

}  // namespace

namespace pafio
{

std::optional<fs::path> ResolveStyioBinary(
    const std::optional<std::string> &explicit_path)
{
  if (explicit_path.has_value() && !explicit_path->empty())
  {
    return CanonicalAbsolutePath(*explicit_path);
  }

  if (const char *env = std::getenv("PAFIO_STYIO_BIN"))
  {
    if (*env != '\0')
    {
      return CanonicalAbsolutePath(env);
    }
  }

  return FindStyioOnPath();
}

CompatibilityReport CheckCompilerCompatibility(const fs::path &binary)
{
  const json machine_info = ProbeMachineInfo(binary);
  const toml::table compat_doc = LoadCompatMatrix();
  const toml::table &entry = FindMatchingEntry(machine_info, compat_doc);

  std::vector<std::string> capabilities = machine_info["capabilities"].get<std::vector<std::string>>();
  const std::vector<std::string> required_capabilities = LoadStringArray(entry, "required_capabilities");
  for (const std::string &required : required_capabilities)
  {
    if (std::find(capabilities.begin(), capabilities.end(), required) == capabilities.end())
    {
      throw CompatibilityError("compiler handshake is missing required capabilities: " + required);
    }
  }

  std::vector<int> supported_compile_plan_versions;
  if (machine_info["supported_contracts"].contains("compile_plan"))
  {
    supported_compile_plan_versions = machine_info["supported_contracts"]["compile_plan"].get<std::vector<int>>();
  }
  const std::vector<int> expected_compile_plan_versions = LoadIntArray(entry, "supported_compile_plan_versions");
  for (int version : expected_compile_plan_versions)
  {
    if (std::find(supported_compile_plan_versions.begin(), supported_compile_plan_versions.end(), version) == supported_compile_plan_versions.end())
    {
      throw CompatibilityError("compiler handshake does not provide the compile-plan versions required by this pafio phase");
    }
  }

  const int compiler_edition = ParseEdition(machine_info["edition_max"].get<std::string>());
  const int supported_edition = ParseEdition(entry["edition_max"].value<std::string>().value_or(""));
  if (compiler_edition < supported_edition)
  {
    throw CompatibilityError("compiler edition_max is lower than the minimum edition supported by this pafio phase");
  }

  std::sort(capabilities.begin(), capabilities.end());

  CompatibilityReport report;
  report.binary = fs::absolute(binary);
  report.compiler_version = machine_info["compiler_version"].get<std::string>();
  report.compiler_channel = machine_info["channel"].get<std::string>();
  report.compiler_edition_max = machine_info["edition_max"].get<std::string>();
  // The phase reported to consumers is the compiler's advertised
  // active_integration_phase; the matrix entry's phase is the requirement it
  // must satisfy, not a value to echo back.
  const std::string compiler_phase = machine_info["active_integration_phase"].get<std::string>();
  const std::string required_phase = entry["integration_phase"].value<std::string>().value_or("");
  if (!required_phase.empty() && compiler_phase != required_phase)
  {
    throw CompatibilityError(
        "compiler active_integration_phase '" + compiler_phase +
        "' does not match the phase required by this pafio phase: " + required_phase);
  }
  report.integration_phase = compiler_phase;
  report.supported_compile_plan_versions = expected_compile_plan_versions;
  report.capabilities = std::move(capabilities);
  return report;
}

}  // namespace pafio
