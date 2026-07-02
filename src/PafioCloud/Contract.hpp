#pragma once

#include "PafioCloud/Execution.hpp"
#include "PafioToolchain/State.hpp"

#include <nlohmann/json.hpp>

namespace pafio
{

nlohmann::json SerializeSharedCachePolicy(const SharedCachePolicy &policy);
nlohmann::json SerializeWorkerPoolKey(const WorkerPoolKey &key);
nlohmann::json SerializeCloudExecutionPolicy(const CloudExecutionPolicy &policy);
nlohmann::json SerializeProjectToolchainState(const ProjectToolchainState &state);
nlohmann::json BuildCloudStatusPayload(const ProjectToolchainState &state, const CloudExecutionPolicy &policy);

}  // namespace pafio
