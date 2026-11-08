#include "BuildTestSupport.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioPlan/CompilePlan.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

using pafio::testsupport::CanonicalAbsolutePath;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::WriteFile;

namespace
{
void ExpectSameInputsWithIndependentOutputs(
    const pafio::BuildPlanResult &first, const pafio::BuildPlanResult &second)
{
  EXPECT_EQ(first.cache_key, second.cache_key);
  EXPECT_NE(first.build_root, second.build_root);
  EXPECT_NE(first.plan_path, second.plan_path);
  EXPECT_EQ(first.build_root.parent_path(), second.build_root.parent_path());
  json first_plan = json::parse(first.plan_json);
  json second_plan = json::parse(second.plan_json);
  first_plan.erase("outputs");
  second_plan.erase("outputs");
  EXPECT_EQ(first_plan, second_plan);
  EXPECT_EQ(ReadFile(first.plan_path), first.plan_json);
}
}  // namespace

TEST(BuildPlanTests, WritesCompilePlanForSingleLibPackage)
{
  const fs::path root = MakeTempDir("single-lib-dry-run");
  WriteFile(
      root / "pafio.toml",
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

  const pafio::BuildPlanResult result = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
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
  EXPECT_EQ(plan["toolchain"]["std_package_id"], "builtin:std@unbound/2026");
  EXPECT_EQ(plan["profile"]["name"], "dev");
  EXPECT_FALSE(plan["profile"].contains("build_mode"));
  EXPECT_EQ(plan["emit"]["error_format"], "jsonl");
  EXPECT_FALSE(plan["emit"].contains("observable_static_snapshot"));
  EXPECT_EQ(
      plan["emit"].dump(),
      R"({"ast":false,"error_format":"jsonl","llvm_ir":false,"styio_ir":false})");
  ASSERT_EQ(plan["packages"].size(), 1U);
  EXPECT_EQ(plan["packages"][0]["targets"]["lib"], CanonicalAbsolutePath(root / "src/lib.styio").string());
}

namespace
{

void WriteSingleBinProject(const fs::path &root)
{
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
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");
}

}  // namespace

TEST(BuildPlanTests, EmitsObservableStaticSnapshotRequestOnlyWhenRequested)
{
  const fs::path root = MakeTempDir("observable-static-snapshot-request");
  WriteSingleBinProject(root);

  const pafio::BuildPlanResult baseline = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
  });
  const pafio::BuildPlanResult baseline_again = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
  });
  ExpectSameInputsWithIndependentOutputs(baseline, baseline_again);
  EXPECT_FALSE(json::parse(baseline.plan_json)["emit"].contains("observable_static_snapshot"));

  const pafio::BuildPlanResult requested = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
      .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{
          .schema_version = 1,
          .required_capabilities = {"zeta", "alpha", "zeta", "beta"},
      },
  });
  EXPECT_NE(requested.cache_key, baseline.cache_key);

  const json plan = json::parse(ReadFile(requested.plan_path));
  EXPECT_EQ(plan["intent"], "check");
  EXPECT_EQ(plan["emit"]["error_format"], "jsonl");
  EXPECT_EQ(plan["emit"]["ast"], false);
  EXPECT_EQ(plan["emit"]["styio_ir"], false);
  EXPECT_EQ(plan["emit"]["llvm_ir"], false);
  ASSERT_TRUE(plan["emit"].contains("observable_static_snapshot"));
  EXPECT_EQ(
      plan["emit"]["observable_static_snapshot"].dump(),
      R"({"required_capabilities":["alpha","beta","zeta"],"schema_version":1})");

  const pafio::BuildPlanResult defaults = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
      .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{},
  });
  EXPECT_EQ(
      json::parse(defaults.plan_json)["emit"]["observable_static_snapshot"].dump(),
      R"({"required_capabilities":[],"schema_version":1})");
}

TEST(BuildPlanTests, ObservableParentSnapshotPathIsEmittedOnlyWhenRequestedAndNeverChangesCacheKey)
{
  const fs::path root = MakeTempDir("observable-parent-snapshot");
  WriteSingleBinProject(root);

  const pafio::BuildPlanResult baseline = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
  });
  EXPECT_EQ(
      json::parse(baseline.plan_json)["emit"].dump(),
      R"({"ast":false,"error_format":"jsonl","llvm_ir":false,"styio_ir":false})");

  const pafio::BuildPlanResult without_parent = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
      .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{
          .schema_version = 1,
          .required_capabilities = {"alpha"},
      },
  });
  EXPECT_FALSE(
      json::parse(without_parent.plan_json)["emit"]["observable_static_snapshot"].contains("parent_snapshot_path"));

  const fs::path absolute_parent =
      CanonicalAbsolutePath(root / "previous" / "app.observable-static-snapshot.json");
  const pafio::BuildPlanResult with_parent = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
      .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{
          .schema_version = 1,
          .required_capabilities = {"alpha"},
          .parent_snapshot_path = root / "previous" / "." / "app.observable-static-snapshot.json",
      },
  });
  EXPECT_EQ(with_parent.cache_key, without_parent.cache_key);
  EXPECT_NE(with_parent.build_root, without_parent.build_root);
  EXPECT_NE(with_parent.cache_key, baseline.cache_key);

  const json with_parent_request = json::parse(with_parent.plan_json)["emit"]["observable_static_snapshot"];
  EXPECT_EQ(with_parent_request["schema_version"], 1);
  EXPECT_EQ(with_parent_request["required_capabilities"].dump(), R"(["alpha"])");
  EXPECT_EQ(with_parent_request["parent_snapshot_path"], absolute_parent.string());
  EXPECT_EQ(
      with_parent_request.dump(),
      R"({"parent_snapshot_path":")" + absolute_parent.string() +
          R"(","required_capabilities":["alpha"],"schema_version":1})");

  const pafio::BuildPlanResult with_relative_parent = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
      .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{
          .schema_version = 1,
          .required_capabilities = {"alpha"},
          .parent_snapshot_path = fs::path("previous/app.observable-static-snapshot.json"),
      },
  });
  EXPECT_EQ(with_relative_parent.cache_key, without_parent.cache_key);
  EXPECT_EQ(
      json::parse(with_relative_parent.plan_json)["emit"]["observable_static_snapshot"]["parent_snapshot_path"],
      absolute_parent.string());

  const pafio::BuildPlanResult baseline_again = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "check",
  });
  ExpectSameInputsWithIndependentOutputs(baseline, baseline_again);

  EXPECT_THROW(
      pafio::WriteBuildCompilePlan({
          .manifest_path = root / "pafio.toml",
          .intent = "check",
          .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{
              .schema_version = 1,
              .parent_snapshot_path = fs::path(),
          },
      }),
      pafio::PlanError);
}

TEST(BuildPlanTests, RejectsMalformedObservableStaticSnapshotRequest)
{
  const fs::path root = MakeTempDir("observable-static-snapshot-invalid");
  WriteSingleBinProject(root);

  EXPECT_THROW(
      pafio::WriteBuildCompilePlan({
          .manifest_path = root / "pafio.toml",
          .intent = "check",
          .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{.schema_version = 0},
      }),
      pafio::PlanError);
  EXPECT_THROW(
      pafio::WriteBuildCompilePlan({
          .manifest_path = root / "pafio.toml",
          .intent = "check",
          .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{
              .schema_version = 1,
              .required_capabilities = {"alpha", ""},
          },
      }),
      pafio::PlanError);
}

TEST(BuildPlanTests, EmitsRuntimeObservationRequestOnlyWhenRequested)
{
  const fs::path root = MakeTempDir("runtime-observation-request");
  WriteSingleBinProject(root);

  const pafio::BuildPlanResult baseline = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
  });
  EXPECT_FALSE(json::parse(baseline.plan_json)["emit"].contains("runtime_observation"));
  EXPECT_EQ(
      json::parse(baseline.plan_json)["emit"].dump(),
      R"({"ast":false,"error_format":"jsonl","llvm_ir":false,"styio_ir":false})");

  // Only the version is set: nothing else is emitted, Styio applies its defaults.
  const pafio::BuildPlanResult version_only = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
      .runtime_observation = pafio::RuntimeObservationRequest{},
  });
  EXPECT_NE(version_only.cache_key, baseline.cache_key);
  EXPECT_NE(version_only.build_root, baseline.build_root);
  const json version_only_plan = json::parse(ReadFile(version_only.plan_path));
  EXPECT_EQ(version_only_plan["emit"]["error_format"], "jsonl");
  EXPECT_EQ(version_only_plan["emit"]["ast"], false);
  EXPECT_EQ(version_only_plan["emit"]["styio_ir"], false);
  EXPECT_EQ(version_only_plan["emit"]["llvm_ir"], false);
  EXPECT_FALSE(version_only_plan["emit"].contains("observable_static_snapshot"));
  EXPECT_EQ(version_only_plan["emit"]["runtime_observation"].dump(), R"({"version":2})");

  const pafio::BuildPlanResult full = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
      .runtime_observation = pafio::RuntimeObservationRequest{
          .version = 2,
          .mode = "sampled",
          .required_capabilities = {"task-lifecycle", "loss-accounting", "task-lifecycle", "scheduler-queue"},
          .lane_capacity = 512,
          .priority_reserved = 64,
          .producer_lanes = 4,
          .sampling = pafio::RuntimeObservationSampling{.numerator = 1, .denominator = 8, .seed = 7},
      },
  });
  EXPECT_NE(full.cache_key, baseline.cache_key);
  EXPECT_NE(full.cache_key, version_only.cache_key);
  EXPECT_EQ(
      json::parse(full.plan_json)["emit"]["runtime_observation"].dump(),
      R"({"lane_capacity":512,"mode":"sampled","priority_reserved":64,"producer_lanes":4,)"
      R"("required_capabilities":["loss-accounting","scheduler-queue","task-lifecycle"],)"
      R"("sampling":{"denominator":8,"numerator":1,"seed":7},"version":2})");

  // Sampling without a seed leaves the seed to Styio.
  const pafio::BuildPlanResult seedless = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
      .runtime_observation = pafio::RuntimeObservationRequest{
          .sampling = pafio::RuntimeObservationSampling{.numerator = 1, .denominator = 16},
      },
  });
  EXPECT_EQ(
      json::parse(seedless.plan_json)["emit"]["runtime_observation"].dump(),
      R"({"sampling":{"denominator":16,"numerator":1},"version":2})");

  // Cache identity is deterministic; invocation outputs remain independent.
  const pafio::BuildPlanResult full_again = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
      .runtime_observation = pafio::RuntimeObservationRequest{
          .version = 2,
          .mode = "sampled",
          .required_capabilities = {"scheduler-queue", "task-lifecycle", "loss-accounting"},
          .lane_capacity = 512,
          .priority_reserved = 64,
          .producer_lanes = 4,
          .sampling = pafio::RuntimeObservationSampling{.numerator = 1, .denominator = 8, .seed = 7},
      },
  });
  EXPECT_EQ(full_again.cache_key, full.cache_key);
  ExpectSameInputsWithIndependentOutputs(full, full_again);

  // Both emission requests coexist and each contributes its own cache material.
  const pafio::BuildPlanResult both = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
      .observable_static_snapshot = pafio::ObservableStaticSnapshotRequest{},
      .runtime_observation = pafio::RuntimeObservationRequest{},
  });
  const json both_emit = json::parse(both.plan_json)["emit"];
  EXPECT_EQ(both_emit["observable_static_snapshot"].dump(), R"({"required_capabilities":[],"schema_version":1})");
  EXPECT_EQ(both_emit["runtime_observation"].dump(), R"({"version":2})");
  EXPECT_NE(both.cache_key, version_only.cache_key);

  const pafio::BuildPlanResult baseline_again = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
      .intent = "run",
  });
  ExpectSameInputsWithIndependentOutputs(baseline, baseline_again);
  EXPECT_EQ(baseline_again.cache_key, baseline.cache_key);
}

TEST(BuildPlanTests, RejectsMalformedRuntimeObservationRequest)
{
  const fs::path root = MakeTempDir("runtime-observation-invalid");
  WriteSingleBinProject(root);

  const auto expect_rejected = [&](pafio::RuntimeObservationRequest observation) {
    EXPECT_THROW(
        pafio::WriteBuildCompilePlan({
            .manifest_path = root / "pafio.toml",
            .intent = "run",
            .runtime_observation = std::move(observation),
        }),
        pafio::PlanError);
  };

  expect_rejected({.version = 0});
  expect_rejected({.version = -2});
  expect_rejected({.mode = "loud"});
  expect_rejected({.mode = ""});
  expect_rejected({.required_capabilities = {"task-lifecycle", ""}});
  expect_rejected({.lane_capacity = 0});
  expect_rejected({.priority_reserved = 0});
  expect_rejected({.producer_lanes = 0});
  expect_rejected({.sampling = pafio::RuntimeObservationSampling{.numerator = 1, .denominator = 0}});
  expect_rejected({.sampling = pafio::RuntimeObservationSampling{.numerator = 0, .denominator = 16}});
  expect_rejected({.sampling = pafio::RuntimeObservationSampling{.numerator = 1, .denominator = 16, .seed = -1}});
}

TEST(BuildPlanTests, RejectsAmbiguousPackageTargetsWithoutExplicitSelection)
{
  const fs::path root = MakeTempDir("ambiguous-target");
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
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/lib.styio", "# lib := 1\n");
  WriteFile(root / "src/main.styio", ">_(\"hi\")\n");

  EXPECT_THROW(
      pafio::WriteBuildCompilePlan({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PlanError);
}

TEST(BuildPlanTests, WorkspaceBuildRequiresExplicitPackageSelectionWhenMultipleRootsExist)
{
  const fs::path root = MakeTempDir("workspace-package-selection");
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
      "publish = false\n\n"
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
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"tool\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "packages/tool/src/main.styio", ">_(\"tool\")\n");

  EXPECT_THROW(
      pafio::WriteBuildCompilePlan({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PlanError);

  const pafio::BuildPlanResult result = pafio::WriteBuildCompilePlan({
      .manifest_path = root / "pafio.toml",
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"deps/util\" }\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "deps/util/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/util\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2027\"\n"
      "publish = false\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "deps/util/src/lib.styio", "# util := 1\n");

  EXPECT_THROW(
      pafio::WriteBuildCompilePlan({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PlanError);
}
