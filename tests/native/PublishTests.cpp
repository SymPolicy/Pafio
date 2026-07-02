#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioPublish/Publish.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

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
  const fs::path root = fs::temp_directory_path() / "pafio-native-publish-tests" / label;
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

std::string ReadFile(const fs::path &path)
{
  std::ifstream in(path);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

json ReadSingleJsonLineFile(const fs::path &path)
{
  std::istringstream in(ReadFile(path));
  std::string line;
  while (std::getline(in, line))
  {
    if (!line.empty())
    {
      return json::parse(line);
    }
  }
  throw std::runtime_error("jsonl file did not contain a record: " + path.string());
}

}  // namespace

TEST(PublishTests, DryRunPreparesArchiveForPublishablePackage)
{
  const fs::path root = MakeTempDir("publishable-package");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "publish",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--dry-run",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "publish");
  EXPECT_EQ(payload.at("mode").get<std::string>(), "dry-run");
  EXPECT_EQ(payload.at("package").get<std::string>(), "acme/app");
  EXPECT_EQ(payload.at("dependencies").get<size_t>(), 0U);
  EXPECT_EQ(payload.at("dev_dependencies").get<size_t>(), 0U);
  EXPECT_TRUE(fs::exists(payload.at("archive_path").get<std::string>()));
}

TEST(PublishTests, RejectsPublishFalsePackage)
{
  const fs::path root = MakeTempDir("publish-false");
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
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_THROW(
      pafio::PreparePublishCandidate({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PublishError);
}

TEST(PublishTests, RejectsNonRegistryDependenciesForPublish)
{
  const fs::path root = MakeTempDir("publish-with-deps");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"vendor/util\" }\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_THROW(
      pafio::PreparePublishCandidate({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PublishError);
}

TEST(PublishTests, AllowsRegistryDependenciesForPublish)
{
  const fs::path root = MakeTempDir("publish-with-registry-deps");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", version = \"0.2.0\", registry = \"https://packages.example.test\" }\n\n"
      "[dev-dependencies]\n"
      "fixture = { package = \"acme/fixture\", version = \"1.0.0\", registry = \"https://packages.example.test\" }\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  const pafio::PublishResult result = pafio::PreparePublishCandidate({
      .manifest_path = root / "pafio.toml",
  });
  EXPECT_EQ(result.package_name, "acme/app");
  EXPECT_EQ(result.dependencies.size(), 1U);
  EXPECT_EQ(result.dev_dependencies.size(), 1U);
  EXPECT_TRUE(fs::exists(result.archive_path));
}

TEST(PublishTests, WorkspacePublishRequiresExplicitPackageSelectionWhenAmbiguous)
{
  const fs::path root = MakeTempDir("workspace-publish-ambiguity");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[workspace]\n"
      "members = [\"packages/app\", \"packages/tool\"]\n"
      "resolver = \"1\"\n");
  WriteFile(
      root / "packages/app/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "packages/app/src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "packages/tool/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/tool\"\n"
      "version = \"0.2.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"tool\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "packages/tool/src/main.styio", ">_(\"tool\")\n");

  EXPECT_THROW(
      pafio::PreparePublishCandidate({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PublishError);

  const pafio::PublishResult result = pafio::PreparePublishCandidate({
      .manifest_path = root / "pafio.toml",
      .package_name = "acme/tool",
  });
  EXPECT_EQ(result.package_name, "acme/tool");
  EXPECT_TRUE(fs::exists(result.archive_path));
}

TEST(PublishCliTests, NonDryRunPublishRequiresExplicitRegistryRoot)
{
  const fs::path root = MakeTempDir("publish-missing-registry");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, PublishesToFilesystemRegistry)
{
  const fs::path root = MakeTempDir("publish-filesystem-registry");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry_root = root / "registry";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "publish",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--registry",
      registry_root.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "publish");
  EXPECT_EQ(payload.at("mode").get<std::string>(), "publish");
  EXPECT_EQ(payload.at("registry_protocol").get<std::string>(), "v2");
  EXPECT_EQ(payload.at("package").get<std::string>(), "acme/app");
  EXPECT_TRUE(fs::exists(payload.at("registry_config_path").get<std::string>()));
  EXPECT_TRUE(fs::exists(payload.at("registry_index_path").get<std::string>()));
  EXPECT_TRUE(fs::exists(payload.at("registry_artifact_path").get<std::string>()));
  EXPECT_TRUE(fs::exists(payload.at("registry_log_leaf_path").get<std::string>()));

  const json entry = ReadSingleJsonLineFile(payload.at("registry_index_path").get<std::string>());
  EXPECT_EQ(entry.at("schema_version").get<int>(), 1);
  EXPECT_EQ(entry.at("package").get<std::string>(), "acme/app");
  EXPECT_EQ(entry.at("version").get<std::string>(), "0.1.0");
  EXPECT_EQ(entry.at("source_artifact").at("sha256").get<std::string>(), payload.at("sha256").get<std::string>());
  EXPECT_EQ(entry.at("dependencies").size(), 0U);
  EXPECT_EQ(entry.at("dev_dependencies").size(), 0U);
}

TEST(PublishCliTests, PublishesToFilesystemRegistryViaFileUrl)
{
  const fs::path root = MakeTempDir("publish-filesystem-registry-file-url");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry_root = root / "registry";
  const std::string registry_url = std::string("file://") + registry_root.string();
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "publish",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--registry",
      registry_url,
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("transport").get<std::string>(), "filesystem");
  EXPECT_EQ(payload.at("registry_protocol").get<std::string>(), "v2");
  EXPECT_TRUE(fs::exists(payload.at("registry_index_path").get<std::string>()));
}

TEST(PublishCliTests, PublishesRegistryDependencyMetadataToFilesystemRegistry)
{
  const fs::path root = MakeTempDir("publish-registry-dependency-metadata");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry_root = root / "registry";
  const std::string registry_url = std::string("file://") + registry_root.string();
  WriteFile(
      root / "pafio.toml",
      std::string(
          "[pafio]\n"
          "manifest-version = 1\n\n"
          "[package]\n"
          "name = \"acme/app\"\n"
          "version = \"0.1.0\"\n"
          "edition = \"2026\"\n"
          "publish = true\n\n"
          "[toolchain]\n"
          "channel = \"nightly\"\n"
          "implicit-std = true\n\n"
          "[[bin]]\n"
          "name = \"app\"\n"
          "path = \"src/main.styio\"\n\n"
          "[dependencies]\n"
          "util = { package = \"acme/util\", version = \"0.2.0\", registry = \"") +
          registry_url +
          "\" }\n\n"
          "[dev-dependencies]\n"
          "fixture = { package = \"acme/fixture\", version = \"1.0.0\", registry = \"" +
          registry_url +
          "\" }\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "publish",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--registry",
      registry_root.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  const json entry = ReadSingleJsonLineFile(payload.at("registry_index_path").get<std::string>());
  ASSERT_EQ(entry.at("dependencies").size(), 1U);
  EXPECT_EQ(entry.at("dependencies")[0].at("alias").get<std::string>(), "util");
  EXPECT_EQ(entry.at("dependencies")[0].at("package").get<std::string>(), "acme/util");
  EXPECT_EQ(entry.at("dependencies")[0].at("version_req").get<std::string>(), "0.2.0");
  EXPECT_EQ(entry.at("dependencies")[0].at("registry").get<std::string>(), registry_url);
  ASSERT_EQ(entry.at("dev_dependencies").size(), 1U);
  EXPECT_EQ(entry.at("dev_dependencies")[0].at("alias").get<std::string>(), "fixture");
  EXPECT_EQ(entry.at("dev_dependencies")[0].at("package").get<std::string>(), "acme/fixture");
}

TEST(PublishCliTests, RejectsRepublishingExistingFilesystemRegistryVersion)
{
  const fs::path root = MakeTempDir("publish-duplicate-version");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry_root = root / "registry";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_root.string(),
            }),
            pafio::kExitSuccess);

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_root.string(),
            }),
            pafio::kExitPublish);
}

TEST(PublishCliTests, RejectsRegistryHeaderForFilesystemRegistry)
{
  const fs::path root = MakeTempDir("publish-filesystem-registry-header");
  const fs::path registry_root = root / "registry";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_root.string(),
                "--registry-header",
                "X-Pafio-Write-Token: example-write-token",
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsRegistryHeaderInDryRun)
{
  const fs::path root = MakeTempDir("publish-dry-run-header");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--dry-run",
                "--registry-header",
                "X-Pafio-Write-Token: example-write-token",
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsMalformedRegistryHeader)
{
  const fs::path root = MakeTempDir("publish-malformed-header");
  const std::string registry_url = "https://packages.example.test";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_url,
                "--registry-header",
                "X-Pafio-Write-Token",
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsRegistryHeaderForRemoteRegistryWithoutPrivateSecurityModule)
{
  const fs::path root = MakeTempDir("publish-remote-header-requires-private");
  const std::string registry_url = "https://packages.example.test";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_url,
                "--registry-header",
                "X-Pafio-Write-Token: example-write-token",
            }),
            pafio::kExitPublish);
}

TEST(PublishCliTests, RejectsRegistryPolicyFileForFilesystemRegistry)
{
  const fs::path root = MakeTempDir("publish-filesystem-policy");
  const fs::path registry_root = root / "registry";
  const fs::path policy_path = root / "publish-policy.toml";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(
      policy_path,
      "schema-version = 1\n\n"
      "[[registry]]\n"
      "root = \"https://packages.example.test\"\n"
      "headers = [\"X-Pafio-Write-Token: example-write-token\"]\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_root.string(),
                "--registry-policy-file",
                policy_path.string(),
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsRegistryPolicyFileInDryRun)
{
  const fs::path root = MakeTempDir("publish-dry-run-policy");
  const fs::path policy_path = root / "publish-policy.toml";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(
      policy_path,
      "schema-version = 1\n\n"
      "[[registry]]\n"
      "root = \"https://packages.example.test\"\n"
      "headers = [\"X-Pafio-Write-Token: example-write-token\"]\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--dry-run",
                "--registry-policy-file",
                policy_path.string(),
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsRegistryPolicyFileForRemoteRegistryWithoutPrivateSecurityModule)
{
  const fs::path root = MakeTempDir("publish-remote-policy-requires-private");
  const std::string registry_url = "https://packages.example.test";
  const fs::path policy_path = root / "publish-policy.toml";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(
      policy_path,
      "schema-version = 2\n\n"
      "[[registry]]\n"
      "root = \"https://packages.example.test\"\n"
      "headers = [\"X-Pafio-Write-Token: example-write-token\"]\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_url,
                "--registry-policy-file",
                policy_path.string(),
            }),
            pafio::kExitPublish);
}

TEST(PublishCliTests, RejectsRegistryProfileForFilesystemRegistry)
{
  const fs::path root = MakeTempDir("publish-filesystem-profile");
  const fs::path registry_root = root / "registry";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_root.string(),
                "--registry-profile",
                "dev",
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsRegistryProfileInDryRun)
{
  const fs::path root = MakeTempDir("publish-dry-run-profile");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--dry-run",
                "--registry-profile",
                "dev",
            }),
            pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsRegistryProfileForRemoteRegistryWithoutPrivateSecurityModule)
{
  const fs::path root = MakeTempDir("publish-remote-profile-requires-private");
  const fs::path pafio_home = root / ".pafio-home";
  const fs::path profile_path = pafio_home / "server/registry/publish-profiles/dev.toml";
  const char *previous_pafio_home = std::getenv("PAFIO_HOME");
  const std::string previous_pafio_home_value = previous_pafio_home != nullptr ? previous_pafio_home : "";
  setenv("PAFIO_HOME", pafio_home.string().c_str(), 1);

  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(
      profile_path,
      "schema-version = 1\n\n"
      "[[registry]]\n"
      "root = \"https://packages.example.test\"\n"
      "headers = [\"X-Pafio-Write-Token: example-write-token\"]\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                "https://packages.example.test",
                "--registry-profile",
                "dev",
            }),
            pafio::kExitPublish);

  if (previous_pafio_home != nullptr)
  {
    setenv("PAFIO_HOME", previous_pafio_home_value.c_str(), 1);
  }
  else
  {
    unsetenv("PAFIO_HOME");
  }
}

TEST(PublishCliTests, RejectsRegistryProfileWhenPolicyFileAlsoProvided)
{
  const fs::path root = MakeTempDir("publish-profile-policy-conflict");
  const std::string registry_url = "https://packages.example.test";
  const fs::path policy_path = root / "publish-policy.toml";
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(
      policy_path,
      "schema-version = 1\n\n"
      "[[registry]]\n"
      "root = \"https://packages.example.test\"\n"
      "headers = [\"X-Pafio-Write-Token: example-write-token\"]\n");

  EXPECT_EQ(pafio::RunCli({
                "publish",
                "--manifest-path",
                (root / "pafio.toml").string(),
                "--registry",
                registry_url,
                "--registry-profile",
                "dev",
                "--registry-policy-file",
                policy_path.string(),
            }),
            pafio::kExitUsage);
}
