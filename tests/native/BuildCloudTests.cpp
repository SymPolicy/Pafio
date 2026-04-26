#include "BuildTestSupport.hpp"

#include "SpioCLI/CLI.hpp"
#include "SpioCloud/Contract.hpp"
#include "SpioCloud/Execution.hpp"
#include "SpioCloud/Job.hpp"
#include "SpioCore/Errors.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

using spio::testsupport::MakeTempDir;
using spio::testsupport::WriteFile;

TEST(BuildCliTests, DryRunBuildReportsWarmSharedCloudPolicyForTrustedInternalProjects)
{
  const fs::path root = MakeTempDir("build-dry-run-warm-shared-cloud");
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
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);
  ASSERT_EQ(
      spio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);
  ASSERT_EQ(
      spio::RunCli({
          "set",
          "security",
          "as",
          "trusted-warm",
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
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "warm-shared");
  EXPECT_EQ(payload.at("cloud").at("security_profile").get<std::string>(), "trusted-warm");
  EXPECT_EQ(payload.at("cloud").at("worker_pool_key").at("execution_lane").get<std::string>(), "warm-shared");
}

TEST(BuildCliTests, ProjectGraphReportsResolvedPackagesAndCloudPolicy)
{
  const fs::path root = MakeTempDir("project-graph-json");
  WriteFile(
      root / "spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/demo\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"demo\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"demo\")\n");

  ASSERT_EQ(
      spio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);
  ASSERT_EQ(
      spio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "project-graph",
      "--manifest-path",
      (root / "spio.toml").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "project-graph");
  ASSERT_EQ(payload.at("packages").size(), 1U);
  EXPECT_EQ(payload.at("packages")[0].at("name").get<std::string>(), "acme/demo");
  EXPECT_EQ(payload.at("package_distribution").at("total_package_count").get<int>(), 1);
  EXPECT_EQ(payload.at("toolchain").at("mode").get<std::string>(), "binary");
  EXPECT_EQ(payload.at("toolchain").at("cloud").at("execution_lane").get<std::string>(), "warm-shared");
  EXPECT_TRUE(payload.at("source_state").contains("source_origin"));
}

TEST(BuildCliTests, CloudPlanBuildEmitsControlPlaneJobRequest)
{
  const fs::path root = MakeTempDir("cloud-plan-build");
  WriteFile(
      root / "spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/demo\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"demo\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"demo\")\n");

  ASSERT_EQ(
      spio::RunCli({
          "use",
          "build",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);
  ASSERT_EQ(
      spio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);
  ASSERT_EQ(
      spio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          (root / "spio.toml").string(),
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "cloud",
      "plan",
      "build",
      "minimal",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--source-rev",
      "nightly-head",
      "--non-interactive",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "cloud plan");
  const json &job_request = payload.at("job_request");
  EXPECT_EQ(job_request.at("api_path").get<std::string>(), "/api/styio-platform/v1/jobs");
  EXPECT_EQ(job_request.at("action").get<std::string>(), "build");
  EXPECT_EQ(job_request.at("toolchain").at("mode").get<std::string>(), "build");
  EXPECT_EQ(job_request.at("toolchain").at("build_mode").get<std::string>(), "minimal");
  EXPECT_EQ(job_request.at("cloud").at("execution_lane").get<std::string>(), "warm-shared");
  EXPECT_EQ(job_request.at("cloud").at("worker_pool_key").at("toolchain_mode").get<std::string>(), "build");
  EXPECT_TRUE(job_request.at("cloud").at("cache_policy").at("worker_local_reuse").get<bool>());
  EXPECT_EQ(job_request.at("source").at("requested_revision").get<std::string>(), "nightly-head");
  EXPECT_TRUE(job_request.at("source").at("non_interactive").get<bool>());
}

TEST(CloudJobRequestTests, RejectsSourceBuildOverridesWhenProjectUsesBinaryMode)
{
  const spio::ProjectToolchainState state = {
      .manifest_path = "spio.toml",
      .state_path = "spio-toolchain.lock",
      .state_file_exists = true,
      .mode = "binary",
      .channel = "stable",
      .build_mode = "minimal",
  };
  const spio::BuildPlanRequest request = {
      .manifest_path = "spio.toml",
      .intent = "build",
      .profile = "dev",
  };
  const spio::WorkflowInvocationOptions options = {
      .source_revision = std::string("nightly-head"),
  };

  try
  {
    (void) spio::BuildCloudBuildJobRequest("build", request, state, options, spio::ResolveCloudExecutionPolicy(state));
    FAIL() << "expected ValidationError";
  }
  catch (const spio::ValidationError &error)
  {
    EXPECT_EQ(std::string(error.what()), "source-build options require 'spio use build'");
  }
}

TEST(CloudJobRequestTests, RejectsStyioBinaryOverrideWhenProjectUsesBuildMode)
{
  const spio::ProjectToolchainState state = {
      .manifest_path = "spio.toml",
      .state_path = "spio-toolchain.lock",
      .state_file_exists = true,
      .mode = "build",
      .channel = "nightly",
      .build_mode = "minimal",
  };
  const spio::BuildPlanRequest request = {
      .manifest_path = "spio.toml",
      .intent = "build",
      .profile = "dev",
  };
  const spio::WorkflowInvocationOptions options = {
      .styio_bin = std::string("/tmp/styio"),
  };

  try
  {
    (void) spio::BuildCloudBuildJobRequest("build", request, state, options, spio::ResolveCloudExecutionPolicy(state));
    FAIL() << "expected ValidationError";
  }
  catch (const spio::ValidationError &error)
  {
    EXPECT_EQ(std::string(error.what()), "--styio-bin is only valid when the project toolchain mode is 'binary'");
  }
}

TEST(CloudJobRequestTests, RejectsUnsupportedBuildModeBeforeEmittingJobRequest)
{
  const spio::ProjectToolchainState state = {
      .manifest_path = "spio.toml",
      .state_path = "spio-toolchain.lock",
      .state_file_exists = true,
      .mode = "build",
      .channel = "nightly",
      .build_mode = "minimal",
  };
  const spio::BuildPlanRequest request = {
      .manifest_path = "spio.toml",
      .intent = "build",
      .profile = "dev",
      .build_mode = "maximal",
  };
  const spio::WorkflowInvocationOptions options;

  try
  {
    (void) spio::BuildCloudBuildJobRequest("build", request, state, options, spio::ResolveCloudExecutionPolicy(state));
    FAIL() << "expected ValidationError";
  }
  catch (const spio::ValidationError &error)
  {
    EXPECT_EQ(std::string(error.what()), "build currently supports only the 'minimal' mode");
  }
}

TEST(CloudJobRequestTests, FactoryBuildsRequestUsingSharedCloudSerializer)
{
  const fs::path root = MakeTempDir("cloud-job-request-factory");
  const spio::ProjectToolchainState state = {
      .manifest_path = root / "spio.toml",
      .state_path = root / "spio-toolchain.lock",
      .state_file_exists = true,
      .mode = "build",
      .channel = "nightly",
      .build_mode = "minimal",
      .risk_class = "trusted-internal",
      .preferred_execution_lane = "warm-shared",
      .security_profile = "trusted-warm",
      .source_revision = "resolved-rev",
  };
  const spio::CloudExecutionPolicy policy = spio::ResolveCloudExecutionPolicy(state);
  const spio::BuildPlanRequest request = {
      .manifest_path = state.manifest_path,
      .intent = "build",
      .profile = "release",
  };
  const spio::WorkflowInvocationOptions options = {
      .locked = true,
      .non_interactive = true,
      .source_revision = std::string("requested-rev"),
  };

  const spio::CloudBuildJobRequest job_request =
      spio::BuildCloudBuildJobRequest("build", request, state, options, policy);
  const json payload = spio::BuildCloudBuildJobRequestPayload(job_request);

  EXPECT_EQ(job_request.build_mode(), "minimal");
  EXPECT_EQ(payload.at("toolchain").at("build_mode").get<std::string>(), "minimal");
  EXPECT_EQ(payload.at("source").at("requested_revision").get<std::string>(), "requested-rev");
  EXPECT_EQ(payload.at("source").at("resolved_revision").get<std::string>(), "resolved-rev");
  EXPECT_EQ(payload.at("cloud"), spio::SerializeCloudExecutionPolicy(policy));
}
