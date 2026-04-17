#include "SpioCLI/CLI.hpp"
#include "SpioCore/Errors.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

class ScopedEnvVar
{
public:
  ScopedEnvVar(const std::string &name, const std::string &value)
      : name_(name)
  {
    if (const char *existing = std::getenv(name.c_str()); existing != nullptr)
    {
      had_previous_ = true;
      previous_value_ = existing;
    }
    setenv(name.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar()
  {
    if (had_previous_)
    {
      setenv(name_.c_str(), previous_value_.c_str(), 1);
    }
    else
    {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  bool had_previous_ = false;
  std::string previous_value_;
};

fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "spio-native-project-graph-tests" / label;
  fs::remove_all(root);
  fs::create_directories(root);
  return root;
}

void WriteFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  ASSERT_TRUE(out.good());
  out << content;
  ASSERT_TRUE(out.good());
}

void WriteExecutable(const fs::path &path, const std::string &content)
{
  WriteFile(path, content);
  fs::permissions(
      path,
      fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
      fs::perm_options::add);
}

void WriteLiveFakeStyio(const fs::path &path, const std::string &version = "0.0.5")
{
  WriteExecutable(
      path,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"" +
          version +
          "\",\"channel\":\"stable\",\"variant\":\"full\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contracts\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[]},\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":false},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
          "  exit 0\n"
          "fi\n"
          "echo unexpected invocation >&2\n"
          "exit 64\n");
}

}  // namespace

TEST(ProjectGraphTests, EmitsStructuredGraphPayloadForCombinedRoot)
{
  const fs::path root = MakeTempDir("combined-root");
  const fs::path fake_styio = root / "fake-styio";
  WriteLiveFakeStyio(fake_styio);

  WriteFile(
      root / "spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[workspace]\n"
      "members = [\"packages/render-kit\"]\n"
      "resolver = \"1\"\n\n"
      "[toolchain]\n"
      "channel = \"stable\"\n"
      "implicit-std = true\n\n"
      "[dependencies]\n"
      "render-kit = { path = \"packages/render-kit\" }\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "packages/render-kit/spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/render-kit\"\n"
      "version = \"0.2.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"stable\"\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "packages/render-kit/src/lib.styio", "# render := 1\n");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "project-graph",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "project-graph");
  EXPECT_EQ(payload.at("schema_version").get<int>(), 1);
  EXPECT_EQ(payload.at("kind").get<std::string>(), "combined-root");
  ASSERT_EQ(payload.at("workspace_members").size(), 1U);
  EXPECT_EQ(payload.at("workspace_members")[0].get<std::string>(), "packages/render-kit");
  ASSERT_EQ(payload.at("packages").size(), 2U);
  ASSERT_EQ(payload.at("targets").size(), 2U);
  EXPECT_EQ(payload.at("toolchain").at("source").get<std::string>(), "environment");
  ASSERT_TRUE(payload.at("active_compiler").is_object());
  EXPECT_EQ(payload.at("active_compiler").at("compiler_version").get<std::string>(), "0.0.5");
  EXPECT_EQ(payload.at("active_compiler").at("feature_flags").at("compile_plan_consumer").get<bool>(), true);
  EXPECT_EQ(payload.at("vendor_state").get<std::string>(), "missing");
  ASSERT_FALSE(payload.at("packages").empty());
  EXPECT_EQ(payload.at("packages")[0].at("publish_enabled").get<bool>(), false);
  ASSERT_FALSE(payload.at("dependencies").empty());
  EXPECT_EQ(payload.at("dependencies")[0].at("source_kind").get<std::string>(), "path");
  EXPECT_EQ(payload.at("dependencies")[0].at("path").get<std::string>(), "packages/render-kit");
  EXPECT_EQ(payload.at("dependencies")[0].at("publish_blocking").get<bool>(), true);
  ASSERT_TRUE(payload.at("package_distribution").is_object());
  ASSERT_EQ(payload.at("package_distribution").at("packages").size(), 2U);
  EXPECT_EQ(payload.at("package_distribution").at("publishable_packages").get<size_t>(), 0U);
  EXPECT_EQ(payload.at("package_distribution").at("blocked_packages").get<size_t>(), 2U);
  ASSERT_TRUE(payload.at("source_state").is_object());
  EXPECT_EQ(payload.at("source_state").at("declared_git_dependencies").get<size_t>(), 0U);
  EXPECT_EQ(payload.at("source_state").at("declared_registry_dependencies").get<size_t>(), 0U);
  EXPECT_EQ(payload.at("source_state").at("vendor").at("vendor_present").get<bool>(), false);
  EXPECT_EQ(payload.at("source_state").at("vendor").at("metadata_present").get<bool>(), false);
}

TEST(ProjectGraphTests, ReportsProjectPinWithoutCrashingWhenManagedInstallIsMissing)
{
  const fs::path root = MakeTempDir("missing-managed-pin");
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
      "channel = \"stable\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "spio-toolchain.toml",
      "[styio]\n"
      "channel = \"stable\"\n"
      "version = \"0.0.5\"\n");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "project-graph",
      "--manifest-path",
      (root / "spio.toml").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("toolchain").at("source").get<std::string>(), "project-pin");
  EXPECT_EQ(payload.at("toolchain").at("channel").get<std::string>(), "stable");
  EXPECT_EQ(payload.at("toolchain").at("version").get<std::string>(), "0.0.5");
  EXPECT_TRUE(payload.at("active_compiler").is_null());
  ASSERT_FALSE(payload.at("notes").empty());
  EXPECT_NE(
      payload.at("notes")[0].get<std::string>().find("active compiler is unresolved"),
      std::string::npos);
  EXPECT_NE(
      payload.at("notes")[0].get<std::string>().find("managed styio install that is not present"),
      std::string::npos);
}

TEST(ProjectGraphTests, EmitsDependencySourceAndCacheStateForProjectGraph)
{
  const fs::path root = MakeTempDir("source-state");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  const fs::path manifest_path = root / "spio.toml";
  const fs::path vendor_root = root / ".spio" / "vendor";
  const fs::path vendor_metadata_path = vendor_root / "spio-vendor.json";

  WriteFile(
      manifest_path,
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"stable\"\n"
      "implicit-std = true\n\n"
      "[dependencies]\n"
      "feed = { package = \"acme/feed\", git = \"https://git.example.test/acme/feed.git\", rev = \"deadbeef\" }\n"
      "util = { package = \"acme/util\", version = \"0.2.0\", registry = \"https://packages.example.test\" }\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  fs::create_directories(root / ".spio-home" / "git" / "repos" / "feed.git");
  fs::create_directories(root / ".spio-home" / "git" / "checkouts" / "feed" / "deadbeef");
  fs::create_directories(root / ".spio-home" / "registry" / "index");
  fs::create_directories(root / ".spio-home" / "registry" / "blobs" / "sha256" / "aa" / "bb");
  fs::create_directories(root / ".spio-home" / "registry" / "checkouts" / "acme" / "util" / "0.2.0" / "cafebabe");
  WriteFile(root / ".spio-home" / "registry" / "blobs" / "sha256" / "aa" / "bb" / "artifact.tar", "blob");
  WriteFile(root / ".spio-home" / "registry" / "checkouts" / "acme" / "util" / "0.2.0" / "cafebabe" / ".spio-snapshot-ready", "ready\n");

  fs::create_directories(vendor_root);
  WriteFile(
      vendor_metadata_path,
      "{\n"
      "  \"tool\": \"spio\",\n"
      "  \"version\": \"0.0.0-test\",\n"
      "  \"manifest_path\": \"" + manifest_path.string() + "\",\n"
      "  \"vendor_root\": \"" + vendor_root.string() + "\",\n"
      "  \"git_snapshots\": [\n"
      "    {\n"
      "      \"source\": \"https://git.example.test/acme/feed.git\",\n"
      "      \"rev\": \"deadbeef\",\n"
      "      \"repo_hash\": \"feed\",\n"
      "      \"path\": \"" + (vendor_root / "git" / "feed" / "deadbeef").string() + "\"\n"
      "    }\n"
      "  ]\n"
      "}\n");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "project-graph",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  ASSERT_TRUE(payload.at("source_state").is_object());
  EXPECT_EQ(payload.at("source_state").at("schema_version").get<int>(), 1);
  EXPECT_EQ(payload.at("source_state").at("spio_home").get<std::string>(), (root / ".spio-home").string());
  EXPECT_EQ(payload.at("source_state").at("declared_git_dependencies").get<size_t>(), 1U);
  EXPECT_EQ(payload.at("source_state").at("declared_registry_dependencies").get<size_t>(), 1U);
  EXPECT_EQ(payload.at("source_state").at("git_cache").at("repos_present").get<bool>(), true);
  EXPECT_EQ(payload.at("source_state").at("git_cache").at("checkouts_present").get<bool>(), true);
  EXPECT_EQ(payload.at("source_state").at("registry_cache").at("index_present").get<bool>(), true);
  EXPECT_EQ(payload.at("source_state").at("registry_cache").at("blobs_present").get<bool>(), true);
  EXPECT_EQ(payload.at("source_state").at("registry_cache").at("checkouts_present").get<bool>(), true);
  EXPECT_EQ(payload.at("source_state").at("vendor").at("vendor_present").get<bool>(), true);
  EXPECT_EQ(payload.at("source_state").at("vendor").at("metadata_present").get<bool>(), true);
  EXPECT_EQ(payload.at("source_state").at("vendor").at("git_snapshots").get<size_t>(), 1U);
}

TEST(ProjectGraphTests, EmitsRegistryAndPublishabilitySummaryForPublishablePackage)
{
  const fs::path root = MakeTempDir("publish-ready");

  WriteFile(
      root / "spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"stable\"\n"
      "implicit-std = true\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", version = \"0.2.0\", registry = \"https://packages.example.test\" }\n\n"
      "[dev-dependencies]\n"
      "fixture = { package = \"acme/fixture\", version = \"1.0.0\", registry = \"https://packages.example.test\" }\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "project-graph",
      "--manifest-path",
      (root / "spio.toml").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  ASSERT_EQ(payload.at("dependencies").size(), 2U);
  EXPECT_EQ(payload.at("dependencies")[0].at("source_kind").get<std::string>(), "registry");
  EXPECT_EQ(payload.at("dependencies")[0].at("registry").get<std::string>(), "https://packages.example.test");
  EXPECT_EQ(payload.at("dependencies")[0].at("package").get<std::string>(), "acme/util");
  EXPECT_EQ(payload.at("dependencies")[0].at("version").get<std::string>(), "0.2.0");
  EXPECT_EQ(payload.at("dependencies")[0].at("publish_blocking").get<bool>(), false);

  ASSERT_TRUE(payload.at("package_distribution").is_object());
  ASSERT_EQ(payload.at("package_distribution").at("packages").size(), 1U);
  const json &package_distribution = payload.at("package_distribution").at("packages")[0];
  EXPECT_EQ(package_distribution.at("package_name").get<std::string>(), "acme/app");
  EXPECT_EQ(package_distribution.at("publish_enabled").get<bool>(), true);
  EXPECT_EQ(package_distribution.at("publish_ready").get<bool>(), true);
  EXPECT_TRUE(package_distribution.at("blocking_reasons").empty());
  EXPECT_EQ(package_distribution.at("runtime_registry_dependencies").get<size_t>(), 1U);
  EXPECT_EQ(package_distribution.at("dev_registry_dependencies").get<size_t>(), 1U);

  ASSERT_EQ(payload.at("package_distribution").at("registry_sources").size(), 1U);
  const json &registry_source = payload.at("package_distribution").at("registry_sources")[0];
  EXPECT_EQ(registry_source.at("registry_root").get<std::string>(), "https://packages.example.test");
  EXPECT_EQ(registry_source.at("transport").get<std::string>(), "https");
  EXPECT_EQ(registry_source.at("dependency_refs").get<size_t>(), 2U);
  ASSERT_EQ(registry_source.at("packages").size(), 1U);
  EXPECT_EQ(registry_source.at("packages")[0].get<std::string>(), "acme/app");
}
