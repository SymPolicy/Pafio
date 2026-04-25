#pragma once

#include "SpioToolchain/State.hpp"

#include <string>

namespace spio
{

struct SharedCachePolicy
{
  bool shared_toolchain_read_only = true;
  bool shared_source_read_only = true;
  bool shared_registry_read_only = true;
  bool worker_local_reuse = false;
  bool shared_cache_promotion_eligible = false;
  std::string promotion_policy = "verified-only";
};

struct WorkerPoolKey
{
  std::string platform;
  std::string architecture;
  std::string execution_lane;
  std::string worker_trust_tier;
  std::string toolchain_mode;
  std::string channel;
  std::string build_mode;
  std::string compiler_fingerprint;
  std::string base_image_revision;
};

struct CloudExecutionPolicy
{
  std::string risk_class;
  std::string requested_execution_lane;
  std::string execution_lane;
  std::string worker_trust_tier;
  std::string requested_security_profile;
  std::string security_profile;
  bool fallback_applied = false;
  bool platform_protection_priority = true;
  bool tenant_isolation_priority = true;
  bool network_egress_default_deny = true;
  bool requires_container_sandbox = true;
  std::string routing_reason;
  SharedCachePolicy cache_policy;
  WorkerPoolKey worker_pool_key;
};

std::string DetectCloudPlatform();
std::string DetectCloudArchitecture();
std::string DetectCloudBaseImageRevision();
CloudExecutionPolicy ResolveCloudExecutionPolicy(const ProjectToolchainState &state);

}  // namespace spio
