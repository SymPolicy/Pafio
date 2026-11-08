#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace pafio
{

struct TufObjectBytes
{
  std::string relative_path;
  std::string bytes;
};

struct TufVerifiedTargets
{
  nlohmann::json signed_payload;
  std::string relative_path;
  std::string sha256;
};

struct TufVerifyResult
{
  bool ok = false;
  std::string error;
  std::string error_code;
  std::optional<TufVerifiedTargets> targets;
  int root_version = 0;
  int timestamp_version = 0;
  int snapshot_version = 0;
};

// Pure verifier: metadata map + optional trusted root pin -> verdict.
// Verification follows root -> timestamp -> snapshot -> namespace targets
// (package-scoped). targets_relative_path is the registry-declared location
// of the namespace targets document (config paths.targets_prefix + name).
// timestamp_floor/snapshot_floor carry the client's persisted replay floors:
// metadata versions below a previously accepted floor are refused.
TufVerifyResult VerifyTufChainForPackage(
    const std::map<std::string, std::string> &objects_by_relative_path,
    const std::string &package_name,
    const std::string &package_namespace,
    const std::optional<std::string> &trusted_root_sha256,
    const std::string &targets_relative_path,
    int timestamp_floor = 0,
    int snapshot_floor = 0);

// Full-registry verify matching verify_registry_root (used by parity fixtures).
TufVerifyResult VerifyTufRegistryRoot(const std::map<std::string, std::string> &objects_by_relative_path);

}  // namespace pafio
