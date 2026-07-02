#include "BuildTestSupport.hpp"

#include "PafioCLI/CLI.hpp"
#include "PafioCloud/Contract.hpp"
#include "PafioCloud/Execution.hpp"
#include "PafioCloud/Job.hpp"
#include "PafioCore/Errors.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

using pafio::testsupport::MakeTempDir;
using pafio::testsupport::WriteFile;

TEST(BuildCliTests, DryRunBuildReportsWarmSharedCloudPolicyForTrustedInternalProjects)
{
  const fs::path root = MakeTempDir("build-dry-run-warm-shared-cloud");
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

  ASSERT_EQ(
      pafio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);
  ASSERT_EQ(
      pafio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);
  ASSERT_EQ(
      pafio::RunCli({
          "set",
          "security",
          "as",
          "trusted-warm",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "build",
      "minimal",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--dry-run",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "warm-shared");
  EXPECT_EQ(payload.at("cloud").at("security_profile").get<std::string>(), "trusted-warm");
  EXPECT_EQ(payload.at("cloud").at("worker_pool_key").at("execution_lane").get<std::string>(), "warm-shared");
}

TEST(BuildCliTests, ProjectGraphReportsResolvedPackagesAndCloudPolicy)
{
  const fs::path root = MakeTempDir("project-graph-json");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
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
      pafio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);
  ASSERT_EQ(
      pafio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "project-graph",
      "--manifest-path",
      (root / "pafio.toml").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
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
      root / "pafio.toml",
      "[pafio]\n"
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
      pafio::RunCli({
          "use",
          "build",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);
  ASSERT_EQ(
      pafio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);
  ASSERT_EQ(
      pafio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          (root / "pafio.toml").string(),
      }),
      pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "cloud",
      "plan",
      "build",
      "minimal",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--source-rev",
      "nightly-head",
      "--non-interactive",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "cloud plan");
  const json &job_request = payload.at("job_request");
  EXPECT_EQ(job_request.at("api_path").get<std::string>(), "/api/pafio/v1/jobs");
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
  const pafio::ProjectToolchainState state = {
      .manifest_path = "pafio.toml",
      .state_path = "pafio-toolchain.lock",
      .state_file_exists = true,
      .mode = "binary",
      .channel = "stable",
      .build_mode = "minimal",
  };
  const pafio::BuildPlanRequest request = {
      .manifest_path = "pafio.toml",
      .intent = "build",
      .profile = "dev",
  };
  const pafio::WorkflowInvocationOptions options = {
      .source_revision = std::string("nightly-head"),
  };

  try
  {
    (void) pafio::BuildCloudBuildJobRequest("build", request, state, options, pafio::ResolveCloudExecutionPolicy(state));
    FAIL() << "expected ValidationError";
  }
  catch (const pafio::ValidationError &error)
  {
    EXPECT_EQ(std::string(error.what()), "source-build options require 'pafio use build'");
  }
}

TEST(CloudJobRequestTests, RejectsStyioBinaryOverrideWhenProjectUsesBuildMode)
{
  const pafio::ProjectToolchainState state = {
      .manifest_path = "pafio.toml",
      .state_path = "pafio-toolchain.lock",
      .state_file_exists = true,
      .mode = "build",
      .channel = "nightly",
      .build_mode = "minimal",
  };
  const pafio::BuildPlanRequest request = {
      .manifest_path = "pafio.toml",
      .intent = "build",
      .profile = "dev",
  };
  const pafio::WorkflowInvocationOptions options = {
      .styio_bin = std::string("/tmp/styio"),
  };

  try
  {
    (void) pafio::BuildCloudBuildJobRequest("build", request, state, options, pafio::ResolveCloudExecutionPolicy(state));
    FAIL() << "expected ValidationError";
  }
  catch (const pafio::ValidationError &error)
  {
    EXPECT_EQ(std::string(error.what()), "--styio-bin is only valid when the project toolchain mode is 'binary'");
  }
}

TEST(CloudJobRequestTests, RejectsUnsupportedBuildModeBeforeEmittingJobRequest)
{
  const pafio::ProjectToolchainState state = {
      .manifest_path = "pafio.toml",
      .state_path = "pafio-toolchain.lock",
      .state_file_exists = true,
      .mode = "build",
      .channel = "nightly",
      .build_mode = "minimal",
  };
  const pafio::BuildPlanRequest request = {
      .manifest_path = "pafio.toml",
      .intent = "build",
      .profile = "dev",
      .build_mode = "maximal",
  };
  const pafio::WorkflowInvocationOptions options;

  try
  {
    (void) pafio::BuildCloudBuildJobRequest("build", request, state, options, pafio::ResolveCloudExecutionPolicy(state));
    FAIL() << "expected ValidationError";
  }
  catch (const pafio::ValidationError &error)
  {
    EXPECT_EQ(std::string(error.what()), "build currently supports only the 'minimal' mode");
  }
}

TEST(CloudJobRequestTests, FactoryBuildsRequestUsingSharedCloudSerializer)
{
  const fs::path root = MakeTempDir("cloud-job-request-factory");
  const pafio::ProjectToolchainState state = {
      .manifest_path = root / "pafio.toml",
      .state_path = root / "pafio-toolchain.lock",
      .state_file_exists = true,
      .mode = "build",
      .channel = "nightly",
      .build_mode = "minimal",
      .risk_class = "trusted-internal",
      .preferred_execution_lane = "warm-shared",
      .security_profile = "trusted-warm",
      .source_revision = "resolved-rev",
  };
  const pafio::CloudExecutionPolicy policy = pafio::ResolveCloudExecutionPolicy(state);
  const pafio::BuildPlanRequest request = {
      .manifest_path = state.manifest_path,
      .intent = "build",
      .profile = "release",
  };
  const pafio::WorkflowInvocationOptions options = {
      .locked = true,
      .non_interactive = true,
      .source_revision = std::string("requested-rev"),
  };

  const pafio::CloudBuildJobRequest job_request =
      pafio::BuildCloudBuildJobRequest("build", request, state, options, policy);
  const json payload = pafio::BuildCloudBuildJobRequestPayload(job_request);

  EXPECT_EQ(job_request.build_mode(), "minimal");
  EXPECT_EQ(payload.at("toolchain").at("build_mode").get<std::string>(), "minimal");
  EXPECT_EQ(payload.at("source").at("requested_revision").get<std::string>(), "requested-rev");
  EXPECT_EQ(payload.at("source").at("resolved_revision").get<std::string>(), "resolved-rev");
  EXPECT_EQ(payload.at("cloud"), pafio::SerializeCloudExecutionPolicy(policy));
}
