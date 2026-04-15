#include "SpioSecurity/RegistrySecurity.hpp"

#include "SpioCore/Errors.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace
{

class ScopedReadSecurityResolver
{
public:
  explicit ScopedReadSecurityResolver(spio::RegistryReadSecurityResolver resolver)
      : previous_(spio::RegisterRegistryReadSecurityResolver(resolver))
  {
  }

  ~ScopedReadSecurityResolver()
  {
    (void) spio::RegisterRegistryReadSecurityResolver(previous_);
  }

private:
  spio::RegistryReadSecurityResolver previous_ = nullptr;
};

class ScopedWriteSecurityResolver
{
public:
  explicit ScopedWriteSecurityResolver(spio::RegistryWriteSecurityResolver resolver)
      : previous_(spio::RegisterRegistryWriteSecurityResolver(resolver))
  {
  }

  ~ScopedWriteSecurityResolver()
  {
    (void) spio::RegisterRegistryWriteSecurityResolver(previous_);
  }

private:
  spio::RegistryWriteSecurityResolver previous_ = nullptr;
};

int g_write_resolver_calls = 0;

spio::RegistryWriteSecurityDecision DelegatingWriteResolver(const spio::RegistryWriteSecurityRequest &request)
{
  ++g_write_resolver_calls;
  spio::RegistryWriteSecurityDecision decision = spio::ResolveDefaultRegistryWriteSecurity(request);
  decision.provider_name = "private-test";
  decision.mode = "delegated";
  return decision;
}

}  // namespace

TEST(SecurityTests, DefaultReadSecurityNormalizesSupportedRoots)
{
  const spio::RegistryReadSecurityDecision http_decision =
      spio::ResolveDefaultRegistryReadSecurity({.registry_root = "https://packages.example.test/"});
  EXPECT_EQ(http_decision.registry_root, "https://packages.example.test");
  EXPECT_TRUE(http_decision.request_headers.empty());
  EXPECT_EQ(http_decision.provider_name, "public-default");

  const spio::RegistryReadSecurityDecision file_decision =
      spio::ResolveDefaultRegistryReadSecurity({.registry_root = "file:///tmp/registry/"});
  EXPECT_EQ(file_decision.registry_root, "file:///tmp/registry");
  EXPECT_TRUE(file_decision.request_headers.empty());
  EXPECT_EQ(file_decision.provider_name, "public-default");
}

TEST(SecurityTests, DefaultReadSecurityRejectsUnsupportedSchemes)
{
  EXPECT_THROW(
      spio::ResolveDefaultRegistryReadSecurity({.registry_root = "ssh://packages.example.test"}),
      spio::FetchError);
}

TEST(SecurityTests, DefaultWriteSecurityNormalizesAnonymousHttpsPublish)
{
  const spio::RegistryWriteSecurityDecision decision =
      spio::ResolveDefaultRegistryWriteSecurity({.registry_root = "https://upload.example.test/"});
  EXPECT_EQ(decision.registry_root, "https://upload.example.test");
  EXPECT_TRUE(decision.request_headers.empty());
  EXPECT_EQ(decision.provider_name, "public-default");
  EXPECT_EQ(decision.mode, "anonymous");
  EXPECT_FALSE(decision.profile_name.has_value());
}

TEST(SecurityTests, DefaultWriteSecurityRejectsPrivateHooksInOpenSourceCore)
{
  EXPECT_THROW(
      spio::ResolveDefaultRegistryWriteSecurity({
          .registry_root = "https://upload.example.test",
          .profile_name = std::string("write-dev"),
      }),
      spio::PublishError);

  EXPECT_THROW(
      spio::ResolveDefaultRegistryWriteSecurity({
          .registry_root = "https://upload.example.test",
          .policy_file = fs::path("/tmp/policy.toml"),
      }),
      spio::PublishError);

  EXPECT_THROW(
      spio::ResolveDefaultRegistryWriteSecurity({
          .registry_root = "https://upload.example.test",
          .explicit_request_headers = {"X-Spio-Write-Token: dev-token"},
      }),
      spio::PublishError);
}

TEST(SecurityTests, RegisteredWriteResolverCanDelegateToDefaultChain)
{
  ScopedWriteSecurityResolver reset_to_default(nullptr);
  g_write_resolver_calls = 0;

  {
    ScopedWriteSecurityResolver override_resolver(DelegatingWriteResolver);
    const spio::RegistryWriteSecurityDecision decision =
        spio::ResolveRegistryWriteSecurity({.registry_root = "https://upload.example.test/"});
    EXPECT_EQ(g_write_resolver_calls, 1);
    EXPECT_EQ(decision.registry_root, "https://upload.example.test");
    EXPECT_TRUE(decision.request_headers.empty());
    EXPECT_EQ(decision.provider_name, "private-test");
    EXPECT_EQ(decision.mode, "delegated");
  }

  const spio::RegistryWriteSecurityDecision restored =
      spio::ResolveRegistryWriteSecurity({.registry_root = "https://upload.example.test/"});
  EXPECT_EQ(restored.provider_name, "public-default");
  EXPECT_EQ(restored.mode, "anonymous");
}

TEST(SecurityTests, ResolveRegistryReadSecurityUsesDefaultChainWhenNoOverrideIsRegistered)
{
  ScopedReadSecurityResolver reset_to_default(nullptr);
  const spio::RegistryReadSecurityDecision decision =
      spio::ResolveRegistryReadSecurity({.registry_root = "https://packages.example.test/"});
  EXPECT_EQ(decision.registry_root, "https://packages.example.test");
  EXPECT_EQ(decision.provider_name, "public-default");
}
