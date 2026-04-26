#pragma once

#include "SpioCore/Errors.hpp"
#include "SpioPlan/CompilePlan.hpp"
#include "SpioCloud/Execution.hpp"
#include "SpioToolchain/State.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace spio
{

struct CloudJobTargetSelection
{
  std::optional<std::string> package_name;
  std::optional<std::string> bin_name;
  std::optional<std::string> test_name;
  bool select_lib = false;
};

struct CloudJobSourceSelection
{
  std::string origin;
  std::optional<std::filesystem::path> explicit_source_root;
  std::optional<std::string> requested_revision;
  std::optional<std::string> resolved_revision;
  bool allow_fetch = true;
  bool assume_yes = false;
  bool non_interactive = false;
  bool offline = false;
};

struct WorkflowInvocationOptions
{
  bool locked = false;
  bool offline = false;
  std::optional<std::string> styio_bin;
  bool allow_fetch = true;
  bool assume_yes = false;
  bool non_interactive = false;
  std::optional<std::filesystem::path> source_root;
  std::optional<std::string> source_revision;
};

class CloudBuildJobRequest
{
public:
  int schema_version() const { return schema_version_; }
  const std::string &api_path() const { return api_path_; }
  const std::string &action() const { return action_; }
  const std::filesystem::path &manifest_path() const { return manifest_path_; }
  const std::string &toolchain_mode() const { return toolchain_mode_; }
  const std::string &channel() const { return channel_; }
  const std::string &build_mode() const { return build_mode_; }
  const std::string &profile() const { return profile_; }
  bool locked() const { return locked_; }
  bool offline() const { return offline_; }
  const CloudJobTargetSelection &target() const { return target_; }
  const CloudJobSourceSelection &source() const { return source_; }
  const CloudExecutionPolicy &cloud_policy() const { return cloud_policy_; }

private:
  friend CloudBuildJobRequest BuildCloudBuildJobRequest(
      std::string_view action,
      const BuildPlanRequest &request,
      const ProjectToolchainState &toolchain_state,
      const WorkflowInvocationOptions &options,
      const CloudExecutionPolicy &cloud_policy);

  int schema_version_ = 1;
  std::string api_path_ = "/api/styio-platform/v1/jobs";
  std::string action_;
  std::filesystem::path manifest_path_;
  std::string toolchain_mode_;
  std::string channel_;
  std::string build_mode_;
  std::string profile_;
  bool locked_ = false;
  bool offline_ = false;
  CloudJobTargetSelection target_;
  CloudJobSourceSelection source_;
  CloudExecutionPolicy cloud_policy_;
};

std::string ResolveSourceOriginBaseline();
CloudBuildJobRequest BuildCloudBuildJobRequest(
    std::string_view action,
    const BuildPlanRequest &request,
    const ProjectToolchainState &toolchain_state,
    const WorkflowInvocationOptions &options,
    const CloudExecutionPolicy &cloud_policy);
nlohmann::json BuildCloudBuildJobRequestPayload(const CloudBuildJobRequest &request);

}  // namespace spio
