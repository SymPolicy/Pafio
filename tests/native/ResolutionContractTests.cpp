#include "PafioResolve/ResolutionContract.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioResolve/Resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

constexpr std::string_view kManifestDigest =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view kLockDigest =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr std::string_view kRegistryDigest =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "pafio-resolution-contract-tests" / label;
  fs::remove_all(root);
  fs::create_directories(root);
  return fs::absolute(root).lexically_normal();
}

pafio::ResolvedPackage MakePackage(
    const fs::path &root,
    std::string id,
    std::string source_kind,
    std::optional<std::string> content_sha256,
    std::vector<pafio::ResolvedDependencyAlias> aliases = {})
{
  fs::create_directories(root);
  return {
      .manifest_path = root / "pafio.toml",
      .root_dir = root,
      .package = {},
      .id = std::move(id),
      .source_kind = std::move(source_kind),
      .sha256 = std::move(content_sha256),
      .dependency_aliases = std::move(aliases),
  };
}

pafio::ResolvedGraphResult MakeCanonicalFixture(const fs::path &root)
{
  const std::string app_id = "workspace:acme/app@1.0.0";
  const std::string bridge_id = "path:acme/bridge@1.1.0";
  const std::string tool_id = "workspace:acme/tool@1.0.0";
  const std::string util_id = "registry:acme/util@2.0.0#" + std::string(kRegistryDigest);

  pafio::ResolvedGraphResult graph;
  graph.manifest_path = root / "pafio.toml";
  graph.lockfile_path = root / "pafio.lock";
  graph.root_ids = {tool_id, app_id};
  graph.packages = {
      MakePackage(root / "tool", tool_id, "workspace", std::nullopt),
      MakePackage(
          root / "bridge",
          bridge_id,
          "path",
          std::nullopt,
          {
              {.alias = "zeta", .package_id = util_id},
              {.alias = "alpha", .package_id = util_id},
          }),
      MakePackage(
          root / "app",
          app_id,
          "workspace",
          std::nullopt,
          {{.alias = "bridge", .package_id = bridge_id}}),
      MakePackage(root / "registry-util", util_id, "registry", std::string(kRegistryDigest)),
  };
  return graph;
}

std::set<std::string> ObjectKeys(const json &value)
{
  std::set<std::string> keys;
  for (auto it = value.begin(); it != value.end(); ++it)
  {
    keys.insert(it.key());
  }
  return keys;
}

}  // namespace

TEST(ResolutionContractTests, SerializesOnlyTheCanonicalMinimalDocument)
{
  const fs::path root = MakeTempDir("canonical");
  const pafio::ResolvedGraphResult graph = MakeCanonicalFixture(root);

  const std::string first =
      pafio::SerializeResolutionCanonical(graph, kManifestDigest, kLockDigest);
  ASSERT_FALSE(first.empty());
  EXPECT_EQ(first.back(), '\n');
  EXPECT_TRUE(first.size() < 2U || first[first.size() - 2U] != '\n');

  pafio::ResolvedGraphResult reordered = graph;
  std::reverse(reordered.root_ids.begin(), reordered.root_ids.end());
  std::reverse(reordered.packages.begin(), reordered.packages.end());
  for (pafio::ResolvedPackage &package : reordered.packages)
  {
    std::reverse(package.dependency_aliases.begin(), package.dependency_aliases.end());
  }
  EXPECT_EQ(
      pafio::SerializeResolutionCanonical(reordered, kManifestDigest, kLockDigest),
      first);

  const json payload = json::parse(first);
  EXPECT_EQ(
      ObjectKeys(payload),
      (std::set<std::string>{
          "lock_sha256",
          "manifest_sha256",
          "packages",
          "roots",
          "schema_version",
      }));
  EXPECT_EQ(payload.at("schema_version"), 1);
  EXPECT_EQ(payload.at("manifest_sha256"), std::string(kManifestDigest));
  EXPECT_EQ(payload.at("lock_sha256"), std::string(kLockDigest));
  EXPECT_EQ(
      payload.at("roots"),
      (json::array({
          "workspace:acme/app@1.0.0",
          "workspace:acme/tool@1.0.0",
      })));

  const json &packages = payload.at("packages");
  ASSERT_EQ(packages.size(), 4U);
  EXPECT_EQ(packages[0].at("id"), "path:acme/bridge@1.1.0");
  EXPECT_EQ(
      packages[0].at("dependencies"),
      (json::array({
          {{"alias", "alpha"},
           {"package_id", "registry:acme/util@2.0.0#" + std::string(kRegistryDigest)}},
          {{"alias", "zeta"},
           {"package_id", "registry:acme/util@2.0.0#" + std::string(kRegistryDigest)}},
      })));
  EXPECT_TRUE(packages[0].at("content_sha256").is_null());
  EXPECT_EQ(packages[1].at("id"), "registry:acme/util@2.0.0#" + std::string(kRegistryDigest));
  EXPECT_EQ(packages[1].at("content_sha256"), std::string(kRegistryDigest));
  EXPECT_EQ(packages[2].at("id"), "workspace:acme/app@1.0.0");
  EXPECT_TRUE(packages[2].at("content_sha256").is_null());
  EXPECT_EQ(packages[3].at("id"), "workspace:acme/tool@1.0.0");
  EXPECT_TRUE(packages[3].at("content_sha256").is_null());

  for (const json &package : packages)
  {
    EXPECT_EQ(
        ObjectKeys(package),
        (std::set<std::string>{
            "content_sha256",
            "dependencies",
            "id",
            "root",
        }));
    EXPECT_TRUE(fs::path(package.at("root").get<std::string>()).is_absolute());
    EXPECT_TRUE(fs::is_directory(package.at("root").get<std::string>()));
    for (const json &dependency : package.at("dependencies"))
    {
      EXPECT_EQ(
          ObjectKeys(dependency),
          (std::set<std::string>{"alias", "package_id"}));
    }
  }

  EXPECT_EQ(first, payload.dump(2) + '\n');
}

TEST(ResolutionContractTests, RejectsInvalidDigestsAndGraphReferences)
{
  const fs::path root = MakeTempDir("invalid");
  const pafio::ResolvedGraphResult valid = MakeCanonicalFixture(root);

  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(valid, "not-a-sha256", kLockDigest),
      pafio::ResolutionError);
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(valid, kManifestDigest, "not-a-sha256"),
      pafio::ResolutionError);

  pafio::ResolvedGraphResult duplicate_id = valid;
  duplicate_id.packages.push_back(duplicate_id.packages.front());
  duplicate_id.packages.back().root_dir = root / "duplicate";
  fs::create_directories(duplicate_id.packages.back().root_dir);
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(duplicate_id, kManifestDigest, kLockDigest),
      pafio::ResolutionError);

  pafio::ResolvedGraphResult dangling_root = valid;
  dangling_root.root_ids.push_back("workspace:acme/missing@1.0.0");
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(dangling_root, kManifestDigest, kLockDigest),
      pafio::ResolutionError);

  pafio::ResolvedGraphResult dangling_alias = valid;
  dangling_alias.packages[1].dependency_aliases.push_back(
      {.alias = "missing", .package_id = "registry:acme/missing@1.0.0#dddd"});
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(dangling_alias, kManifestDigest, kLockDigest),
      pafio::ResolutionError);

  pafio::ResolvedGraphResult duplicate_alias = valid;
  duplicate_alias.packages[1].dependency_aliases.push_back(
      duplicate_alias.packages[1].dependency_aliases.front());
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(duplicate_alias, kManifestDigest, kLockDigest),
      pafio::ResolutionError);

  pafio::ResolvedGraphResult relative_root = valid;
  relative_root.packages.front().root_dir = "relative/source";
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(relative_root, kManifestDigest, kLockDigest),
      pafio::ResolutionError);

  pafio::ResolvedGraphResult missing_root = valid;
  missing_root.packages.front().root_dir = root / "does-not-exist";
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(missing_root, kManifestDigest, kLockDigest),
      pafio::ResolutionError);

  pafio::ResolvedGraphResult wrong_registry_digest = valid;
  const auto registry = std::find_if(
      wrong_registry_digest.packages.begin(),
      wrong_registry_digest.packages.end(),
      [](const pafio::ResolvedPackage &package) {
        return package.source_kind == "registry";
      });
  ASSERT_NE(registry, wrong_registry_digest.packages.end());
  registry->sha256 = "eeee";
  EXPECT_THROW(
      pafio::SerializeResolutionCanonical(wrong_registry_digest, kManifestDigest, kLockDigest),
      pafio::ResolutionError);
}
