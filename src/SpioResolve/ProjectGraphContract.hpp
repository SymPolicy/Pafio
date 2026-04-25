#pragma once

#include "SpioCloud/Execution.hpp"
#include "SpioResolve/Resolver.hpp"
#include "SpioTool/Install.hpp"
#include "SpioToolchain/State.hpp"

#include <filesystem>

#include <nlohmann/json.hpp>

namespace spio
{

struct WorkflowFlags;

nlohmann::json BuildProjectGraphPayload(
    const std::filesystem::path &manifest_path,
    const ResolvedGraphResult &graph,
    const WorkflowFlags &workflow_flags,
    const ProjectToolchainState &toolchain_state,
    const CloudExecutionPolicy &cloud_policy,
    const ToolStatusResult &tool_status,
    const ResolveOptions &resolve_options);

}  // namespace spio
