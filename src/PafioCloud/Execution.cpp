#include "PafioCloud/Execution.hpp"

#include <cstdlib>

namespace
{

std::string DefaultSecurityProfile(const std::string &risk_class, const std::string &execution_lane)
{
  if (execution_lane == pafio::kExecutionLaneWarmShared)
  {
    return std::string(pafio::kSecurityProfileTrustedWarm);
  }
  if (risk_class == pafio::kRiskClassPartnerControlled)
  {
    return std::string(pafio::kSecurityProfilePartnerRestricted);
  }
  return std::string(pafio::kSecurityProfileSandboxDefault);
}

std::string DefaultWorkerTrustTier(const std::string &risk_class, const std::string &execution_lane)
{
  if (execution_lane == pafio::kExecutionLaneWarmShared)
  {
    return "internal-warm";
  }
  if (risk_class == pafio::kRiskClassTrustedInternal)
  {
    return "trusted-isolated";
  }
  if (risk_class == pafio::kRiskClassPartnerControlled)
  {
    return "partner-isolated";
  }
  return "untrusted-isolated";
}

std::string CompilerFingerprintForState(const pafio::ProjectToolchainState &state)
{
  if (state.mode == pafio::kToolchainModeBuild)
  {
    return state.source_revision.value_or("source-revision-unset");
  }
  return "published-" + state.channel + "-" + state.build_mode;
}

}  // namespace

namespace pafio
{

std::string DetectCloudPlatform()
{
#if defined(__linux__)
  return "linux";
#elif defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "unknown";
#endif
}

std::string DetectCloudArchitecture()
{
#if defined(__aarch64__) || defined(_M_ARM64)
  return "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__arm__) || defined(_M_ARM)
  return "arm";
#else
  return "unknown";
#endif
}

std::string DetectCloudBaseImageRevision()
{
  if (const char *revision = std::getenv("PAFIO_CLOUD_BASE_IMAGE_REVISION"); revision != nullptr && revision[0] != '\0')
  {
    return revision;
  }
  return "dev-unset";
}

CloudExecutionPolicy ResolveCloudExecutionPolicy(const ProjectToolchainState &state)
{
  CloudExecutionPolicy policy;
  policy.risk_class = state.risk_class;
  policy.requested_execution_lane = state.preferred_execution_lane;
  policy.execution_lane = state.preferred_execution_lane;
  policy.requested_security_profile = state.security_profile;
  policy.security_profile = state.security_profile;

  if (!pafio::IsSupportedCloudRiskClass(policy.risk_class))
  {
    policy.risk_class = std::string(pafio::kRiskClassUntrustedUser);
    policy.fallback_applied = true;
    policy.routing_reason = "unknown risk class fell back to untrusted-user";
  }

  if (!pafio::IsSupportedCloudExecutionLane(policy.execution_lane))
  {
    policy.execution_lane = std::string(pafio::kExecutionLaneIsolated);
    policy.fallback_applied = true;
    policy.routing_reason = "unknown execution lane fell back to isolated";
  }

  if (policy.risk_class == pafio::kRiskClassUntrustedUser && policy.execution_lane != pafio::kExecutionLaneIsolated)
  {
    policy.execution_lane = std::string(pafio::kExecutionLaneIsolated);
    policy.fallback_applied = true;
    policy.routing_reason = "untrusted-user jobs require isolated execution";
  }
  else if (policy.risk_class == pafio::kRiskClassPartnerControlled &&
           policy.execution_lane == pafio::kExecutionLaneWarmShared)
  {
    policy.execution_lane = std::string(pafio::kExecutionLaneIsolated);
    policy.fallback_applied = true;
    policy.routing_reason = "partner-controlled jobs require explicit allowlist before warm-shared execution";
  }
  else if (policy.routing_reason.empty())
  {
    if (policy.execution_lane == pafio::kExecutionLaneWarmShared)
    {
      policy.routing_reason = "trusted internal workload is eligible for warm-shared execution";
    }
    else
    {
      policy.routing_reason = "default isolated execution protects the platform before enabling warm-shared reuse";
    }
  }

  const std::string expected_security = DefaultSecurityProfile(policy.risk_class, policy.execution_lane);
  if (!pafio::IsSupportedCloudSecurityProfile(policy.security_profile) ||
      policy.security_profile != expected_security)
  {
    policy.security_profile = expected_security;
    policy.fallback_applied = true;
  }

  policy.worker_trust_tier = DefaultWorkerTrustTier(policy.risk_class, policy.execution_lane);
  policy.cache_policy.worker_local_reuse = policy.execution_lane == pafio::kExecutionLaneWarmShared;
  policy.cache_policy.shared_cache_promotion_eligible = policy.risk_class == pafio::kRiskClassTrustedInternal;
  policy.worker_pool_key = {
      .platform = DetectCloudPlatform(),
      .architecture = DetectCloudArchitecture(),
      .execution_lane = policy.execution_lane,
      .worker_trust_tier = policy.worker_trust_tier,
      .toolchain_mode = state.mode,
      .channel = state.channel,
      .build_mode = state.build_mode,
      .compiler_fingerprint = CompilerFingerprintForState(state),
      .base_image_revision = DetectCloudBaseImageRevision(),
  };
  return policy;
}

}  // namespace pafio
