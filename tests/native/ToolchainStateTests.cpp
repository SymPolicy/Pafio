#include "SpioCLI/CLI.hpp"
#include "SpioCore/Errors.hpp"

#include "ToolTestSupport.hpp"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace spio_test_support;

TEST(ToolchainStateTests, UseBuildWritesProjectToolchainState)
{
  const fs::path root = MakeTempDir("toolchain-state-use-build");
  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "use",
      "build",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("mode").get<std::string>(), "build");
  EXPECT_EQ(payload.at("channel").get<std::string>(), "stable");
  EXPECT_EQ(payload.at("build_mode").get<std::string>(), "minimal");

  const std::string state_text = ReadFile(root / "project/spio-toolchain.lock");
  EXPECT_NE(state_text.find("mode = \"build\""), std::string::npos);
  EXPECT_NE(state_text.find("channel = \"stable\""), std::string::npos);
  EXPECT_NE(state_text.find("build = \"minimal\""), std::string::npos);
}

TEST(ToolchainStateTests, SetChannelSupportsCanonicalAndCompactSyntax)
{
  const fs::path root = MakeTempDir("toolchain-state-set-channel");
  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "set",
          "channel",
          "as",
          "nightly",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);

  EXPECT_EQ(
      spio::RunCli({
          "set",
          "build",
          "minimal",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);

  const std::string state_text = ReadFile(root / "project/spio-toolchain.lock");
  EXPECT_NE(state_text.find("channel = \"nightly\""), std::string::npos);
  EXPECT_NE(state_text.find("build = \"minimal\""), std::string::npos);
}

TEST(ToolchainStateTests, SetRiskLaneAndSecurityPersistCloudPreferences)
{
  const fs::path root = MakeTempDir("toolchain-state-set-cloud");
  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);
  EXPECT_EQ(
      spio::RunCli({
          "set",
          "lane",
          "warm-shared",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);
  EXPECT_EQ(
      spio::RunCli({
          "set",
          "security",
          "as",
          "trusted-warm",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);

  const std::string state_text = ReadFile(root / "project/spio-toolchain.lock");
  EXPECT_NE(state_text.find("risk = \"trusted-internal\""), std::string::npos);
  EXPECT_NE(state_text.find("lane = \"warm-shared\""), std::string::npos);
  EXPECT_NE(state_text.find("security = \"trusted-warm\""), std::string::npos);
}

TEST(ToolchainStateTests, CloudStatusDefaultsToIsolatedExecutionForUntrustedProjects)
{
  const fs::path root = MakeTempDir("cloud-status-default-isolated");
  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "cloud",
      "status",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "cloud status");
  EXPECT_EQ(payload.at("toolchain_mode").get<std::string>(), "binary");
  EXPECT_EQ(payload.at("risk_class").get<std::string>(), "untrusted-user");
  EXPECT_EQ(payload.at("preferred_execution_lane").get<std::string>(), "isolated");
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "isolated");
  EXPECT_EQ(payload.at("cloud").at("security_profile").get<std::string>(), "sandbox-default");
  EXPECT_EQ(payload.at("cloud").at("worker_pool_key").at("execution_lane").get<std::string>(), "isolated");
  EXPECT_EQ(payload.at("supported_execution_lanes"), json::array({"isolated", "warm-shared"}));
  EXPECT_EQ(
      payload.at("supported_risk_classes"),
      json::array({"trusted-internal", "partner-controlled", "untrusted-user"}));
  EXPECT_EQ(
      payload.at("supported_security_profiles"),
      json::array({"sandbox-default", "partner-restricted", "trusted-warm"}));
}

TEST(ToolchainStateTests, CloudStatusFallsBackFromWarmSharedForUntrustedProjects)
{
  const fs::path root = MakeTempDir("cloud-status-fallback");
  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "cloud",
      "status",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("preferred_execution_lane").get<std::string>(), "warm-shared");
  EXPECT_TRUE(payload.at("cloud").at("fallback_applied").get<bool>());
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "isolated");
  EXPECT_EQ(
      payload.at("cloud").at("routing_reason").get<std::string>(),
      "untrusted-user jobs require isolated execution");
}

TEST(ToolchainStateTests, CloudStatusKeepsWarmSharedExecutionForTrustedInternalProjects)
{
  const fs::path root = MakeTempDir("cloud-status-warm-shared");
  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);
  EXPECT_EQ(
      spio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);
  EXPECT_EQ(
      spio::RunCli({
          "set",
          "security",
          "as",
          "trusted-warm",
          "--manifest-path",
          manifest_path.string(),
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "cloud",
      "status",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "warm-shared");
  EXPECT_EQ(payload.at("cloud").at("worker_trust_tier").get<std::string>(), "internal-warm");
  EXPECT_TRUE(payload.at("cloud").at("cache_policy").at("worker_local_reuse").get<bool>());
  EXPECT_TRUE(payload.at("cloud").at("cache_policy").at("shared_cache_promotion_eligible").get<bool>());
}
