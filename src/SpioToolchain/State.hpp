#pragma once

#include "SpioToolchain/Vocabulary.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace spio
{

struct ProjectToolchainState
{
  std::filesystem::path manifest_path;
  std::filesystem::path state_path;
  bool state_file_exists = false;
  std::string mode = std::string(kToolchainModeBinary);
  std::string channel = std::string(kChannelStable);
  std::string build_mode = std::string(kBuildModeMinimal);
  std::string risk_class = std::string(kRiskClassUntrustedUser);
  std::string preferred_execution_lane = std::string(kExecutionLaneIsolated);
  std::string security_profile = std::string(kSecurityProfileSandboxDefault);
  std::optional<std::string> source_revision;
};

struct ToolchainStateUpdate
{
  std::filesystem::path manifest_path = "spio.toml";
  std::optional<std::string> mode;
  std::optional<std::string> channel;
  std::optional<std::string> build_mode;
  std::optional<std::string> risk_class;
  std::optional<std::string> preferred_execution_lane;
  std::optional<std::string> security_profile;
  std::optional<std::string> source_revision;
};

ProjectToolchainState LoadProjectToolchainState(const std::filesystem::path &manifest_path);
ProjectToolchainState UpdateProjectToolchainState(const ToolchainStateUpdate &update);

}  // namespace spio
