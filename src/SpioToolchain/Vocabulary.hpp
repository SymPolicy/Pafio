#pragma once

#include <array>
#include <string_view>

namespace spio
{

inline constexpr std::string_view kToolchainModeBinary = "binary";
inline constexpr std::string_view kToolchainModeBuild = "build";
inline constexpr std::string_view kChannelStable = "stable";
inline constexpr std::string_view kChannelNightly = "nightly";
inline constexpr std::string_view kBuildModeMinimal = "minimal";
inline constexpr std::string_view kRiskClassTrustedInternal = "trusted-internal";
inline constexpr std::string_view kRiskClassPartnerControlled = "partner-controlled";
inline constexpr std::string_view kRiskClassUntrustedUser = "untrusted-user";
inline constexpr std::string_view kExecutionLaneIsolated = "isolated";
inline constexpr std::string_view kExecutionLaneWarmShared = "warm-shared";
inline constexpr std::string_view kSecurityProfileSandboxDefault = "sandbox-default";
inline constexpr std::string_view kSecurityProfilePartnerRestricted = "partner-restricted";
inline constexpr std::string_view kSecurityProfileTrustedWarm = "trusted-warm";

inline constexpr std::array<std::string_view, 2> kSupportedToolchainModes = {
    kToolchainModeBinary,
    kToolchainModeBuild,
};

inline constexpr std::array<std::string_view, 2> kSupportedChannels = {
    kChannelStable,
    kChannelNightly,
};

inline constexpr std::array<std::string_view, 1> kSupportedBuildModes = {
    kBuildModeMinimal,
};

inline constexpr std::array<std::string_view, 3> kSupportedCloudRiskClasses = {
    kRiskClassTrustedInternal,
    kRiskClassPartnerControlled,
    kRiskClassUntrustedUser,
};

inline constexpr std::array<std::string_view, 2> kSupportedCloudExecutionLanes = {
    kExecutionLaneIsolated,
    kExecutionLaneWarmShared,
};

inline constexpr std::array<std::string_view, 3> kSupportedCloudSecurityProfiles = {
    kSecurityProfileSandboxDefault,
    kSecurityProfilePartnerRestricted,
    kSecurityProfileTrustedWarm,
};

template <size_t N>
inline constexpr bool VocabularyContains(std::string_view value, const std::array<std::string_view, N> &supported)
{
  for (const std::string_view candidate : supported)
  {
    if (candidate == value)
    {
      return true;
    }
  }
  return false;
}

inline constexpr bool IsSupportedToolchainMode(std::string_view value)
{
  return VocabularyContains(value, kSupportedToolchainModes);
}

inline constexpr bool IsSupportedChannel(std::string_view value)
{
  return VocabularyContains(value, kSupportedChannels);
}

inline constexpr bool IsSupportedBuildMode(std::string_view value)
{
  return VocabularyContains(value, kSupportedBuildModes);
}

inline constexpr bool IsSupportedCloudRiskClass(std::string_view value)
{
  return VocabularyContains(value, kSupportedCloudRiskClasses);
}

inline constexpr bool IsSupportedCloudExecutionLane(std::string_view value)
{
  return VocabularyContains(value, kSupportedCloudExecutionLanes);
}

inline constexpr bool IsSupportedCloudSecurityProfile(std::string_view value)
{
  return VocabularyContains(value, kSupportedCloudSecurityProfiles);
}

}  // namespace spio
