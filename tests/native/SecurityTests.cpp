#include "PafioSecurity/RegistrySecurity.hpp"
#include "PafioSecurity/RegistryTrust.hpp"
#include "BuildTestSupport.hpp"

#include "PafioCore/Errors.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace fs = std::filesystem;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ScopedEnvVar;
using pafio::testsupport::WriteFile;

namespace
{

class ScopedReadSecurityResolver
{
public:
  explicit ScopedReadSecurityResolver(pafio::RegistryReadSecurityResolver resolver)
      : previous_(pafio::RegisterRegistryReadSecurityResolver(resolver))
  {
  }

  ~ScopedReadSecurityResolver()
  {
    (void) pafio::RegisterRegistryReadSecurityResolver(previous_);
  }

private:
  pafio::RegistryReadSecurityResolver previous_ = nullptr;
};

class ScopedWriteSecurityResolver
{
public:
  explicit ScopedWriteSecurityResolver(pafio::RegistryWriteSecurityResolver resolver)
      : previous_(pafio::RegisterRegistryWriteSecurityResolver(resolver))
  {
  }

  ~ScopedWriteSecurityResolver()
  {
    (void) pafio::RegisterRegistryWriteSecurityResolver(previous_);
  }

private:
  pafio::RegistryWriteSecurityResolver previous_ = nullptr;
};

int g_write_resolver_calls = 0;

pafio::RegistryWriteSecurityDecision DelegatingWriteResolver(const pafio::RegistryWriteSecurityRequest &request)
{
  ++g_write_resolver_calls;
  pafio::RegistryWriteSecurityDecision decision = pafio::ResolveDefaultRegistryWriteSecurity(request);
  decision.provider_name = "test-delegate";
  decision.mode = "delegated";
  return decision;
}

}  // namespace

TEST(SecurityTests, DefaultReadSecurityNormalizesSupportedRoots)
{
  const pafio::RegistryReadSecurityDecision http_decision =
      pafio::ResolveDefaultRegistryReadSecurity({.registry_root = "https://packages.example.test/"});
  EXPECT_EQ(http_decision.registry_root, "https://packages.example.test");
  EXPECT_TRUE(http_decision.request_headers.empty());
  EXPECT_EQ(http_decision.provider_name, "public-default");

  const pafio::RegistryReadSecurityDecision file_decision =
      pafio::ResolveDefaultRegistryReadSecurity({.registry_root = "file:///tmp/registry/"});
  EXPECT_EQ(file_decision.registry_root, "file:///tmp/registry");
  EXPECT_TRUE(file_decision.request_headers.empty());
  EXPECT_EQ(file_decision.provider_name, "public-default");
}

TEST(SecurityTests, DefaultReadSecurityRejectsUnsupportedSchemes)
{
  EXPECT_THROW(
      pafio::ResolveDefaultRegistryReadSecurity({.registry_root = "ssh://packages.example.test"}),
      pafio::FetchError);
}

TEST(SecurityTests, DefaultWriteSecurityNormalizesAnonymousHttpsPublish)
{
  const pafio::RegistryWriteSecurityDecision decision =
      pafio::ResolveDefaultRegistryWriteSecurity({.registry_root = "https://upload.example.test/"});
  EXPECT_EQ(decision.registry_root, "https://upload.example.test");
  EXPECT_TRUE(decision.request_headers.empty());
  EXPECT_EQ(decision.provider_name, "public-default");
  EXPECT_EQ(decision.mode, "anonymous");
}

TEST(SecurityTests, DefaultWriteSecurityRequiresOptInForPlaintextHttp)
{
  EXPECT_THROW(
      pafio::ResolveDefaultRegistryWriteSecurity({.registry_root = "http://upload.example.test"}),
      pafio::PublishError);

  const pafio::RegistryWriteSecurityDecision decision = pafio::ResolveDefaultRegistryWriteSecurity(
      {.registry_root = "http://localhost:8080", .allow_plaintext_http = true});
  EXPECT_EQ(decision.registry_root, "http://localhost:8080");

  const pafio::RegistryWriteSecurityDecision loopback_ip = pafio::ResolveDefaultRegistryWriteSecurity(
      {.registry_root = "http://127.0.0.1:8080", .allow_plaintext_http = true});
  EXPECT_EQ(loopback_ip.registry_root, "http://127.0.0.1:8080");

  // The plaintext opt-in is a loopback escape hatch, not a general
  // insecure-transport override for remote registries.
  EXPECT_THROW(
      pafio::ResolveDefaultRegistryWriteSecurity(
          {.registry_root = "http://upload.example.test", .allow_plaintext_http = true}),
      pafio::PublishError);

  // A registered name that only shares the 127. prefix resolves remotely.
  EXPECT_THROW(
      pafio::ResolveDefaultRegistryWriteSecurity(
          {.registry_root = "http://127.attacker.example", .allow_plaintext_http = true}),
      pafio::PublishError);
  EXPECT_THROW(
      pafio::ResolveDefaultRegistryWriteSecurity(
          {.registry_root = "http://127.0.0.1.evil.test", .allow_plaintext_http = true}),
      pafio::PublishError);
}

TEST(SecurityTests, DefaultSecurityRejectsUrlUserinfo)
{
  EXPECT_THROW(
      pafio::ResolveDefaultRegistryReadSecurity({.registry_root = "https://user:token@packages.example.test"}),
      pafio::FetchError);
  EXPECT_THROW(
      pafio::ResolveDefaultRegistryWriteSecurity({.registry_root = "https://user:token@upload.example.test"}),
      pafio::PublishError);
}

TEST(SecurityTests, RegisteredWriteResolverCanDelegateToDefaultChain)
{
  ScopedWriteSecurityResolver reset_to_default(nullptr);
  g_write_resolver_calls = 0;

  {
    ScopedWriteSecurityResolver override_resolver(DelegatingWriteResolver);
    const pafio::RegistryWriteSecurityDecision decision =
        pafio::ResolveRegistryWriteSecurity({.registry_root = "https://upload.example.test/"});
    EXPECT_EQ(g_write_resolver_calls, 1);
    EXPECT_EQ(decision.registry_root, "https://upload.example.test");
    EXPECT_TRUE(decision.request_headers.empty());
    EXPECT_EQ(decision.provider_name, "test-delegate");
    EXPECT_EQ(decision.mode, "delegated");
  }

  const pafio::RegistryWriteSecurityDecision restored =
      pafio::ResolveRegistryWriteSecurity({.registry_root = "https://upload.example.test/"});
  EXPECT_EQ(restored.provider_name, "public-default");
  EXPECT_EQ(restored.mode, "anonymous");
}

TEST(SecurityTests, ResolveRegistryReadSecurityUsesDefaultChainWhenNoOverrideIsRegistered)
{
  ScopedReadSecurityResolver reset_to_default(nullptr);
  const pafio::RegistryReadSecurityDecision decision =
      pafio::ResolveRegistryReadSecurity({.registry_root = "https://packages.example.test/"});
  EXPECT_EQ(decision.registry_root, "https://packages.example.test");
  EXPECT_EQ(decision.provider_name, "public-default");
}

TEST(SecurityTests, ValidatesRegistryPackageIdentitySegments)
{
  EXPECT_NO_THROW(pafio::ValidateRegistryPackageIdentity("acme/util"));
  const pafio::RegistryPackageNameParts parts = pafio::SplitRegistryPackageName("acme-corp/util_core", "registry package");
  EXPECT_EQ(parts.namespace_name, "acme-corp");
  EXPECT_EQ(parts.short_name, "util_core");

  EXPECT_THROW(pafio::ValidateRegistryPackageIdentity("acme"), pafio::FetchError);
  EXPECT_THROW(pafio::ValidateRegistryPackageIdentity("acme/util/extra"), pafio::FetchError);
  EXPECT_THROW(pafio::ValidateRegistryPackageIdentity("Acme/util"), pafio::FetchError);
  EXPECT_THROW(pafio::ValidateRegistryPackageIdentity("acme/util.core"), pafio::FetchError);
  EXPECT_THROW(pafio::ValidateRegistryPackageIdentity("../util"), pafio::FetchError);
  EXPECT_THROW(pafio::ValidateRegistryPackageIdentity("acme/.."), pafio::FetchError);
  EXPECT_THROW(pafio::ValidateRegistryPackageIdentity("acme\\evil/util"), pafio::FetchError);
  EXPECT_TRUE(pafio::IsRegistrySha256Digest("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  EXPECT_FALSE(pafio::IsRegistrySha256Digest("0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
}

TEST(SecurityTests, NormalizesRegistryObjectPathWithPythonCompatiblePolicy)
{
  EXPECT_EQ(pafio::NormalizeRegistryObjectPath("trust/targets/acme.json", "test").generic_string(), "trust/targets/acme.json");

  EXPECT_THROW(pafio::NormalizeRegistryObjectPath("../trust/root.json", "test"), pafio::FetchError);
  EXPECT_THROW(pafio::NormalizeRegistryObjectPath("trust/../root.json", "test"), pafio::FetchError);
  EXPECT_THROW(pafio::NormalizeRegistryObjectPath("trust/./root.json", "test"), pafio::FetchError);
  EXPECT_THROW(pafio::NormalizeRegistryObjectPath("trust//root.json", "test"), pafio::FetchError);
  EXPECT_THROW(pafio::NormalizeRegistryObjectPath("/trust/root.json", "test"), pafio::FetchError);
  EXPECT_THROW(pafio::NormalizeRegistryObjectPath("trust/root.json/", "test"), pafio::FetchError);
  EXPECT_THROW(pafio::NormalizeRegistryObjectPath("trust\\root.json", "test"), pafio::FetchError);
}

TEST(SecurityTests, ImportsAndResolvesRegistryTrustDescriptor)
{
  const fs::path root = MakeTempDir("registry-trust-import");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path descriptor_path = root / "descriptor.json";
  const std::string root_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  WriteFile(
      descriptor_path,
      "{\n"
      "  \"schema_version\": 1,\n"
      "  \"registry_root\": \"https://packages.example.test/pafio/\",\n"
      "  \"registry_name\": \"unit-registry\",\n"
      "  \"root_sha256\": \"" + root_digest + "\",\n"
      "  \"control_plane_base_url\": \"https://packages.example.test/api/pafio-registry-control/v1\",\n"
      "  \"issued_at\": \"2026-05-02T00:00:00Z\",\n"
      "  \"expires\": \"2099-06-02T00:00:00Z\"\n"
      "}\n");

  EXPECT_THROW(
      pafio::ImportRegistryTrustDescriptor(root / ".pafio-home", descriptor_path.string()),
      pafio::FetchError);

  const pafio::RegistryTrustPin imported = pafio::ImportRegistryTrustDescriptor(
      root / ".pafio-home",
      descriptor_path.string(),
      pafio::RegistryTrustImportOptions{.allow_dev_unsigned = true});
  EXPECT_EQ(imported.registry_root, "https://packages.example.test/pafio");
  EXPECT_EQ(imported.root_sha256, root_digest);
  EXPECT_EQ(imported.registry_name, "unit-registry");
  EXPECT_EQ(imported.descriptor_sha256.size(), 64U);

  const std::optional<pafio::RegistryTrustPin> resolved =
      pafio::ResolveRegistryTrustPin(root / ".pafio-home", "https://packages.example.test/pafio/");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->root_sha256, root_digest);
  EXPECT_TRUE(fs::exists(root / ".pafio-home" / "registry" / "trust" / "registry-trust.json"));
}

TEST(SecurityTests, RejectsExpiredAndUnknownSignerDescriptors)
{
  const fs::path root = MakeTempDir("registry-trust-negative");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path expired_path = root / "expired.json";
  WriteFile(
      expired_path,
      "{\n"
      "  \"schema_version\": 1,\n"
      "  \"registry_root\": \"https://packages.example.test/pafio/\",\n"
      "  \"registry_name\": \"unit-registry\",\n"
      "  \"root_sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
      "  \"issued_at\": \"2020-01-01T00:00:00Z\",\n"
      "  \"expires\": \"2020-02-01T00:00:00Z\"\n"
      "}\n");
  EXPECT_THROW(
      pafio::ImportRegistryTrustDescriptor(
          root / ".pafio-home",
          expired_path.string(),
          pafio::RegistryTrustImportOptions{.allow_dev_unsigned = true}),
      pafio::FetchError);

  const fs::path unknown_signer = root / "unknown-signer.json";
  WriteFile(
      unknown_signer,
      "{\n"
      "  \"signed\": {\n"
      "    \"schema_version\": 1,\n"
      "    \"registry_root\": \"https://packages.example.test/pafio/\",\n"
      "    \"registry_name\": \"unit-registry\",\n"
      "    \"root_sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
      "    \"issued_at\": \"2026-05-02T00:00:00Z\",\n"
      "    \"expires\": \"2099-06-02T00:00:00Z\"\n"
      "  },\n"
      "  \"signatures\": [{\"keyid\": \"unknown\", \"sig\": \"YWJjZA==\"}]\n"
      "}\n");
  EXPECT_THROW(
      pafio::ImportRegistryTrustDescriptor(root / ".pafio-home", unknown_signer.string()),
      pafio::FetchError);
}

TEST(SecurityTests, RejectsTrustDescriptorUrlsWithUserinfo)
{
  const fs::path root = MakeTempDir("registry-trust-userinfo");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const std::string root_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

  const fs::path userinfo_root = root / "userinfo-root.json";
  WriteFile(
      userinfo_root,
      "{\n"
      "  \"schema_version\": 1,\n"
      "  \"registry_root\": \"https://user:token@packages.example.test/pafio/\",\n"
      "  \"registry_name\": \"unit-registry\",\n"
      "  \"root_sha256\": \"" + root_digest + "\",\n"
      "  \"issued_at\": \"2026-05-02T00:00:00Z\",\n"
      "  \"expires\": \"2099-06-02T00:00:00Z\"\n"
      "}\n");
  EXPECT_THROW(
      pafio::ImportRegistryTrustDescriptor(
          root / ".pafio-home",
          userinfo_root.string(),
          pafio::RegistryTrustImportOptions{.allow_dev_unsigned = true}),
      pafio::FetchError);

  const fs::path userinfo_plane = root / "userinfo-plane.json";
  WriteFile(
      userinfo_plane,
      "{\n"
      "  \"schema_version\": 1,\n"
      "  \"registry_root\": \"https://packages.example.test/pafio/\",\n"
      "  \"registry_name\": \"unit-registry\",\n"
      "  \"root_sha256\": \"" + root_digest + "\",\n"
      "  \"control_plane_base_url\": \"https://user:token@packages.example.test/api/pafio-registry-control/v1\",\n"
      "  \"issued_at\": \"2026-05-02T00:00:00Z\",\n"
      "  \"expires\": \"2099-06-02T00:00:00Z\"\n"
      "}\n");
  EXPECT_THROW(
      pafio::ImportRegistryTrustDescriptor(
          root / ".pafio-home",
          userinfo_plane.string(),
          pafio::RegistryTrustImportOptions{.allow_dev_unsigned = true}),
      pafio::FetchError);
}

TEST(SecurityTests, PersistedReplayFloorsOnlyIncrease)
{
  const fs::path root = MakeTempDir("registry-trust-floors");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path descriptor_path = root / "descriptor.json";
  const std::string root_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  WriteFile(
      descriptor_path,
      "{\n"
      "  \"schema_version\": 1,\n"
      "  \"registry_root\": \"https://packages.example.test/pafio/\",\n"
      "  \"registry_name\": \"unit-registry\",\n"
      "  \"root_sha256\": \"" + root_digest + "\",\n"
      "  \"issued_at\": \"2026-05-02T00:00:00Z\",\n"
      "  \"expires\": \"2099-06-02T00:00:00Z\"\n"
      "}\n");
  const pafio::RegistryTrustPin imported = pafio::ImportRegistryTrustDescriptor(
      root / ".pafio-home",
      descriptor_path.string(),
      pafio::RegistryTrustImportOptions{.allow_dev_unsigned = true});
  EXPECT_EQ(imported.timestamp_floor, 0);
  EXPECT_EQ(imported.snapshot_floor, 0);

  pafio::RaiseRegistryTrustFloors(root / ".pafio-home", "https://packages.example.test/pafio", 7, 9);
  std::optional<pafio::RegistryTrustPin> pin =
      pafio::ResolveRegistryTrustPin(root / ".pafio-home", "https://packages.example.test/pafio");
  ASSERT_TRUE(pin.has_value());
  EXPECT_EQ(pin->timestamp_floor, 7);
  EXPECT_EQ(pin->snapshot_floor, 9);

  // A lower or equal observation never regresses the persisted floors.
  pafio::RaiseRegistryTrustFloors(root / ".pafio-home", "https://packages.example.test/pafio", 3, 9);
  pin = pafio::ResolveRegistryTrustPin(root / ".pafio-home", "https://packages.example.test/pafio");
  ASSERT_TRUE(pin.has_value());
  EXPECT_EQ(pin->timestamp_floor, 7);
  EXPECT_EQ(pin->snapshot_floor, 9);
}
