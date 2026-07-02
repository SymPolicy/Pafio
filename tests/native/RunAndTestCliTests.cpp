#include "BuildTestSupport.hpp"

#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioPlan/CompilePlan.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::ScopedEnvVar;
using pafio::testsupport::WriteExecutable;
using pafio::testsupport::WriteFakeCompilePlanStyio;
using pafio::testsupport::WriteFile;

TEST(RunCliTests, DryRunEmitsRunIntentForUniqueBinaryTarget)
{
  const fs::path root = MakeTempDir("run-dry-run");
  WriteFile(
      root / "pafio.toml",
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
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const pafio::BuildPlanResult result = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
  });
  const json plan = json::parse(pafio::testsupport::ReadFile(result.plan_path));
  EXPECT_EQ(plan["intent"], "run");
  EXPECT_EQ(plan["entry"]["target_kind"], "bin");
  EXPECT_EQ(plan["entry"]["target_name"], "app");
}

TEST(RunCliTests, RejectsLibSelection)
{
  const fs::path root = MakeTempDir("run-rejects-lib");
  WriteFile(
      root / "pafio.toml",
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
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const int exit_code = pafio::RunCli({
      "--json",
      "run",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--lib",
      "--dry-run",
  });
  EXPECT_EQ(exit_code, pafio::kExitUsage);
}

TEST(RunCliTests, NonDryRunRunRejectsCompilerWithoutRequiredCompilePlanVersion)
{
  const fs::path root = MakeTempDir("run-contract-gate");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteFile(
      root / "pafio.toml",
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

  const int exit_code = pafio::RunCli({
      "run",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  EXPECT_EQ(exit_code, pafio::kExitContract);
}

TEST(RunCliTests, NonDryRunRunExecutesPublishedCompilePlan)
{
  const fs::path root = MakeTempDir("run-compile-plan-live");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteFile(
      root / "pafio.toml",
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
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const fs::path fake_styio = root / "fake-styio";
  WriteFakeCompilePlanStyio(fake_styio);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "run",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("intent").get<std::string>(), "run");
  EXPECT_EQ(payload.at("styio").at("integration_phase").get<std::string>(), "compile-plan-live");
  const json receipt = json::parse(ReadFile(fs::path(payload.at("build_root").get<std::string>()) / "receipt.json"));
  EXPECT_EQ(receipt.at("intent").get<std::string>(), "run");
}

TEST(TestCliTests, DryRunEmitsTestIntentForUniqueTestTarget)
{
  const fs::path root = MakeTempDir("test-dry-run");
  WriteFile(
      root / "pafio.toml",
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
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", "# smoke := true\n");

  const pafio::BuildPlanResult result = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "test",
  });
  const json plan = json::parse(pafio::testsupport::ReadFile(result.plan_path));
  EXPECT_EQ(plan["intent"], "test");
  EXPECT_EQ(plan["entry"]["target_kind"], "test");
  EXPECT_EQ(plan["entry"]["target_name"], "smoke");
  ASSERT_TRUE(plan["packages"][0]["targets"].contains("tests"));
  EXPECT_EQ(plan["packages"][0]["targets"]["tests"][0]["name"], "smoke");
}

TEST(TestCliTests, RejectsBinSelection)
{
  const fs::path root = MakeTempDir("test-rejects-bin");
  WriteFile(
      root / "pafio.toml",
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
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", "# smoke := true\n");

  const int exit_code = pafio::RunCli({
      "--json",
      "test",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--bin",
      "app",
      "--dry-run",
  });
  EXPECT_EQ(exit_code, pafio::kExitUsage);
}

TEST(TestCliTests, NonDryRunTestRejectsCompilerWithoutRequiredCompilePlanVersion)
{
  const fs::path root = MakeTempDir("test-contract-gate");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteFile(
      root / "pafio.toml",
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
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", "# smoke := true\n");

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

  const int exit_code = pafio::RunCli({
      "test",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  EXPECT_EQ(exit_code, pafio::kExitContract);
}

TEST(TestCliTests, NonDryRunTestExecutesPublishedCompilePlan)
{
  const fs::path root = MakeTempDir("test-compile-plan-live");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteFile(
      root / "pafio.toml",
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
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", "# smoke := true\n");

  const fs::path fake_styio = root / "fake-styio";
  WriteFakeCompilePlanStyio(fake_styio);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "test",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("intent").get<std::string>(), "test");
  EXPECT_EQ(payload.at("styio").at("integration_phase").get<std::string>(), "compile-plan-live");
  const json receipt = json::parse(ReadFile(fs::path(payload.at("build_root").get<std::string>()) / "receipt.json"));
  EXPECT_EQ(receipt.at("intent").get<std::string>(), "test");
}
