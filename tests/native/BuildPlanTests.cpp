#include "BuildTestSupport.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioPlan/CompilePlan.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

using spio::testsupport::CanonicalAbsolutePath;
using spio::testsupport::MakeTempDir;
using spio::testsupport::ReadFile;
using spio::testsupport::WriteFile;

TEST(BuildPlanTests, WritesCompilePlanForSingleLibPackage)
{
  const fs::path root = MakeTempDir("single-lib-dry-run");
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
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "src/lib.styio", "# value := 1\n");

  const spio::BuildPlanResult result = spio::WriteBuildCompilePlan({
      .manifest_path = root / "spio.toml",
      .select_lib = true,
  });

  EXPECT_TRUE(fs::exists(result.plan_path));
  EXPECT_EQ(result.entry_target_kind, "lib");
  EXPECT_EQ(result.entry_package_name, "acme/demo");

  const json plan = json::parse(ReadFile(result.plan_path));
  EXPECT_EQ(plan["plan_version"], 1);
  EXPECT_EQ(plan["intent"], "build");
  EXPECT_EQ(plan["workspace_root"], CanonicalAbsolutePath(root).string());
  EXPECT_EQ(plan["entry"]["target_kind"], "lib");
  EXPECT_EQ(plan["entry"]["file"], CanonicalAbsolutePath(root / "src/lib.styio").string());
  EXPECT_EQ(plan["toolchain"]["std_package_id"], "builtin:std@nightly/2026");
  EXPECT_EQ(plan["profile"]["name"], "dev");
  EXPECT_EQ(plan["emit"]["error_format"], "jsonl");
  ASSERT_EQ(plan["packages"].size(), 1U);
  EXPECT_EQ(plan["packages"][0]["targets"]["lib"], CanonicalAbsolutePath(root / "src/lib.styio").string());
}

TEST(BuildPlanTests, RejectsAmbiguousPackageTargetsWithoutExplicitSelection)
{
  const fs::path root = MakeTempDir("ambiguous-target");
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
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/lib.styio", "# lib := 1\n");
  WriteFile(root / "src/main.styio", ">_(\"hi\")\n");

  EXPECT_THROW(
      spio::WriteBuildCompilePlan({
          .manifest_path = root / "spio.toml",
      }),
      spio::PlanError);
}

TEST(BuildPlanTests, WorkspaceBuildRequiresExplicitPackageSelectionWhenMultipleRootsExist)
{
  const fs::path root = MakeTempDir("workspace-package-selection");
  WriteFile(
      root / "spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[workspace]\n"
      "members = [\"packages/app\", \"packages/tool\"]\n"
      "resolver = \"1\"\n");
  WriteFile(
      root / "packages/app/spio.toml",
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
  WriteFile(root / "packages/app/src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "packages/tool/spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/tool\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"tool\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "packages/tool/src/main.styio", ">_(\"tool\")\n");

  EXPECT_THROW(
      spio::WriteBuildCompilePlan({
          .manifest_path = root / "spio.toml",
      }),
      spio::PlanError);

  const spio::BuildPlanResult result = spio::WriteBuildCompilePlan({
      .manifest_path = root / "spio.toml",
      .package_name = "acme/tool",
  });
  EXPECT_EQ(result.entry_package_name, "acme/tool");
  EXPECT_EQ(result.entry_target_kind, "bin");
  EXPECT_EQ(result.entry_target_name, "tool");
}

TEST(BuildPlanTests, RejectsMixedEditionGraphForCompilePlanV1)
{
  const fs::path root = MakeTempDir("mixed-edition");
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"deps/util\" }\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "deps/util/spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/util\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2027\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "deps/util/src/lib.styio", "# util := 1\n");

  EXPECT_THROW(
      spio::WriteBuildCompilePlan({
          .manifest_path = root / "spio.toml",
      }),
      spio::PlanError);
}
