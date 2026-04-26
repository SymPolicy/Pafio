#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace spio
{

struct ToolInstallRequest
{
  std::filesystem::path styio_binary;
};

struct ToolInstallResult
{
  std::filesystem::path source_binary;
  std::filesystem::path spio_home;
  std::filesystem::path install_root;
  std::filesystem::path install_binary_path;
  std::filesystem::path install_metadata_path;
  std::filesystem::path current_root;
  std::filesystem::path managed_binary_path;
  std::filesystem::path current_metadata_path;
  std::string compiler_version;
  std::string compiler_channel;
  std::string compiler_edition_max;
  std::string integration_phase;
  std::vector<int> supported_compile_plan_versions;
  std::vector<std::string> capabilities;
};

struct ToolUseRequest
{
  std::string compiler_version;
  std::optional<std::string> compiler_channel;
};

struct ToolUseResult
{
  std::filesystem::path spio_home;
  std::filesystem::path install_root;
  std::filesystem::path install_binary_path;
  std::filesystem::path install_metadata_path;
  std::filesystem::path current_root;
  std::filesystem::path managed_binary_path;
  std::filesystem::path current_metadata_path;
  std::string compiler_version;
  std::string compiler_channel;
  std::string compiler_edition_max;
  std::string integration_phase;
  std::vector<int> supported_compile_plan_versions;
  std::vector<std::string> capabilities;
};

struct ToolPinRequest
{
  std::filesystem::path manifest_path = "spio.toml";
  std::optional<std::string> compiler_version;
  std::optional<std::string> compiler_channel;
  bool clear = false;
};

struct ToolPinResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path pin_path;
  bool cleared = false;
  std::optional<std::filesystem::path> install_root;
  std::optional<std::filesystem::path> install_binary_path;
  std::optional<std::string> compiler_version;
  std::optional<std::string> compiler_channel;
};

struct ProjectToolchainPinStatus
{
  std::filesystem::path pin_path;
  std::string compiler_version;
  std::string compiler_channel;
};

struct ManagedToolchainStatus
{
  std::filesystem::path install_root;
  std::filesystem::path install_binary_path;
  std::filesystem::path install_metadata_path;
  std::string compiler_version;
  std::string compiler_channel;
  std::string compiler_edition_max;
  std::string integration_phase;
  std::vector<int> supported_compile_plan_versions;
  std::vector<std::string> capabilities;
  bool current = false;
};

struct ToolStatusResult
{
  std::filesystem::path spio_home;
  std::optional<std::filesystem::path> manifest_path;
  std::optional<ProjectToolchainPinStatus> project_pin;
  std::optional<ManagedToolchainStatus> current_compiler;
  std::vector<ManagedToolchainStatus> managed_toolchains;
};

ToolInstallResult InstallManagedStyio(const ToolInstallRequest &request);
ToolUseResult UseManagedStyio(const ToolUseRequest &request);
ToolPinResult PinManagedStyio(const ToolPinRequest &request);
ToolStatusResult QueryToolStatus(const std::optional<std::filesystem::path> &manifest_path = std::nullopt);

}  // namespace spio
