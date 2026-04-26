#include "SpioCLI/CLI.hpp"
#include "SpioCompat/Compat.hpp"
#include "SpioCore/Errors.hpp"
#include "SpioTool/Install.hpp"

#include "ToolTestSupport.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace spio_test_support;

TEST(ToolPinTests, ProjectPinOverridesManagedCurrentCompiler)
{
  const fs::path root = MakeTempDir("tool-pin-project-override");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v4, "0.0.4");
  WriteFakeStyio(styio_v5, "0.0.5");

  const spio::ToolInstallResult install_v4 = spio::InstallManagedStyio({.styio_binary = styio_v4});
  const spio::ToolInstallResult install_v5 = spio::InstallManagedStyio({.styio_binary = styio_v5});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  testing::internal::CaptureStdout();
  const int pin_exit = spio::RunCli({
      "--json",
      "tool",
      "pin",
      "--manifest-path",
      manifest_path.string(),
      "--version",
      "0.0.4",
  });
  const std::string pin_stdout = testing::internal::GetCapturedStdout();

  EXPECT_EQ(pin_exit, spio::kExitSuccess);
  const json pin_payload = json::parse(pin_stdout);
  EXPECT_EQ(pin_payload.at("command").get<std::string>(), "tool pin");
  EXPECT_EQ(pin_payload.at("channel").get<std::string>(), "stable");
  EXPECT_EQ(pin_payload.at("compiler_version").get<std::string>(), "0.0.4");

  const fs::path pin_path = pin_payload.at("pin_path").get<std::string>();
  EXPECT_EQ(
      ReadFile(pin_path),
      "[styio]\n"
      "channel = \"stable\"\n"
      "version = \"0.0.4\"\n");

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt, manifest_path);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(install_v4.install_binary_path));
  EXPECT_NE(*resolved, CanonicalAbsolutePath(install_v5.managed_binary_path));

  testing::internal::CaptureStdout();
  const int check_exit = spio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string check_stdout = testing::internal::GetCapturedStdout();

  EXPECT_EQ(check_exit, spio::kExitSuccess);
  const json check_payload = json::parse(check_stdout);
  EXPECT_EQ(check_payload.at("styio").at("binary").get<std::string>(), install_v4.install_binary_path.string());
}

TEST(ToolPinTests, EnvironmentVariableStillOverridesProjectPin)
{
  const fs::path root = MakeTempDir("tool-pin-env-override");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  WriteFakeStyio(styio_v4, "0.0.4");
  (void) spio::InstallManagedStyio({.styio_binary = styio_v4});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "tool",
          "pin",
          "--manifest-path",
          manifest_path.string(),
          "--version",
          "0.0.4",
      }),
      spio::kExitSuccess);

  const fs::path explicit_styio = root / "explicit-styio";
  WriteFakeStyio(explicit_styio, "0.0.6");
  const ScopedEnvVar styio_bin("SPIO_STYIO_BIN", explicit_styio.string());

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt, manifest_path);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(explicit_styio));
}

TEST(ToolPinTests, ClearRemovesProjectPinAndFallsBackToManagedCurrent)
{
  const fs::path root = MakeTempDir("tool-pin-clear");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v4, "0.0.4");
  WriteFakeStyio(styio_v5, "0.0.5");

  const spio::ToolInstallResult install_v4 = spio::InstallManagedStyio({.styio_binary = styio_v4});
  const spio::ToolInstallResult install_v5 = spio::InstallManagedStyio({.styio_binary = styio_v5});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "tool",
          "pin",
          "--manifest-path",
          manifest_path.string(),
          "--version",
          "0.0.4",
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int clear_exit = spio::RunCli({
      "--json",
      "tool",
      "pin",
      "--manifest-path",
      manifest_path.string(),
      "--clear",
  });
  const std::string clear_stdout = testing::internal::GetCapturedStdout();

  EXPECT_EQ(clear_exit, spio::kExitSuccess);
  const json clear_payload = json::parse(clear_stdout);
  EXPECT_EQ(clear_payload.at("mode").get<std::string>(), "clear");
  EXPECT_FALSE(fs::exists(clear_payload.at("pin_path").get<std::string>()));

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt, manifest_path);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(install_v5.managed_binary_path));
  EXPECT_NE(*resolved, CanonicalAbsolutePath(install_v4.install_binary_path));
}

TEST(ToolPinTests, CheckFailsWhenPinnedManagedCompilerIsMissing)
{
  const fs::path root = MakeTempDir("tool-pin-missing-compiler");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);
  WriteFile(
      root / "project/spio-toolchain.toml",
      "[styio]\n"
      "channel = \"stable\"\n"
      "version = \"0.0.5\"\n");

  testing::internal::CaptureStderr();
  const int exit_code = spio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stderr_text = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, spio::kExitToolInstall);
  const json payload = json::parse(stderr_text);
  EXPECT_EQ(payload.at("category").get<std::string>(), "ToolError");
}
