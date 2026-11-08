#include "PafioCLI/MachineInfoContract.hpp"

#include "PafioCore/Version.hpp"

using json = nlohmann::json;

namespace pafio
{

json BuildMachineInfoPayload()
{
  return {
      {"tool", "pafio"},
      {"version", pafio::kVersion},
      {"implementation_language", "c++"},
      {"supported_manifests", json::array({1})},
      {"supported_lockfiles", json::array({1})},
      {"supported_contracts", {
                                 {"metadata", json::array({1})},
                                 {"resolution", json::array({1})},
                                 {"compile_plan", json::array({1})},
                                 {"workflow", json::array({1})},
                             }},
      {"owners", {
                     {"compile_plan", "pafio"},
                     {"compile_plan_consumer", "styio"},
                     {"diagnostics", "styio"},
                     {"manifest", "pafio"},
                     {"metadata", "pafio"},
                     {"receipt", "styio"},
                     {"resolution", "pafio"},
                     {"runtime_events", "styio"},
                     {"workflow", "pafio"},
                 }},
      {"notes", json::array({
                    "project workflows sync before external Styio validation",
                    "check/build/run/test hand off compile-plan v1 to system Styio",
                })},
  };
}

}  // namespace pafio
