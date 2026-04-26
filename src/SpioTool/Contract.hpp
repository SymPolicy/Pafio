#pragma once

#include "SpioCloud/Execution.hpp"
#include "SpioTool/Install.hpp"
#include "SpioToolchain/State.hpp"

#include <optional>

#include <nlohmann/json.hpp>

namespace spio
{

nlohmann::json BuildProjectPinPayload(const ProjectToolchainPinStatus &pin);
nlohmann::json BuildManagedToolchainPayload(const ManagedToolchainStatus &status);
nlohmann::json BuildToolStatusPayload(
    const ToolStatusResult &status,
    const std::optional<ProjectToolchainState> &project_state = std::nullopt,
    const std::optional<CloudExecutionPolicy> &cloud_policy = std::nullopt);

}  // namespace spio
