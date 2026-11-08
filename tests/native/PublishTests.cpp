#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioPublish/Publish.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "BuildTestSupport.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::WriteFile;

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
      "[build]\n"
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
      "[build]\n"
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
      "[build]\n"
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
      "[build]\n"
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

TEST(PublishTests, RemoteRequestCarriesBoundedArchiveContentWithoutLocalPaths)
{
  const fs::path root = MakeTempDir("remote-request");
  const fs::path archive_path = root / "acme-app-0.1.0.pafio.src.tar";
  WriteFile(archive_path, "archive");

  const json request = json::parse(pafio::BuildRemotePublishRequestJson({
      .archive_path = archive_path,
      .package_name = "acme/app",
      .package_version = "0.1.0",
  }));

  EXPECT_EQ(request.size(), 9U);
  EXPECT_EQ(request.at("package").get<std::string>(), "acme/app");
  EXPECT_EQ(request.at("version").get<std::string>(), "0.1.0");
  EXPECT_EQ(request.at("archive_name").get<std::string>(), "acme-app-0.1.0.pafio.src.tar");
  EXPECT_EQ(request.at("archive_base64").get<std::string>(), "YXJjaGl2ZQ==");
  EXPECT_EQ(
      request.at("archive_sha256").get<std::string>(),
      "0eb3e36bfb24dcd9bb1d1bece1531216b59539a8fde17ee80224af0653c92aa3");
  EXPECT_EQ(request.at("archive_size_bytes").get<std::uintmax_t>(), 7U);
  EXPECT_EQ(request.at("publisher_id").get<std::string>(), "pafio-cli");
  EXPECT_TRUE(request.at("dependencies").empty());
  EXPECT_TRUE(request.at("dev_dependencies").empty());
  EXPECT_FALSE(request.contains("archive_path"));
  EXPECT_FALSE(request.contains("manifest_path"));
  EXPECT_FALSE(request.contains("package_root"));
}

TEST(PublishTests, RemoteRequestRejectsArchiveAboveExplicitLimit)
{
  const fs::path root = MakeTempDir("remote-request-limit");
  const fs::path archive_path = root / "package.pafio.src.tar";
  WriteFile(archive_path, "1234");

  EXPECT_THROW(
      pafio::BuildRemotePublishRequestJson(
          {
              .archive_path = archive_path,
              .package_name = "acme/app",
              .package_version = "0.1.0",
          },
          3U),
      pafio::PublishError);
}

TEST(PublishTests, RemoteRequestSerializesCanonicalDependencyMetadata)
{
  const fs::path root = MakeTempDir("remote-request-dependencies");
  const fs::path archive_path = root / "package.pafio.src.tar";
  WriteFile(archive_path, "archive");

  const json request = json::parse(pafio::BuildRemotePublishRequestJson({
      .archive_path = archive_path,
      .package_name = "acme/app",
      .package_version = "0.1.0",
      .dependencies = {
          {
              .alias = "zeta",
              .package = "acme/zeta",
              .source_kind = pafio::DependencySourceKind::kRegistry,
              .source = "https://packages.example.test",
              .version = "2.0.0",
          },
          {
              .alias = "alpha",
              .package = "acme/alpha",
              .source_kind = pafio::DependencySourceKind::kRegistry,
              .source = "https://packages.example.test",
              .version = "1.0.0",
          },
      },
      .dev_dependencies = {
          {
              .alias = "fixture",
              .package = "acme/fixture",
              .source_kind = pafio::DependencySourceKind::kRegistry,
              .source = "https://packages.example.test",
              .version = "3.0.0",
          },
      },
  }));

  ASSERT_EQ(request.at("dependencies").size(), 2U);
  EXPECT_EQ(
      request.at("dependencies")[0],
      json({
          {"alias", "alpha"},
          {"package", "acme/alpha"},
          {"version_req", "1.0.0"},
          {"registry", "https://packages.example.test"},
      }));
  EXPECT_EQ(request.at("dependencies")[1].at("alias").get<std::string>(), "zeta");
  ASSERT_EQ(request.at("dev_dependencies").size(), 1U);
  EXPECT_EQ(request.at("dev_dependencies")[0].at("alias").get<std::string>(), "fixture");
}

TEST(PublishTests, RemoteRequestRejectsUnboundedDependencyTables)
{
  const fs::path root = MakeTempDir("remote-request-dependency-limit");
  const fs::path archive_path = root / "package.pafio.src.tar";
  WriteFile(archive_path, "archive");

  pafio::PublishResult candidate{
      .archive_path = archive_path,
      .package_name = "acme/app",
      .package_version = "0.1.0",
  };
  candidate.dependencies.resize(257U);

  EXPECT_THROW(pafio::BuildRemotePublishRequestJson(candidate), pafio::PublishError);
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
      "[build]\n"
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
      "[build]\n"
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
      "[build]\n"
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

TEST(PublishCliTests, RejectsFilesystemRegistryBeforePreparingArchive)
{
  const fs::path root = MakeTempDir("publish-filesystem-before-pack");
  EXPECT_EQ(
      pafio::RunCli({
          "publish",
          "--manifest-path",
          (root / "missing.toml").string(),
          "--registry",
          (root / "registry").string(),
      }),
      pafio::kExitUsage);
}
TEST(PublishCliTests, RejectsFilesystemRegistryPath)
{
  const fs::path root = MakeTempDir("publish-filesystem-registry");
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
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(
      pafio::RunCli({
          "publish",
          "--manifest-path",
          (root / "pafio.toml").string(),
          "--registry",
          registry_root.string(),
      }),
      pafio::kExitUsage);
}

TEST(PublishCliTests, RejectsPlaintextHttpRegistryWithoutOptIn)
{
  const fs::path root = MakeTempDir("publish-plaintext-http");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(
      pafio::RunCli({
          "publish",
          "--manifest-path",
          (root / "pafio.toml").string(),
          "--registry",
          "http://upload.example.test",
      }),
      pafio::kExitPublish);
}

TEST(PublishCliTests, RejectsFilesystemRegistryFileUrl)
{
  const fs::path root = MakeTempDir("publish-filesystem-registry-file-url");
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
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");

  EXPECT_EQ(
      pafio::RunCli({
          "publish",
          "--manifest-path",
          (root / "pafio.toml").string(),
          "--registry",
          registry_url,
      }),
      pafio::kExitUsage);
}
