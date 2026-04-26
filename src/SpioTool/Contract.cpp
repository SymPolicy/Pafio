#include "SpioTool/Contract.hpp"

#include "SpioCloud/Contract.hpp"

using json = nlohmann::json;

namespace spio
{

json BuildProjectPinPayload(const ProjectToolchainPinStatus &pin)
{
  return {
      {"pin_path", pin.pin_path.string()},
      {"compiler_version", pin.compiler_version},
      {"channel", pin.compiler_channel},
  };
}

json BuildManagedToolchainPayload(const ManagedToolchainStatus &status)
{
  return {
      {"install_root", status.install_root.string()},
      {"install_binary_path", status.install_binary_path.string()},
      {"install_metadata_path", status.install_metadata_path.string()},
      {"compiler_version", status.compiler_version},
      {"channel", status.compiler_channel},
      {"edition_max", status.compiler_edition_max},
      {"integration_phase", status.integration_phase},
      {"supported_compile_plan_versions", status.supported_compile_plan_versions},
      {"capabilities", status.capabilities},
      {"current", status.current},
  };
}

json BuildToolStatusPayload(
    const ToolStatusResult &status,
    const std::optional<ProjectToolchainState> &project_state,
    const std::optional<CloudExecutionPolicy> &cloud_policy)
{
  json payload = {
      {"command", "tool status"},
      {"spio_home", status.spio_home.string()},
      {"managed_toolchains", json::array()},
  };

  if (status.manifest_path.has_value())
  {
    payload["manifest_path"] = status.manifest_path->string();
  }
  if (status.project_pin.has_value())
  {
    payload["project_pin"] = BuildProjectPinPayload(*status.project_pin);
  }
  else
  {
    payload["project_pin"] = nullptr;
  }
  if (status.current_compiler.has_value())
  {
    payload["current_compiler"] = BuildManagedToolchainPayload(*status.current_compiler);
  }
  else
  {
    payload["current_compiler"] = nullptr;
  }
  for (const auto &toolchain : status.managed_toolchains)
  {
    payload["managed_toolchains"].push_back(BuildManagedToolchainPayload(toolchain));
  }

  if (project_state.has_value())
  {
    payload["toolchain_state"] = SerializeProjectToolchainState(*project_state);
  }
  if (cloud_policy.has_value())
  {
    payload["cloud"] = SerializeCloudExecutionPolicy(*cloud_policy);
  }
  return payload;
}

}  // namespace spio
