#pragma once

#include "PafioCloud/Execution.hpp"
#include "PafioResolve/Resolver.hpp"
#include "PafioTool/Install.hpp"
#include "PafioToolchain/State.hpp"

#include <filesystem>

#include <nlohmann/json.hpp>

namespace pafio
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

}  // namespace pafio
