#include "PafioCLI/CLI.hpp"
#include "PafioCompat/Compat.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioTool/Install.hpp"

#include "BuildTestSupport.hpp"
#include "ToolTestSupport.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace pafio_test_support;

TEST(ToolInstallTests, InstallsManagedCompilerAndWritesMetadata)
{
  const fs::path root = MakeTempDir("install-managed-compiler");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path fake_styio = root / "fake-styio";
  WriteFakeStyio(fake_styio, "0.0.5");

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "tool",
      "install",
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "tool install");
  EXPECT_EQ(payload.at("compiler_version").get<std::string>(), "0.0.5");

  const fs::path install_binary = payload.at("install_binary_path").get<std::string>();
  const fs::path managed_binary = payload.at("managed_binary_path").get<std::string>();
  const fs::path current_metadata = payload.at("current_metadata_path").get<std::string>();

  EXPECT_TRUE(fs::exists(install_binary));
  EXPECT_TRUE(fs::exists(managed_binary));
  EXPECT_TRUE(fs::exists(current_metadata));

  const std::optional<fs::path> resolved = pafio::ResolveStyioBinary(std::nullopt);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(managed_binary));

  const json metadata = json::parse(ReadFile(current_metadata));
  EXPECT_EQ(metadata.at("compiler_version").get<std::string>(), "0.0.5");
  EXPECT_EQ(metadata.at("channel").get<std::string>(), "stable");
  EXPECT_EQ(metadata.at("managed_binary").get<std::string>(), CanonicalAbsolutePath(managed_binary).string());
}

TEST(ToolInstallTests, TopLevelInstallBuildsStyioFromSourceRootAndSelectsLatestStable)
{
  const fs::path root = MakeTempDir("install-styio-latest-source-root");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  pafio::testsupport::WriteFakeSourceToolchain(root / "styio-source");

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "install",
      "styio",
      "--source-root",
      (root / "styio-source").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "install");
  EXPECT_EQ(payload.at("package").get<std::string>(), "styio");
  EXPECT_EQ(payload.at("requested").get<std::string>(), "latest");
  EXPECT_EQ(payload.at("channel").get<std::string>(), "stable");
  EXPECT_EQ(payload.at("build_mode").get<std::string>(), "minimal");
  EXPECT_EQ(payload.at("compiler_version").get<std::string>(), "0.0.5");
  EXPECT_TRUE(payload.at("source_built").get<bool>());

  const fs::path managed_binary = payload.at("managed_binary_path").get<std::string>();
  EXPECT_TRUE(fs::exists(managed_binary));

  const std::optional<fs::path> resolved = pafio::ResolveStyioBinary(std::nullopt);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(managed_binary));
}

TEST(ToolInstallTests, CheckFallsBackToManagedCompilerWhenNoExplicitPathIsProvided)
{
  const fs::path root = MakeTempDir("check-managed-fallback");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path fake_styio = root / "fake-styio";
  WriteFakeStyio(fake_styio, "0.0.5");

  const pafio::ToolInstallResult install = pafio::InstallManagedStyio({.styio_binary = fake_styio});

  WriteFile(
      root / "project/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "project/src/main.styio", ">_(\"app\")\n");

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      (root / "project/pafio.toml").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_TRUE(payload.at("compiler_checked").get<bool>());
  EXPECT_EQ(payload.at("styio").at("binary").get<std::string>(), install.managed_binary_path.string());
}

TEST(ToolInstallTests, EnvironmentVariableTakesPrecedenceOverManagedCompiler)
{
  const fs::path root = MakeTempDir("env-precedence");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());

  const fs::path installed_styio = root / "installed-styio";
  WriteFakeStyio(installed_styio, "0.0.5");
  (void) pafio::InstallManagedStyio({.styio_binary = installed_styio});

  const fs::path explicit_styio = root / "explicit-styio";
  WriteFakeStyio(explicit_styio, "0.0.6");
  const ScopedEnvVar styio_bin("PAFIO_STYIO_BIN", explicit_styio.string());

  const std::optional<fs::path> resolved = pafio::ResolveStyioBinary(std::nullopt);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(explicit_styio));
}

TEST(ToolInstallTests, ToolUseSwitchesManagedCurrentVersion)
{
  const fs::path root = MakeTempDir("tool-use-switch");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v4, "0.0.4");
  WriteFakeStyio(styio_v5, "0.0.5");

  (void) pafio::InstallManagedStyio({.styio_binary = styio_v4});
  (void) pafio::InstallManagedStyio({.styio_binary = styio_v5});

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "tool",
      "use",
      "--version",
      "0.0.4",
      "--channel",
      "stable",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "tool use");
  EXPECT_EQ(payload.at("compiler_version").get<std::string>(), "0.0.4");
  EXPECT_EQ(payload.at("channel").get<std::string>(), "stable");

  const std::optional<fs::path> resolved = pafio::ResolveStyioBinary(std::nullopt);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(payload.at("managed_binary_path").get<std::string>()));
}

TEST(ToolInstallTests, ToolUseRejectsMissingManagedVersion)
{
  const fs::path root = MakeTempDir("tool-use-missing-version");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());

  testing::internal::CaptureStderr();
  const int exit_code = pafio::RunCli({
      "--json",
      "tool",
      "use",
      "--version",
      "0.0.4",
      "--channel",
      "stable",
  });
  const std::string stderr_text = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, pafio::kExitToolInstall);
  const json payload = json::parse(stderr_text);
  EXPECT_EQ(payload.at("category").get<std::string>(), "ToolError");
}

TEST(ToolInstallTests, RejectsCompilerOutsidePublishedCompatibilityMatrix)
{
  const fs::path root = MakeTempDir("tool-install-rejects-outside-matrix");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path fake_styio = root / "fake-styio";
  WriteFakeStyio(fake_styio, "0.1.2");

  testing::internal::CaptureStderr();
  const int exit_code = pafio::RunCli({
      "--json",
      "tool",
      "install",
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stderr_text = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, pafio::kExitContract);
  const json payload = json::parse(stderr_text);
  EXPECT_EQ(payload.at("category").get<std::string>(), "ContractError");
}
