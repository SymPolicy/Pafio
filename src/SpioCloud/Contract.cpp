#include "SpioCloud/Contract.hpp"

using json = nlohmann::json;

namespace spio
{

namespace
{

template <size_t N>
json SerializeSupportedValues(const std::array<std::string_view, N> &values)
{
  json payload = json::array();
  for (const std::string_view value : values)
  {
    payload.push_back(value);
  }
  return payload;
}

}  // namespace

json SerializeSharedCachePolicy(const SharedCachePolicy &policy)
{
  return {
      {"shared_toolchain_read_only", policy.shared_toolchain_read_only},
      {"shared_source_read_only", policy.shared_source_read_only},
      {"shared_registry_read_only", policy.shared_registry_read_only},
      {"worker_local_reuse", policy.worker_local_reuse},
      {"shared_cache_promotion_eligible", policy.shared_cache_promotion_eligible},
      {"promotion_policy", policy.promotion_policy},
  };
}

json SerializeWorkerPoolKey(const WorkerPoolKey &key)
{
  return {
      {"platform", key.platform},
      {"architecture", key.architecture},
      {"execution_lane", key.execution_lane},
      {"worker_trust_tier", key.worker_trust_tier},
      {"toolchain_mode", key.toolchain_mode},
      {"channel", key.channel},
      {"build_mode", key.build_mode},
      {"compiler_fingerprint", key.compiler_fingerprint},
      {"base_image_revision", key.base_image_revision},
  };
}

json SerializeCloudExecutionPolicy(const CloudExecutionPolicy &policy)
{
  return {
      {"contract_version", 1},
      {"risk_class", policy.risk_class},
      {"requested_execution_lane", policy.requested_execution_lane},
      {"execution_lane", policy.execution_lane},
      {"worker_trust_tier", policy.worker_trust_tier},
      {"requested_security_profile", policy.requested_security_profile},
      {"security_profile", policy.security_profile},
      {"fallback_applied", policy.fallback_applied},
      {"platform_protection_priority", policy.platform_protection_priority},
      {"tenant_isolation_priority", policy.tenant_isolation_priority},
      {"network_egress_default_deny", policy.network_egress_default_deny},
      {"requires_container_sandbox", policy.requires_container_sandbox},
      {"routing_reason", policy.routing_reason},
      {"cache_policy", SerializeSharedCachePolicy(policy.cache_policy)},
      {"worker_pool_key", SerializeWorkerPoolKey(policy.worker_pool_key)},
  };
}

json SerializeProjectToolchainState(const ProjectToolchainState &state)
{
  json payload = {
      {"path", state.state_path.string()},
      {"exists", state.state_file_exists},
      {"mode", state.mode},
      {"channel", state.channel},
      {"build_mode", state.build_mode},
      {"risk_class", state.risk_class},
      {"preferred_execution_lane", state.preferred_execution_lane},
      {"security_profile", state.security_profile},
  };
  if (state.source_revision.has_value())
  {
    payload["source_revision"] = *state.source_revision;
  }
  return payload;
}

json BuildCloudStatusPayload(const ProjectToolchainState &state, const CloudExecutionPolicy &policy)
{
  return {
      {"command", "cloud status"},
      {"message", "resolved cloud execution policy for " + state.manifest_path.string()},
      {"manifest_path", state.manifest_path.string()},
      {"toolchain_state_path", state.state_path.string()},
      {"toolchain_mode", state.mode},
      {"channel", state.channel},
      {"build_mode", state.build_mode},
      {"risk_class", state.risk_class},
      {"preferred_execution_lane", state.preferred_execution_lane},
      {"security_profile", state.security_profile},
      {"supported_execution_lanes", SerializeSupportedValues(kSupportedCloudExecutionLanes)},
      {"supported_risk_classes", SerializeSupportedValues(kSupportedCloudRiskClasses)},
      {"supported_security_profiles", SerializeSupportedValues(kSupportedCloudSecurityProfiles)},
      {"cloud", SerializeCloudExecutionPolicy(policy)},
  };
}

}  // namespace spio
