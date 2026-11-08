#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "BuildTestSupport.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using pafio::testsupport::CanonicalAbsolutePath;
using pafio::testsupport::CreateWorkspaceGitRepo;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::ScopedEnvVar;
using pafio::testsupport::WriteFile;

TEST(VendorCliTests, WritesProjectLocalVendorSnapshotsAndMetadata)
{
  const fs::path root = MakeTempDir("vendor-write");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
      std::string(
          "[pafio]\n"
          "manifest-version = 1\n\n"
          "[package]\n"
          "name = \"acme/app\"\n"
          "version = \"0.1.0\"\n"
          "edition = \"2026\"\n\n"
          "[build]\n"
          "implicit-std = true\n\n"
          "[[bin]]\n"
          "name = \"app\"\n"
          "path = \"src/main.styio\"\n\n"
          "[dependencies]\n"
          "feed = { package = \"acme/feed\", git = \"") +
          CanonicalAbsolutePath(git_repo).generic_string() +
          "\", rev = \"" + rev + "\" }\n");

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "vendor", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());

  const fs::path vendor_root = root / ".pafio" / "vendor";
  EXPECT_EQ(payload.at("command").get<std::string>(), "vendor");
  EXPECT_EQ(payload.at("vendor_root").get<std::string>(), CanonicalAbsolutePath(vendor_root).string());
  EXPECT_EQ(payload.at("git_snapshots").get<size_t>(), 1U);
  EXPECT_TRUE(fs::exists(vendor_root / "pafio-vendor.json"));

  const json metadata = json::parse(ReadFile(vendor_root / "pafio-vendor.json"));
  ASSERT_EQ(metadata.at("git_snapshots").size(), 1U);
  EXPECT_EQ(metadata.at("git_snapshots")[0].at("rev").get<std::string>(), rev);
  EXPECT_TRUE(fs::exists(metadata.at("git_snapshots")[0].at("path").get<std::string>()));
}

TEST(SyncCliTests, OfflineUsesVendoredSnapshotsWithoutGlobalCache)
{
  const fs::path root = MakeTempDir("vendor-offline-fetch");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
      std::string(
          "[pafio]\n"
          "manifest-version = 1\n\n"
          "[package]\n"
          "name = \"acme/app\"\n"
          "version = \"0.1.0\"\n"
          "edition = \"2026\"\n\n"
          "[build]\n"
          "implicit-std = true\n\n"
          "[[bin]]\n"
          "name = \"app\"\n"
          "path = \"src/main.styio\"\n\n"
          "[dependencies]\n"
          "feed = { package = \"acme/feed\", git = \"") +
          CanonicalAbsolutePath(git_repo).generic_string() +
          "\", rev = \"" + rev + "\" }\n");

  ASSERT_EQ(pafio::RunCli({"vendor", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  fs::remove_all(root / ".pafio-home");

  testing::internal::CaptureStdout();
  EXPECT_EQ(
      pafio::RunCli({"--json", "sync", "--offline", "--manifest-path", manifest_path.string()}),
      pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());

  EXPECT_TRUE(payload.at("offline").get<bool>());
  EXPECT_EQ(payload.at("packages").get<size_t>(), 3U);
  EXPECT_EQ(payload.at("git_packages").get<size_t>(), 2U);
}

TEST(CheckCliTests, LockedRequiresAdjacentLockfile)
{
  const fs::path root = MakeTempDir("locked-check-missing-lock");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");

  EXPECT_EQ(pafio::RunCli({"check", "--locked", "--manifest-path", manifest_path.string()}), pafio::kExitLock);
}

TEST(BuildCliTests, FrozenDryRunRequiresAdjacentLockfile)
{
  const fs::path root = MakeTempDir("frozen-build-missing-lock");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/demo\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "src/lib.styio", "# value := 1\n");

  EXPECT_EQ(pafio::RunCli({"build", "--dry-run", "--frozen", "--manifest-path", manifest_path.string(), "--lib"}), pafio::kExitLock);
}
