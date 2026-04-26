#include "SpioCompat/Compat.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>
#include <toml++/toml.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

std::tuple<int, int, int> ParseSemver(const std::string &version)
{
  std::stringstream in(version);
  std::string major;
  std::string minor;
  std::string patch;
  if (!std::getline(in, major, '.') || !std::getline(in, minor, '.') || !std::getline(in, patch, '.'))
  {
    throw spio::CompatibilityError("invalid compiler version in handshake: " + version);
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
    throw spio::CompatibilityError("invalid compiler edition_max in handshake: " + edition);
  }
}

toml::table LoadCompatMatrix()
{
  const fs::path compat_matrix_path = spio::ProjectRoot() / "contracts" / "compat" / "styio-support.toml";
  return toml::parse_file(compat_matrix_path.string());
}

json ProbeMachineInfo(const fs::path &binary)
{
  const spio::ProcessResult result = spio::RunProcess<spio::CompilerProbeError>({
      .program = binary.string(),
      .args = {"--machine-info=json"},
      .search_path = false,
      .timeout = spio::kExternalProcessProbeTimeout,
      .error_context = "compiler probe process",
  });
  if (result.exit_code != 0)
  {
    const std::string detail = spio::DescribeProcessFailure(result);
    throw spio::CompilerProbeError("compiler '" + binary.string() + "' rejected --machine-info=json" + (detail.empty() ? "" : ": " + detail));
  }

  json payload;
  try
  {
    payload = json::parse(result.stdout_text);
  }
  catch (const json::parse_error &)
  {
    throw spio::CompatibilityError("compiler '" + binary.string() + "' returned invalid machine-info JSON");
  }

  static const std::array required_fields = {
      "tool",
      "compiler_version",
      "channel",
      "supported_contracts",
      "capabilities",
      "edition_max",
  };
  for (const char *field : required_fields)
  {
    if (!payload.contains(field))
    {
      throw spio::CompatibilityError("compiler handshake missing required field: " + std::string(field));
    }
  }
  if (!payload["supported_contracts"].is_object())
  {
    throw spio::CompatibilityError("compiler handshake field 'supported_contracts' must be an object");
  }
  if (!payload["capabilities"].is_array())
  {
    throw spio::CompatibilityError("compiler handshake field 'capabilities' must be an array");
  }
  if (payload["tool"] != "styio")
  {
    throw spio::CompatibilityError("compiler handshake field 'tool' must equal 'styio'");
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
    throw spio::CompatibilityError("compatibility matrix is missing supported_styio entries");
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

  throw spio::CompatibilityError("compiler version/channel is outside the published spio compatibility matrix");
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

struct ProjectToolchainPin
{
  fs::path pin_path;
  std::string compiler_version;
  std::string compiler_channel;
};

std::optional<ProjectToolchainPin> LoadProjectToolchainPin(const fs::path &manifest_path)
{
  const std::optional<fs::path> pin_path = spio::FindProjectToolchainPinPath(manifest_path);
  if (!pin_path.has_value())
  {
    return std::nullopt;
  }

  toml::table doc;
  try
  {
    doc = toml::parse_file(pin_path->string());
  }
  catch (const toml::parse_error &err)
  {
    throw spio::ToolError(
        "failed to parse project toolchain pin '" + pin_path->string() + "': " + std::string(err.description()));
  }

  const toml::table *styio_table = doc["styio"].as_table();
  if (styio_table == nullptr)
  {
    throw spio::ToolError("project toolchain pin is missing [styio]: " + pin_path->string());
  }

  const auto compiler_version = styio_table->get_as<std::string>("version");
  const auto compiler_channel = styio_table->get_as<std::string>("channel");
  if (compiler_version == nullptr || compiler_version->get().empty())
  {
    throw spio::ToolError("project toolchain pin is missing styio.version: " + pin_path->string());
  }
  if (compiler_channel == nullptr || compiler_channel->get().empty())
  {
    throw spio::ToolError("project toolchain pin is missing styio.channel: " + pin_path->string());
  }

  return ProjectToolchainPin{
      .pin_path = *pin_path,
      .compiler_version = compiler_version->get(),
      .compiler_channel = compiler_channel->get(),
  };
}

}  // namespace

namespace spio
{

std::optional<fs::path> ResolveStyioBinary(
    const std::optional<std::string> &explicit_path,
    const std::optional<fs::path> &manifest_path)
{
  if (explicit_path.has_value() && !explicit_path->empty())
  {
    return CanonicalAbsolutePath(*explicit_path);
  }

  if (const char *env = std::getenv("SPIO_STYIO_BIN"))
  {
    if (*env != '\0')
    {
      return CanonicalAbsolutePath(env);
    }
  }

  if (manifest_path.has_value())
  {
    if (const std::optional<ProjectToolchainPin> pin = LoadProjectToolchainPin(*manifest_path); pin.has_value())
    {
      const fs::path spio_home = ResolveSpioHome();
      const fs::path managed_binary =
          ManagedStyioBinaryPath(ManagedStyioInstallRoot(spio_home, pin->compiler_channel, pin->compiler_version));
      if (!fs::exists(managed_binary) || !fs::is_regular_file(managed_binary))
      {
        throw ToolError(
            "project toolchain pin requires an installed managed styio compiler: " + pin->compiler_channel + "/" +
            pin->compiler_version + " (" + pin->pin_path.string() + ")");
      }
      return managed_binary;
    }
  }

  const std::optional<fs::path> spio_home = ResolveOptionalSpioHome();
  if (spio_home.has_value())
  {
    const fs::path managed_binary = ManagedStyioBinaryPath(ManagedStyioCurrentRoot(*spio_home));
    if (fs::exists(managed_binary))
    {
      return managed_binary;
    }
  }

  return std::nullopt;
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
      throw CompatibilityError("compiler handshake does not provide the compile-plan versions required by this spio phase");
    }
  }

  const int compiler_edition = ParseEdition(machine_info["edition_max"].get<std::string>());
  const int supported_edition = ParseEdition(entry["edition_max"].value<std::string>().value_or(""));
  if (compiler_edition < supported_edition)
  {
    throw CompatibilityError("compiler edition_max is lower than the minimum edition supported by this spio phase");
  }

  std::sort(capabilities.begin(), capabilities.end());

  CompatibilityReport report;
  report.binary = fs::absolute(binary);
  report.compiler_version = machine_info["compiler_version"].get<std::string>();
  report.compiler_channel = machine_info["channel"].get<std::string>();
  report.compiler_edition_max = machine_info["edition_max"].get<std::string>();
  report.integration_phase = entry["integration_phase"].value<std::string>().value_or("unspecified");
  report.supported_compile_plan_versions = expected_compile_plan_versions;
  report.capabilities = std::move(capabilities);
  return report;
}

}  // namespace spio
