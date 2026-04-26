#include "BuildTestSupport.hpp"

#include "SpioCLI/CLI.hpp"
#include "SpioCore/Errors.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

using spio::testsupport::CanonicalAbsolutePath;
using spio::testsupport::MakeTempDir;
using spio::testsupport::ReadFile;
using spio::testsupport::ScopedEnvVar;
using spio::testsupport::WriteExecutable;
using spio::testsupport::WriteFakeCompilePlanStyio;
using spio::testsupport::WriteFakeSourceToolchain;
using spio::testsupport::WriteFile;

TEST(BuildCliTests, NonDryRunBuildRejectsCompilerWithoutRequiredCompilePlanVersion)
{
  const fs::path root = MakeTempDir("build-contract-gate");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  WriteFile(
      root / "spio.toml",
      "[spio]\n"
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
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const fs::path fake_styio = root / "fake-styio";
  WriteExecutable(
      fake_styio,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"supported_contracts\":{\"compile_plan\":[]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n");

  const int exit_code = spio::RunCli({
      "build",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  EXPECT_EQ(exit_code, spio::kExitContract);
}

TEST(BuildCliTests, NonDryRunBuildExecutesPublishedCompilePlan)
{
  const fs::path root = MakeTempDir("build-compile-plan-live");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  WriteFile(
      root / "spio.toml",
      "[spio]\n"
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
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const fs::path fake_styio = root / "fake-styio";
  WriteFakeCompilePlanStyio(fake_styio);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "build",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("mode").get<std::string>(), "execute");
  EXPECT_EQ(payload.at("intent").get<std::string>(), "build");
  EXPECT_EQ(payload.at("styio").at("integration_phase").get<std::string>(), "compile-plan-live");
  EXPECT_EQ(payload.at("styio").at("supported_compile_plan_versions").at(0).get<int>(), 1);

  const fs::path build_root = payload.at("build_root").get<std::string>();
  ASSERT_TRUE(fs::exists(build_root / "receipt.json"));
  const json receipt = json::parse(ReadFile(build_root / "receipt.json"));
  EXPECT_EQ(receipt.at("tool").get<std::string>(), "styio");
  EXPECT_EQ(receipt.at("intent").get<std::string>(), "build");
}

TEST(BuildCliTests, DryRunBuildMinimalReportsProjectToolchainState)
{
  const fs::path root = MakeTempDir("build-minimal-dry-run-state");
  WriteFile(
      root / "spio.toml",
      "[spio]\n"
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
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  ASSERT_EQ(
      spio::RunCli({
          "use",
          "build",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "build",
      "minimal",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--dry-run",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("mode").get<std::string>(), "dry-run");
  EXPECT_EQ(payload.at("toolchain_mode").get<std::string>(), "build");
  EXPECT_EQ(payload.at("build_mode").get<std::string>(), "minimal");
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "isolated");
  EXPECT_EQ(payload.at("cloud").at("worker_pool_key").at("toolchain_mode").get<std::string>(), "build");
}

TEST(BuildCliTests, BuildModeUsesLocalSourceRootToProduceCompiler)
{
  const fs::path root = MakeTempDir("build-mode-local-source-root");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  WriteFile(
      root / "project/spio.toml",
      "[spio]\n"
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
  WriteFakeSourceToolchain(root / "styio-source");

  ASSERT_EQ(
      spio::RunCli({
          "use",
          "build",
          "--manifest-path",
          (root / "project/spio.toml").string(),
      }),
      spio::kExitSuccess);
  ASSERT_EQ(
      spio::RunCli({
          "set",
          "channel",
          "as",
          "nightly",
          "--manifest-path",
          (root / "project/spio.toml").string(),
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "build",
      "minimal",
      "--manifest-path",
      (root / "project/spio.toml").string(),
      "--source-root",
      (root / "styio-source").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("toolchain_mode").get<std::string>(), "build");
  EXPECT_EQ(payload.at("build_mode").get<std::string>(), "minimal");
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "isolated");
  EXPECT_EQ(payload.at("styio").at("mode").get<std::string>(), "build");
  EXPECT_EQ(payload.at("styio").at("source_root").get<std::string>(), CanonicalAbsolutePath(root / "styio-source").string());
  EXPECT_TRUE(fs::exists(payload.at("styio").at("compiler_binary").get<std::string>()));
  EXPECT_NE(ReadFile(root / "project/spio-toolchain.lock").find("[source]"), std::string::npos);
}
