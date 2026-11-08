#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/Sha256.hpp"
#include "PafioManifest/Manifest.hpp"
#include "BuildTestSupport.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using pafio::testsupport::ScopedEnvVar;

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
      "[build]\n"
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
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[dependencies]\n"
      "base = { package = \"acme/base\", path = \"../base\" }\n");
  WriteFile(
      root / "vendor/base/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/base\"\n"
      "version = \"0.3.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
}

}  // namespace

TEST(SyncCliTests, WritesLockfileAndFetchesDependencyGraph)
{
  const fs::path root = MakeTempDir("sync-write");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WritePathDependencyProject(root);
  const fs::path manifest_path = root / "pafio.toml";
  const fs::path lockfile_path = root / "pafio.lock";
  const fs::path resolution_path = root / ".pafio/resolution-v1.json";

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json first_payload = json::parse(testing::internal::GetCapturedStdout());

  EXPECT_TRUE(fs::exists(lockfile_path));
  EXPECT_TRUE(fs::exists(resolution_path));
  EXPECT_EQ(first_payload.at("command"), "sync");
  EXPECT_EQ(first_payload.at("lockfile_mode"), "write");
  EXPECT_EQ(first_payload.at("packages"), 3);
  EXPECT_EQ(first_payload.at("git_packages"), 0);
  EXPECT_EQ(first_payload.at("registry_packages"), 0);
  EXPECT_FALSE(first_payload.at("locked"));
  EXPECT_FALSE(first_payload.at("offline"));
  EXPECT_NE(ReadFile(lockfile_path).find("name = \"acme/util\""), std::string::npos);
  EXPECT_NE(ReadFile(lockfile_path).find("name = \"acme/base\""), std::string::npos);
  EXPECT_EQ(ReadFile(lockfile_path).find(fs::absolute(root).lexically_normal().string()), std::string::npos);

  const std::string first_lock = ReadFile(lockfile_path);
  const std::string first_resolution = ReadFile(resolution_path);
  const json resolution = json::parse(first_resolution);
  EXPECT_EQ(resolution.at("schema_version"), 1);
  EXPECT_EQ(
      resolution.at("manifest_sha256"),
      pafio::Sha256Text(pafio::SerializeManifestCanonical(pafio::LoadManifest(manifest_path))));
  EXPECT_EQ(resolution.at("lock_sha256"), pafio::Sha256Text(first_lock));
  EXPECT_EQ(
      resolution.at("roots"),
      json::array({"workspace:acme/app@0.1.0"}));

  const json &packages = resolution.at("packages");
  ASSERT_EQ(packages.size(), 3U);
  std::vector<std::string> ids;
  for (const json &package : packages)
  {
    ids.push_back(package.at("id").get<std::string>());
    EXPECT_TRUE(package.at("content_sha256").is_null());
    const fs::path package_root = package.at("root").get<std::string>();
    EXPECT_TRUE(package_root.is_absolute());
    EXPECT_TRUE(fs::is_directory(package_root));
  }
  EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));

  const auto find_package = [&](const std::string &id) -> const json * {
    const auto it = std::find_if(
        packages.begin(),
        packages.end(),
        [&](const json &package) {
          return package.at("id") == id;
        });
    return it == packages.end() ? nullptr : &*it;
  };
  const json *app_package = find_package("workspace:acme/app@0.1.0");
  const json *util_package = find_package("path:acme/util@0.2.0");
  ASSERT_NE(app_package, nullptr);
  ASSERT_NE(util_package, nullptr);
  EXPECT_EQ(
      app_package->at("dependencies"),
      json::array({
          {
              {"alias", "util"},
              {"package_id", "path:acme/util@0.2.0"},
          },
      }));
  EXPECT_EQ(
      util_package->at("dependencies"),
      json::array({
          {
              {"alias", "base"},
              {"package_id", "path:acme/base@0.3.0"},
          },
      }));

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json second_payload = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_EQ(second_payload.at("lockfile_mode"), "unchanged");
  EXPECT_EQ(ReadFile(lockfile_path), first_lock);
  EXPECT_EQ(ReadFile(resolution_path), first_resolution);

  testing::internal::CaptureStdout();
  EXPECT_EQ(
      pafio::RunCli(
          {"--json", "sync", "--locked", "--offline", "--manifest-path", manifest_path.string()}),
      pafio::kExitSuccess);
  const json offline_payload = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_TRUE(offline_payload.at("locked"));
  EXPECT_TRUE(offline_payload.at("offline"));
  EXPECT_EQ(ReadFile(lockfile_path), first_lock);
  EXPECT_EQ(ReadFile(resolution_path), first_resolution);
}

TEST(SyncCliTests, LockedSyncRequiresExistingFreshLockfile)
{
  const fs::path root = MakeTempDir("sync-locked");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WritePathDependencyProject(root);
  const fs::path manifest_path = root / "pafio.toml";

  testing::internal::CaptureStderr();
  EXPECT_EQ(
      pafio::RunCli({"sync", "--locked", "--manifest-path", manifest_path.string()}),
      pafio::kExitLock);
  const std::string missing_lock_error = testing::internal::GetCapturedStderr();
  EXPECT_NE(missing_lock_error.find("lockfile missing"), std::string::npos);

  ASSERT_EQ(pafio::RunCli({"sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  EXPECT_EQ(
      pafio::RunCli({"--json", "sync", "--locked", "--manifest-path", manifest_path.string()}),
      pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_EQ(payload.at("lockfile_mode"), "locked");
  EXPECT_TRUE(payload.at("locked"));
  EXPECT_FALSE(payload.at("offline"));
  EXPECT_TRUE(fs::exists(root / ".pafio/resolution-v1.json"));
}

TEST(SyncCliTests, FrozenSyncUsesLockedOfflinePolicy)
{
  const fs::path root = MakeTempDir("sync-frozen");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WritePathDependencyProject(root);
  const fs::path manifest_path = root / "pafio.toml";

  ASSERT_EQ(pafio::RunCli({"sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  EXPECT_EQ(
      pafio::RunCli({"--json", "sync", "--frozen", "--manifest-path", manifest_path.string()}),
      pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());
  EXPECT_EQ(payload.at("lockfile_mode"), "locked");
  EXPECT_TRUE(payload.at("locked"));
  EXPECT_TRUE(payload.at("offline"));
  EXPECT_TRUE(fs::exists(root / ".pafio/resolution-v1.json"));
}
