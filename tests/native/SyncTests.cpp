#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"

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

fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "pafio-native-sync-tests" / label;
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

void WritePathDependencyProject(const fs::path &root)
{
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"vendor/util\" }\n");
  WriteFile(
      root / "vendor/util/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/util\"\n"
      "version = \"0.2.0\"\n"
      "edition = \"2026\"\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
}

}  // namespace

TEST(SyncCliTests, WritesLockfileAndFetchesDependencyGraph)
{
  const fs::path root = MakeTempDir("sync-write");
  WritePathDependencyProject(root);
  const fs::path manifest_path = root / "pafio.toml";
  const fs::path lockfile_path = root / "pafio.lock";

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json first_payload = json::parse(testing::internal::GetCapturedStdout());

  EXPECT_TRUE(fs::exists(lockfile_path));
  EXPECT_EQ(first_payload.at("command"), "sync");
  EXPECT_EQ(first_payload.at("lockfile_mode"), "write");
  EXPECT_EQ(first_payload.at("packages"), 2);
  EXPECT_EQ(first_payload.at("git_packages"), 0);
  EXPECT_EQ(first_payload.at("registry_packages"), 0);
  EXPECT_FALSE(first_payload.at("locked"));
  EXPECT_FALSE(first_payload.at("offline"));
  EXPECT_NE(ReadFile(lockfile_path).find("name = \"acme/util\""), std::string::npos);

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json second_payload = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_EQ(second_payload.at("lockfile_mode"), "unchanged");
}

TEST(SyncCliTests, LockedSyncRequiresExistingFreshLockfile)
{
  const fs::path root = MakeTempDir("sync-locked");
  WritePathDependencyProject(root);
  const fs::path manifest_path = root / "pafio.toml";

  testing::internal::CaptureStderr();
  EXPECT_EQ(
      pafio::RunCli({"sync", "--locked", "--manifest-path", manifest_path.string()}),
      pafio::kExitLock);
  const std::string missing_lock_error = testing::internal::GetCapturedStderr();
  EXPECT_NE(missing_lock_error.find("lockfile missing"), std::string::npos);

  ASSERT_EQ(pafio::RunCli({"lock", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  EXPECT_EQ(
      pafio::RunCli({"--json", "sync", "--locked", "--manifest-path", manifest_path.string()}),
      pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_EQ(payload.at("lockfile_mode"), "locked");
  EXPECT_TRUE(payload.at("locked"));
  EXPECT_FALSE(payload.at("offline"));
}

TEST(SyncCliTests, FrozenSyncUsesLockedOfflinePolicy)
{
  const fs::path root = MakeTempDir("sync-frozen");
  WritePathDependencyProject(root);
  const fs::path manifest_path = root / "pafio.toml";

  ASSERT_EQ(pafio::RunCli({"lock", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  EXPECT_EQ(
      pafio::RunCli({"--json", "sync", "--frozen", "--manifest-path", manifest_path.string()}),
      pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_EQ(payload.at("lockfile_mode"), "locked");
  EXPECT_TRUE(payload.at("locked"));
  EXPECT_TRUE(payload.at("offline"));
}
