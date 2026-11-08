#include "PafioResolve/MetadataContract.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

TEST(MetadataTests, SerializerExposesOnlyTheMetadataV1TopLevelFields)
{
  const pafio::MetadataDocument document{
      .package = {{"name", "acme/app"}},
      .workspace = {{"root", "."}},
      .dependencies = nlohmann::json::array({{{"name", "acme/util"}}}),
      .targets = nlohmann::json::array({{{"name", "app"}, {"kind", "bin"}}}),
      .lock = {{"present", true}},
      .resolution = {{"packages", 2}},
      .vendor = {{"present", false}},
  };

  const nlohmann::json payload = pafio::SerializeMetadataV1(document);
  constexpr std::array<const char *, 7> expected{
      "package",
      "workspace",
      "dependencies",
      "targets",
      "lock",
      "resolution",
      "vendor",
  };

  ASSERT_EQ(payload.size(), expected.size());
  std::set<std::string> actual_fields;
  for (const auto &[field, value] : payload.items())
  {
    (void) value;
    actual_fields.insert(field);
  }
  EXPECT_EQ(actual_fields, std::set<std::string>(expected.begin(), expected.end()));

  for (const char *field : expected)
  {
    EXPECT_TRUE(payload.contains(field)) << field;
  }

  EXPECT_EQ(payload.at("package"), document.package);
  EXPECT_EQ(payload.at("workspace"), document.workspace);
  EXPECT_EQ(payload.at("dependencies"), document.dependencies);
  EXPECT_EQ(payload.at("targets"), document.targets);
  EXPECT_EQ(payload.at("lock"), document.lock);
  EXPECT_EQ(payload.at("resolution"), document.resolution);
  EXPECT_EQ(payload.at("vendor"), document.vendor);

  for (const char *forbidden : {"cloud", "cloud_policy", "toolchain", "managed_toolchain", "ide", "ide_hints"})
  {
    EXPECT_FALSE(payload.contains(forbidden)) << forbidden;
  }
}

TEST(MetadataTests, ProjectSnapshotCarriesWorkspacePackagesAndDependencySources)
{
  const fs::path project_root =
      fs::temp_directory_path() / "pafio-metadata-v1-project-snapshot";
  fs::remove_all(project_root);
  fs::create_directories(project_root);
  const fs::path manifest_path = project_root / "pafio.toml";
  {
    std::ofstream manifest(manifest_path);
    manifest
        << "[pafio]\n"
        << "manifest-version = 1\n\n"
        << "[package]\n"
        << "name = \"acme/app\"\n"
        << "version = \"1.0.0\"\n"
        << "edition = \"2026\"\n\n"
        << "[build]\n"
        << "implicit-std = true\n\n"
        << "[[bin]]\n"
        << "name = \"app\"\n"
        << "path = \"src/main.styio\"\n";
  }

  pafio::PackageConfig application{
      .name = "acme/app",
      .version = "1.0.0",
      .edition = "2026",
      .publish = true,
  };
  application.dependencies.push_back({
      .alias = "core",
      .package = "acme/core",
      .source_kind = pafio::DependencySourceKind::kPath,
      .source = "../core",
  });
  application.dev_dependencies.push_back({
      .alias = "fixtures",
      .package = "acme/fixtures",
      .source_kind = pafio::DependencySourceKind::kRegistry,
      .source = "https://registry.example.invalid",
      .version = "2.0.0",
  });

  const pafio::ResolvedGraphResult graph{
      .manifest_path = manifest_path,
      .lockfile_path = project_root / "pafio.lock",
      .root_ids = {"workspace:acme/app@1.0.0"},
      .packages = {
          {
              .manifest_path = manifest_path,
              .root_dir = project_root,
              .package = application,
              .id = "workspace:acme/app@1.0.0",
              .source_kind = "workspace",
              .dependencies = {
                  "path:acme/core@1.0.0",
                  "registry:acme/fixtures@2.0.0#sha256",
              },
              .dependency_aliases = {
                  {.alias = "core", .package_id = "path:acme/core@1.0.0"},
                  {
                      .alias = "fixtures",
                      .package_id =
                          "registry:acme/fixtures@2.0.0#sha256",
                  },
              },
          },
          {
              .manifest_path = project_root.parent_path() / "core" /
                               "pafio.toml",
              .root_dir = project_root.parent_path() / "core",
              .package = {
                  .name = "acme/core",
                  .version = "1.0.0",
                  .edition = "2026",
              },
              .id = "path:acme/core@1.0.0",
              .source_kind = "path",
          },
          {
              .manifest_path = project_root / ".cache" / "fixtures" /
                               "pafio.toml",
              .root_dir = project_root / ".cache" / "fixtures",
              .package = {
                  .name = "acme/fixtures",
                  .version = "2.0.0",
                  .edition = "2026",
              },
              .id = "registry:acme/fixtures@2.0.0#sha256",
              .source_kind = "registry",
          },
      },
  };

  const nlohmann::json payload = pafio::SerializeMetadataV1(
      pafio::BuildMetadataDocument(manifest_path, graph));

  ASSERT_EQ(payload.at("workspace").at("packages").size(), 3U);
  EXPECT_EQ(
      payload.at("workspace").at("packages")[0].at("name"), "acme/core");
  EXPECT_EQ(
      payload.at("workspace").at("packages")[1].at("name"), "acme/fixtures");
  EXPECT_EQ(
      payload.at("workspace").at("packages")[2].at("name"), "acme/app");

  ASSERT_EQ(payload.at("dependencies").size(), 2U);
  const nlohmann::json &core = payload.at("dependencies")[0];
  EXPECT_EQ(core.at("alias"), "core");
  EXPECT_EQ(core.at("kind"), "normal");
  EXPECT_EQ(core.at("package"), "acme/core");
  EXPECT_TRUE(core.at("requirement").is_null());
  EXPECT_EQ(core.at("source").at("kind"), "path");
  EXPECT_EQ(core.at("source").at("location"), "../core");
  EXPECT_TRUE(core.at("source").at("rev").is_null());

  const nlohmann::json &fixtures = payload.at("dependencies")[1];
  EXPECT_EQ(fixtures.at("alias"), "fixtures");
  EXPECT_EQ(fixtures.at("kind"), "dev");
  EXPECT_EQ(fixtures.at("package"), "acme/fixtures");
  EXPECT_EQ(fixtures.at("requirement"), "2.0.0");
  EXPECT_EQ(fixtures.at("source").at("kind"), "registry");
  EXPECT_EQ(
      fixtures.at("source").at("location"),
      "https://registry.example.invalid");
}
