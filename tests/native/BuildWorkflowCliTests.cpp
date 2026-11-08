#include "BuildTestSupport.hpp"

#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

using pafio::testsupport::CanonicalAbsolutePath;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::ScopedEnvVar;
using pafio::testsupport::WriteExecutable;
using pafio::testsupport::WriteFakeCompilePlanStyio;
using pafio::testsupport::WriteFile;

TEST(BuildCliTests, NonDryRunBuildRejectsCompilerWithoutRequiredCompilePlanVersion)
{
  const fs::path root = MakeTempDir("build-contract-gate");
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
      "[build]\n"
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
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":false},\"supported_contracts\":{\"compile_plan\":[]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n");

  const int exit_code = pafio::RunCli({
      "build",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  EXPECT_EQ(exit_code, pafio::kExitContract);
}

TEST(BuildCliTests, NonDryRunBuildExecutesPublishedCompilePlan)
{
  const fs::path root = MakeTempDir("build-compile-plan-live");
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
      "[build]\n"
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
      "build",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("action").get<std::string>(), "build");
  EXPECT_EQ(payload.at("status").get<std::string>(), "succeeded");
  EXPECT_EQ(payload.at("mode").get<std::string>(), "execute");
  EXPECT_EQ(payload.at("intent").get<std::string>(), "build");
  EXPECT_EQ(payload.at("sync").at("status").get<std::string>(), "succeeded");
  EXPECT_EQ(payload.at("styio").at("compiler_channel").get<std::string>(), "nightly");
  EXPECT_EQ(payload.at("styio").at("integration_phase").get<std::string>(), "compile-plan-live");
  EXPECT_EQ(payload.at("styio").at("process").at("status").get<std::string>(), "exited");
  EXPECT_EQ(payload.at("styio").at("process").at("exit_code").get<int>(), 0);
  EXPECT_EQ(payload.at("styio").at("supported_compile_plan_versions").at(0).get<int>(), 1);

  const fs::path build_root = payload.at("plan").at("build_root").get<std::string>();
  ASSERT_TRUE(fs::exists(build_root / "receipt.json"));
  const json receipt = json::parse(ReadFile(build_root / "receipt.json"));
  EXPECT_EQ(receipt.at("tool").get<std::string>(), "styio");
  EXPECT_EQ(receipt.at("intent").get<std::string>(), "build");
}

namespace
{

void WriteSingleBinProject(const fs::path &root)
{
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");
}

}  // namespace

TEST(BuildCliTests, DryRunPassesObservableStaticSnapshotRequestIntoCompilePlan)
{
  const fs::path root = MakeTempDir("build-observable-snapshot-dry-run");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "build",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--dry-run",
      "--emit-observable-static-snapshot",
      "--observable-capability",
      "zeta",
      "--observable-capability",
      "alpha",
      "--observable-capability",
      "zeta",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("mode").get<std::string>(), "dry-run");
  const json plan = json::parse(ReadFile(payload.at("plan").at("path").get<std::string>()));
  EXPECT_EQ(
      plan.at("emit").at("observable_static_snapshot").dump(),
      R"({"required_capabilities":["alpha","zeta"],"schema_version":1})");
}

TEST(BuildCliTests, DryRunWithoutObservableFlagLeavesCompilePlanEmitUnchanged)
{
  const fs::path root = MakeTempDir("build-observable-snapshot-absent");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--dry-run",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  const json plan = json::parse(ReadFile(payload.at("plan").at("path").get<std::string>()));
  EXPECT_EQ(
      plan.at("emit").dump(),
      R"({"ast":false,"error_format":"jsonl","llvm_ir":false,"styio_ir":false})");
}

TEST(BuildCliTests, ObservableStaticSnapshotFlagAcceptsExplicitSchemaVersion)
{
  const fs::path root = MakeTempDir("check-observable-snapshot-schema-version");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--dry-run",
      "--emit-observable-static-snapshot=2",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  const json plan = json::parse(ReadFile(payload.at("plan").at("path").get<std::string>()));
  EXPECT_EQ(
      plan.at("emit").at("observable_static_snapshot").dump(),
      R"({"required_capabilities":[],"schema_version":2})");
}

TEST(BuildCliTests, ObservableStaticSnapshotFlagsRejectMalformedValues)
{
  const fs::path root = MakeTempDir("check-observable-snapshot-usage-errors");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);
  const std::string manifest = (root / "pafio.toml").string();

  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--emit-observable-static-snapshot=abc"}),
      pafio::kExitUsage);
  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--emit-observable-static-snapshot=0"}),
      pafio::kExitUsage);
  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--emit-observable-static-snapshot="}),
      pafio::kExitUsage);
  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--observable-capability", "alpha"}),
      pafio::kExitUsage);
  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--emit-observable-static-snapshot", "--observable-capability"}),
      pafio::kExitUsage);
  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--emit-observable-static-snapshot", "--observable-capability", ""}),
      pafio::kExitUsage);
}

TEST(BuildCliTests, CheckHelpAdvertisesObservableStaticSnapshotFlags)
{
  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({"check", "--help"});
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  EXPECT_EQ(
      stdout_text,
      "usage: pafio check [--manifest-path <path>] [--styio-bin <path>] "
      "[--locked|--offline|--frozen] "
      "[--emit-observable-static-snapshot[=<schema-version>]] "
      "[--observable-capability <name>] "
      "[--observable-parent-snapshot <path>] "
      "[--emit-runtime-observation[=<version>]] "
      "[--runtime-observation-mode <disabled|aggregate|sampled|detailed>] "
      "[--runtime-observation-capability <name>] "
      "[--runtime-observation-lane-capacity <n>] "
      "[--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]\n");
}

TEST(BuildCliTests, DryRunPassesObservableParentSnapshotPathIntoCompilePlan)
{
  const fs::path root = MakeTempDir("build-observable-parent-snapshot-dry-run");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);
  const std::string manifest = (root / "pafio.toml").string();
  const fs::path parent = root / "previous" / "app.observable-static-snapshot.json";
  ASSERT_FALSE(fs::exists(parent));

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "build",
      "--manifest-path",
      manifest,
      "--dry-run",
      "--emit-observable-static-snapshot",
      "--observable-capability",
      "alpha",
      "--observable-parent-snapshot",
      parent.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("mode").get<std::string>(), "dry-run");
  const json plan = json::parse(ReadFile(payload.at("plan").at("path").get<std::string>()));
  EXPECT_EQ(
      plan.at("emit").at("observable_static_snapshot").dump(),
      R"({"parent_snapshot_path":")" + CanonicalAbsolutePath(parent).string() +
          R"(","required_capabilities":["alpha"],"schema_version":1})");

  testing::internal::CaptureStdout();
  const int without_parent_exit_code = pafio::RunCli({
      "--json",
      "build",
      "--manifest-path",
      manifest,
      "--dry-run",
      "--emit-observable-static-snapshot",
      "--observable-capability",
      "alpha",
  });
  const json without_parent = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_EQ(without_parent_exit_code, pafio::kExitSuccess);
  EXPECT_EQ(
      without_parent.at("plan").at("cache_key").get<std::string>(),
      payload.at("plan").at("cache_key").get<std::string>());
  EXPECT_NE(
      without_parent.at("plan").at("build_root").get<std::string>(),
      payload.at("plan").at("build_root").get<std::string>());
}

TEST(BuildCliTests, ObservableParentSnapshotFlagRejectsMalformedUsage)
{
  const fs::path root = MakeTempDir("check-observable-parent-snapshot-usage-errors");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);
  const std::string manifest = (root / "pafio.toml").string();

  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--observable-parent-snapshot", "previous.observable-static-snapshot.json"}),
      pafio::kExitUsage);
  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--emit-observable-static-snapshot", "--observable-parent-snapshot"}),
      pafio::kExitUsage);
  EXPECT_EQ(
      pafio::RunCli({"--json", "check", "--manifest-path", manifest, "--dry-run",
                     "--emit-observable-static-snapshot", "--observable-parent-snapshot", ""}),
      pafio::kExitUsage);
}

namespace
{

json DryRunEmit(const std::string &command, const std::string &manifest, const std::vector<std::string> &extra)
{
  std::vector<std::string> args = {"--json", command, "--manifest-path", manifest, "--dry-run"};
  args.insert(args.end(), extra.begin(), extra.end());
  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli(args);
  const std::string stdout_text = testing::internal::GetCapturedStdout();
  EXPECT_EQ(exit_code, pafio::kExitSuccess) << stdout_text;
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("mode").get<std::string>(), "dry-run");
  json plan = json::parse(ReadFile(payload.at("plan").at("path").get<std::string>()));
  plan["emit"]["__cache_key"] = payload.at("plan").at("cache_key");
  return plan.at("emit");
}

}  // namespace

TEST(BuildCliTests, DryRunPassesRuntimeObservationRequestIntoCompilePlan)
{
  const fs::path root = MakeTempDir("run-runtime-observation-dry-run");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);
  const std::string manifest = (root / "pafio.toml").string();

  json baseline = DryRunEmit("run", manifest, {});
  const std::string baseline_cache_key = baseline.at("__cache_key").get<std::string>();
  baseline.erase("__cache_key");
  EXPECT_EQ(baseline.dump(), R"({"ast":false,"error_format":"jsonl","llvm_ir":false,"styio_ir":false})");

  // The bare flag emits only the default version; Styio applies every other default.
  json version_only = DryRunEmit("run", manifest, {"--emit-runtime-observation"});
  const std::string version_only_cache_key = version_only.at("__cache_key").get<std::string>();
  version_only.erase("__cache_key");
  EXPECT_NE(version_only_cache_key, baseline_cache_key);
  EXPECT_FALSE(version_only.contains("observable_static_snapshot"));
  EXPECT_EQ(version_only.at("runtime_observation").dump(), R"({"version":2})");

  json explicit_version = DryRunEmit("run", manifest, {"--emit-runtime-observation=3"});
  EXPECT_EQ(explicit_version.at("runtime_observation").dump(), R"({"version":3})");

  json full = DryRunEmit(
      "run", manifest,
      {"--emit-runtime-observation",
       "--runtime-observation-mode", "sampled",
       "--runtime-observation-capability", "task-lifecycle",
       "--runtime-observation-capability", "loss-accounting",
       "--runtime-observation-capability", "task-lifecycle",
       "--runtime-observation-lane-capacity", "512",
       "--runtime-observation-sampling", "1/8@7"});
  const std::string full_cache_key = full.at("__cache_key").get<std::string>();
  full.erase("__cache_key");
  EXPECT_NE(full_cache_key, baseline_cache_key);
  EXPECT_NE(full_cache_key, version_only_cache_key);
  EXPECT_EQ(
      full.at("runtime_observation").dump(),
      R"({"lane_capacity":512,"mode":"sampled",)"
      R"("required_capabilities":["loss-accounting","task-lifecycle"],)"
      R"("sampling":{"denominator":8,"numerator":1,"seed":7},"version":2})");

  // Secondary flags may precede the emission flag; a seedless sampling omits the seed.
  json reordered = DryRunEmit(
      "run", manifest,
      {"--runtime-observation-sampling", "3/16",
       "--runtime-observation-mode", "detailed",
       "--emit-runtime-observation"});
  EXPECT_EQ(
      reordered.at("runtime_observation").dump(),
      R"({"mode":"detailed","sampling":{"denominator":16,"numerator":3},"version":2})");

  // Every plan command accepts the request; the snapshot and observation requests coexist.
  const fs::path tested_root = MakeTempDir("test-runtime-observation-dry-run");
  WriteFile(
      tested_root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(tested_root / "src/main.styio", ">_(\"app\")\n");
  WriteFile(tested_root / "tests/smoke.styio", ">_(\"smoke\")\n");
  const std::string tested_manifest = (tested_root / "pafio.toml").string();
  for (const std::string command : {"check", "build", "test"})
  {
    json emit = DryRunEmit(
        command, tested_manifest,
        {"--emit-runtime-observation", "--runtime-observation-mode", "aggregate",
         "--emit-observable-static-snapshot", "--observable-capability", "alpha"});
    EXPECT_EQ(emit.at("runtime_observation").dump(), R"({"mode":"aggregate","version":2})") << command;
    EXPECT_EQ(
        emit.at("observable_static_snapshot").dump(),
        R"({"required_capabilities":["alpha"],"schema_version":1})")
        << command;
  }

  json baseline_again = DryRunEmit("run", manifest, {});
  EXPECT_EQ(baseline_again.at("__cache_key").get<std::string>(), baseline_cache_key);
  baseline_again.erase("__cache_key");
  EXPECT_EQ(baseline_again.dump(), baseline.dump());
}

TEST(BuildCliTests, RuntimeObservationFlagsRejectMalformedValues)
{
  const fs::path root = MakeTempDir("run-runtime-observation-usage-errors");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);
  const std::string manifest = (root / "pafio.toml").string();

  const auto usage_error = [&](const std::vector<std::string> &extra) {
    std::vector<std::string> args = {"--json", "run", "--manifest-path", manifest, "--dry-run"};
    args.insert(args.end(), extra.begin(), extra.end());
    return pafio::RunCli(args) == pafio::kExitUsage;
  };

  // Emission flag values.
  EXPECT_TRUE(usage_error({"--emit-runtime-observation=abc"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation=0"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation="}));

  // Every secondary flag requires the emission flag.
  EXPECT_TRUE(usage_error({"--runtime-observation-mode", "aggregate"}));
  EXPECT_TRUE(usage_error({"--runtime-observation-capability", "task-lifecycle"}));
  EXPECT_TRUE(usage_error({"--runtime-observation-lane-capacity", "256"}));
  EXPECT_TRUE(usage_error({"--runtime-observation-sampling", "1/16"}));

  // Missing values.
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-mode"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-capability"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-lane-capacity"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling"}));

  // Malformed mode.
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-mode", "loud"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-mode", "Aggregate"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-mode", ""}));

  // Empty capability.
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-capability", ""}));

  // Malformed lane capacity (Pafio checks only for a positive integer; Styio owns the bounds).
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-lane-capacity", "0"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-lane-capacity", "-64"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-lane-capacity", "64x"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-lane-capacity", ""}));

  // Malformed sampling.
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "16"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "1/"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "/16"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "0/16"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "1/0"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "1//16"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "1/16@"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "1/16@-1"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "1/16@x"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", "1/16@1@2"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", " 1/16"}));
  EXPECT_TRUE(usage_error({"--emit-runtime-observation", "--runtime-observation-sampling", ""}));

  // Well-formed values still succeed after the rejected ones.
  EXPECT_EQ(
      pafio::RunCli({"--json", "run", "--manifest-path", manifest, "--dry-run",
                     "--emit-runtime-observation", "--runtime-observation-sampling", "1/16@0"}),
      pafio::kExitSuccess);
}

TEST(BuildCliTests, NonDryRunCheckWithObservableStaticSnapshotExposesReceiptThroughPlanPayload)
{
  const fs::path root = MakeTempDir("check-observable-snapshot-live");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteSingleBinProject(root);

  const fs::path fake_styio = root / "fake-styio";
  WriteFakeCompilePlanStyio(fake_styio);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
      "--emit-observable-static-snapshot",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("intent").get<std::string>(), "check");
  EXPECT_EQ(payload.at("status").get<std::string>(), "succeeded");

  const fs::path build_root = payload.at("plan").at("build_root").get<std::string>();
  const fs::path artifact_dir = payload.at("plan").at("artifact_dir").get<std::string>();
  EXPECT_TRUE(fs::is_directory(artifact_dir));
  ASSERT_TRUE(fs::exists(build_root / "receipt.json"));
  const json receipt = json::parse(ReadFile(build_root / "receipt.json"));
  EXPECT_EQ(receipt.at("outputs").at("artifact_dir").get<std::string>(), artifact_dir.string());

  const json plan = json::parse(ReadFile(payload.at("plan").at("path").get<std::string>()));
  EXPECT_EQ(
      plan.at("emit").at("observable_static_snapshot").dump(),
      R"({"required_capabilities":[],"schema_version":1})");
}

TEST(BuildCliTests, SyncCompletesBeforeStyioProbeAndExplicitBinaryWinsOverEnvironment)
{
  const fs::path root = MakeTempDir("build-sync-before-styio");
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
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"vendor/util\" }\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "vendor/util/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/util\"\n"
      "version = \"0.2.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "vendor/util/src/lib.styio", "# util := true\n");

  const fs::path explicit_marker = root / "explicit-probed";
  const fs::path environment_marker = root / "environment-probed";
  const fs::path explicit_styio = root / "explicit-styio";
  const fs::path environment_styio = root / "environment-styio";
  WriteExecutable(
      environment_styio,
      "#!/bin/sh\n"
      "printf '%s\\n' environment > \"" + environment_marker.string() + "\"\n"
      "exit 70\n");
  WriteExecutable(
      explicit_styio,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  test -f \"" + (root / "pafio.lock").string() + "\" || exit 71\n"
      "  test -f \"" + (root / ".pafio/resolution-v1.json").string() + "\" || exit 72\n"
      "  printf '%s\\n' explicit > \"" + explicit_marker.string() + "\"\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":false},\"supported_contracts\":{\"compile_plan\":[1]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n" +
          pafio::testsupport::FakeCompilePlanConsumerBody() +
          "exit 64\n");
  const ScopedEnvVar environment_styio_bin("PAFIO_STYIO_BIN", environment_styio.string());

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "build",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      explicit_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("action").get<std::string>(), "build");
  EXPECT_EQ(payload.at("status").get<std::string>(), "succeeded");
  EXPECT_EQ(payload.at("intent").get<std::string>(), "build");
  EXPECT_EQ(payload.at("sync").at("status").get<std::string>(), "succeeded");
  EXPECT_EQ(payload.at("styio").at("process").at("status").get<std::string>(), "exited");
  EXPECT_EQ(payload.at("styio").at("process").at("exit_code").get<int>(), 0);
  EXPECT_TRUE(fs::exists(root / "pafio.lock"));
  EXPECT_TRUE(fs::exists(root / ".pafio/resolution-v1.json"));
  EXPECT_TRUE(fs::exists(explicit_marker));
  EXPECT_FALSE(fs::exists(environment_marker));
}
