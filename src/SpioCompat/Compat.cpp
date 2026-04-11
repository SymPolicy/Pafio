#include "SpioCompat/Compat.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"

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

ChildProcessResult RunChildProcess(const fs::path &binary, const std::vector<std::string> &args)
{
  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    throw spio::CompilerProbeError("failed to create pipes for compiler probe");
  }

  const pid_t child = fork();
  if (child < 0)
  {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    throw spio::CompilerProbeError("failed to fork compiler probe process");
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
  const ChildProcessResult result = RunChildProcess(binary, {"--machine-info=json"});
  if (result.exit_code != 0)
  {
    const std::string detail = result.stderr_text.empty() ? result.stdout_text : result.stderr_text;
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

}  // namespace

namespace spio
{

std::optional<fs::path> ResolveStyioBinary(const std::optional<std::string> &explicit_path)
{
  if (explicit_path.has_value() && !explicit_path->empty())
  {
    return fs::path(*explicit_path);
  }

  if (const char *env = std::getenv("SPIO_STYIO_BIN"))
  {
    if (*env != '\0')
    {
      return fs::path(env);
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
