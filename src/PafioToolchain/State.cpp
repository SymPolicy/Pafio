#include "PafioToolchain/State.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioManifest/Manifest.hpp"

#include <fstream>
#include <string>

#include <toml++/toml.h>

namespace fs = std::filesystem;

namespace
{

template <size_t N>
void RequireAllowedValue(const std::string &value, const std::array<std::string_view, N> &allowed, const std::string &label)
{
  for (const std::string_view candidate : allowed)
  {
    if (value == candidate)
    {
      return;
    }
  }
  throw pafio::ToolError("unsupported " + label + ": " + value);
}

std::string RenderToolchainState(const pafio::ProjectToolchainState &state)
{
  std::string content;
  content += "[toolchain]\n";
  content += "mode = \"" + state.mode + "\"\n";
  content += "channel = \"" + state.channel + "\"\n";
  content += "build = \"" + state.build_mode + "\"\n";
  content += "\n[cloud]\n";
  content += "risk = \"" + state.risk_class + "\"\n";
  content += "lane = \"" + state.preferred_execution_lane + "\"\n";
  content += "security = \"" + state.security_profile + "\"\n";
  if (state.source_revision.has_value())
  {
    content += "\n[source]\n";
    content += "revision = \"" + *state.source_revision + "\"\n";
  }
  return content;
}

void WriteTextFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out)
  {
    throw pafio::ToolError("failed to open toolchain state for write: " + path.string());
  }
  out << content;
  if (!out.good())
  {
    throw pafio::ToolError("failed to write toolchain state: " + path.string());
  }
}

}  // namespace

namespace pafio
{

ProjectToolchainState LoadProjectToolchainState(const fs::path &manifest_path)
{
  ProjectToolchainState state;
  state.manifest_path = CanonicalAbsolutePath(manifest_path);
  state.state_path = ProjectToolchainStatePathForManifest(state.manifest_path);

  const std::optional<fs::path> discovered = FindProjectToolchainStatePath(state.manifest_path);
  if (!discovered.has_value())
  {
    return state;
  }

  state.state_path = *discovered;
  state.state_file_exists = true;

  toml::table doc;
  try
  {
    doc = toml::parse_file(discovered->string());
  }
  catch (const toml::parse_error &err)
  {
    throw ToolError("failed to parse project toolchain state '" + discovered->string() + "': " + std::string(err.description()));
  }

  const toml::table *toolchain_table = doc["toolchain"].as_table();
  if (toolchain_table == nullptr)
  {
    throw ToolError("project toolchain state is missing [toolchain]: " + discovered->string());
  }

  if (const auto value = toolchain_table->get_as<std::string>("mode"); value != nullptr && !value->get().empty())
  {
    state.mode = value->get();
  }
  if (const auto value = toolchain_table->get_as<std::string>("channel"); value != nullptr && !value->get().empty())
  {
    state.channel = value->get();
  }
  if (const auto value = toolchain_table->get_as<std::string>("build"); value != nullptr && !value->get().empty())
  {
    state.build_mode = value->get();
  }

  if (const toml::table *cloud_table = doc["cloud"].as_table(); cloud_table != nullptr)
  {
    if (const auto value = cloud_table->get_as<std::string>("risk"); value != nullptr && !value->get().empty())
    {
      state.risk_class = value->get();
    }
    if (const auto value = cloud_table->get_as<std::string>("lane"); value != nullptr && !value->get().empty())
    {
      state.preferred_execution_lane = value->get();
    }
    if (const auto value = cloud_table->get_as<std::string>("security"); value != nullptr && !value->get().empty())
    {
      state.security_profile = value->get();
    }
  }

  if (const toml::table *source_table = doc["source"].as_table(); source_table != nullptr)
  {
    if (const auto value = source_table->get_as<std::string>("revision"); value != nullptr && !value->get().empty())
    {
      state.source_revision = value->get();
    }
  }

  RequireAllowedValue(state.mode, pafio::kSupportedToolchainModes, "toolchain mode");
  RequireAllowedValue(state.channel, pafio::kSupportedChannels, "toolchain channel");
  RequireAllowedValue(state.build_mode, pafio::kSupportedBuildModes, "build mode");
  RequireAllowedValue(state.risk_class, pafio::kSupportedCloudRiskClasses, "cloud risk class");
  RequireAllowedValue(state.preferred_execution_lane, pafio::kSupportedCloudExecutionLanes, "cloud execution lane");
  RequireAllowedValue(state.security_profile, pafio::kSupportedCloudSecurityProfiles, "cloud security profile");
  return state;
}

ProjectToolchainState UpdateProjectToolchainState(const ToolchainStateUpdate &update)
{
  const fs::path manifest_path = CanonicalAbsolutePath(update.manifest_path);
  (void) LoadManifest(manifest_path);

  ProjectToolchainState state = LoadProjectToolchainState(manifest_path);
  state.manifest_path = manifest_path;
  state.state_path = ProjectToolchainStatePathForManifest(manifest_path);
  if (update.mode.has_value())
  {
    state.mode = *update.mode;
  }
  if (update.channel.has_value())
  {
    state.channel = *update.channel;
  }
  if (update.build_mode.has_value())
  {
    state.build_mode = *update.build_mode;
  }
  if (update.risk_class.has_value())
  {
    state.risk_class = *update.risk_class;
  }
  if (update.preferred_execution_lane.has_value())
  {
    state.preferred_execution_lane = *update.preferred_execution_lane;
  }
  if (update.security_profile.has_value())
  {
    state.security_profile = *update.security_profile;
  }
  if (update.source_revision.has_value())
  {
    state.source_revision = update.source_revision;
  }

  RequireAllowedValue(state.mode, pafio::kSupportedToolchainModes, "toolchain mode");
  RequireAllowedValue(state.channel, pafio::kSupportedChannels, "toolchain channel");
  RequireAllowedValue(state.build_mode, pafio::kSupportedBuildModes, "build mode");
  RequireAllowedValue(state.risk_class, pafio::kSupportedCloudRiskClasses, "cloud risk class");
  RequireAllowedValue(state.preferred_execution_lane, pafio::kSupportedCloudExecutionLanes, "cloud execution lane");
  RequireAllowedValue(state.security_profile, pafio::kSupportedCloudSecurityProfiles, "cloud security profile");

  WriteTextFile(state.state_path, RenderToolchainState(state));
  state.state_file_exists = true;
  return state;
}

}  // namespace pafio
