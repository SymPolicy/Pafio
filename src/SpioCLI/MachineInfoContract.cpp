#include "SpioCLI/MachineInfoContract.hpp"

#include "SpioCore/Version.hpp"

using json = nlohmann::json;

namespace spio
{

json BuildMachineInfoPayload()
{
  return {
      {"tool", "spio"},
      {"version", spio::kVersion},
      {"bootstrap", true},
      {"implementation_language", "c++"},
      {"supported_manifests", json::array({1})},
      {"supported_lockfiles", json::array({1})},
      {"supported_contracts", {
                                 {"compile_plan", json::array({1})},
                                 {"build_job_request", json::array({1})},
                                 {"project_graph", json::array({1})},
                                 {"toolchain_state", json::array({1})},
                                 {"workflow_success_payloads", json::array({1})},
                                 {"cloud_execution_policy", json::array({1})},
                                 {"worker_pool_keys", json::array({1})},
                             }},
      {"notes", json::array({
                    "native c++ workflow core with compile-plan v1 handoff",
                    "binary-mode build/run/test execute through styio --compile-plan when compatibility passes",
                    "cloud control-plane execution-policy baseline is active as a local machine contract",
                })},
  };
}

}  // namespace spio
