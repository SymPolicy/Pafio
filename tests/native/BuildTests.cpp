#include "SpioCLI/CLI.hpp"
#include "SpioCore/Errors.hpp"
#include "SpioPlan/CompilePlan.hpp"

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

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "spio-native-build-tests" / label;
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

void WriteExecutable(const fs::path &path, const std::string &content)
{
  WriteFile(path, content);
  fs::permissions(
      path,
      fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
      fs::perm_options::add);
}

fs::path WriteFakeCompilePlanStyio(const fs::path &root, const std::string &name)
{
  const fs::path binary = root / name;
  WriteExecutable(
      binary,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"variant\":\"full\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contracts\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[1]},\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[1]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":true},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$1\" = \"--compile-plan\" ] && [ -n \"$2\" ]; then\n"
      "  python3 - \"$2\" <<'PY'\n"
      "import json, pathlib, sys\n"
      "plan_path = pathlib.Path(sys.argv[1])\n"
      "plan = json.loads(plan_path.read_text())\n"
      "outputs = plan['outputs']\n"
      "build_root = pathlib.Path(outputs['build_root'])\n"
      "artifact_dir = pathlib.Path(outputs['artifact_dir'])\n"
      "diag_dir = pathlib.Path(outputs['diag_dir'])\n"
      "build_root.mkdir(parents=True, exist_ok=True)\n"
      "artifact_dir.mkdir(parents=True, exist_ok=True)\n"
      "diag_dir.mkdir(parents=True, exist_ok=True)\n"
      "runtime_events_path = build_root / 'runtime-events.jsonl'\n"
      "entry = plan['entry']\n"
      "target_name = entry.get('target_name') or pathlib.Path(entry['file']).stem\n"
      "session_id = 'fake-runtime-session'\n"
      "artifact_path = artifact_dir / f\"{target_name}.llvm.ir\"\n"
      "artifact_path.write_text('; fake llvm ir\\n')\n"
      "executed = plan['intent'] in ('run', 'test')\n"
      "receipt = {\n"
      "  'schema_version': 1,\n"
      "  'tool': 'styio',\n"
      "  'compiler_version': '0.0.5',\n"
      "  'channel': 'stable',\n"
      "  'plan_version': plan['plan_version'],\n"
      "  'intent': plan['intent'],\n"
      "  'session_id': session_id,\n"
      "  'executed': executed,\n"
      "  'wall_time_ms': 1,\n"
      "  'generated_at': '2026-04-17T00:00:00Z',\n"
      "  'dict_impl': {'selected': 'native'},\n"
      "  'entry': entry,\n"
      "  'outputs': outputs,\n"
      "  'artifacts': [str(artifact_path)],\n"
      "}\n"
      "(build_root / 'receipt.json').write_text(json.dumps(receipt))\n"
      "(diag_dir / 'diagnostics.jsonl').write_text('')\n"
      "unit_id = f\"{entry['package_id']}::{entry['target_kind']}:{entry['target_name']}\"\n"
      "sequence = 1\n"
      "events = []\n"
      "def emit(kind, origin, payload):\n"
      "  global sequence\n"
      "  timestamp = '2026-04-17T00:00:00Z' if sequence <= 3 else ('2026-04-17T00:00:01Z' if sequence <= 6 else '2026-04-17T00:00:02Z')\n"
      "  events.append({'schema_version': 1, 'session_id': session_id, 'sequence': sequence, 'timestamp': timestamp, 'eventKind': kind, 'origin': origin, 'payload': payload})\n"
      "  sequence += 1\n"
      "emit('compile.started', 'styio.compile-plan', {'intent': plan['intent']})\n"
      "emit('unit.entered', 'styio.compile-plan', {'unit_id': unit_id})\n"
      "if executed and plan['intent'] == 'test':\n"
      "  emit('unit.test.started', 'styio.tests', {'unit_id': unit_id, 'test_name': entry['target_name']})\n"
      "if executed:\n"
      "  emit('run.started', 'styio.runtime', {'intent': plan['intent']})\n"
      "emit('compile.finished', 'styio.compile-plan', {'intent': plan['intent'], 'executed': executed})\n"
      "emit('unit.exited', 'styio.compile-plan', {'unit_id': unit_id, 'executed': executed, 'success': True})\n"
      "if executed and plan['intent'] == 'test':\n"
      "  emit('unit.test.finished', 'styio.tests', {'unit_id': unit_id, 'test_name': entry['target_name'], 'success': True})\n"
      "if executed:\n"
      "  emit('run.finished', 'styio.runtime', {'intent': plan['intent'], 'success': True})\n"
      "runtime_events_path.write_text(''.join(json.dumps(event) + '\\n' for event in events))\n"
      "if plan['intent'] == 'run':\n"
      "  print('spio-run-ok')\n"
      "elif plan['intent'] == 'test':\n"
      "  print('spio-test-ok')\n"
      "sys.exit(0)\n"
      "PY\n"
      "  exit $?\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n");
  return binary;
}

fs::path WriteFailingCompilePlanStyio(const fs::path &root, const std::string &name)
{
  const fs::path binary = root / name;
  WriteExecutable(
      binary,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"variant\":\"full\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contracts\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[1]},\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[1]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":true},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$1\" = \"--compile-plan\" ] && [ -n \"$2\" ]; then\n"
      "  python3 - \"$2\" <<'PY'\n"
      "import json, pathlib, sys\n"
      "plan_path = pathlib.Path(sys.argv[1])\n"
      "plan = json.loads(plan_path.read_text())\n"
      "outputs = plan['outputs']\n"
      "build_root = pathlib.Path(outputs['build_root'])\n"
      "artifact_dir = pathlib.Path(outputs['artifact_dir'])\n"
      "diag_dir = pathlib.Path(outputs['diag_dir'])\n"
      "build_root.mkdir(parents=True, exist_ok=True)\n"
      "artifact_dir.mkdir(parents=True, exist_ok=True)\n"
      "diag_dir.mkdir(parents=True, exist_ok=True)\n"
      "runtime_events_path = build_root / 'runtime-events.jsonl'\n"
      "session_id = 'fake-runtime-session-failure'\n"
      "receipt = {\n"
      "  'schema_version': 1,\n"
      "  'tool': 'styio',\n"
      "  'compiler_version': '0.0.5',\n"
      "  'channel': 'stable',\n"
      "  'plan_version': plan['plan_version'],\n"
      "  'intent': plan['intent'],\n"
      "  'session_id': session_id,\n"
      "  'executed': False,\n"
      "  'entry': plan['entry'],\n"
      "  'outputs': outputs,\n"
      "  'artifacts': [],\n"
      "}\n"
      "(build_root / 'receipt.json').write_text(json.dumps(receipt))\n"
      "diagnostic = {\n"
      "  'severity': 'error',\n"
      "  'code': 'fake.compile-plan.failure',\n"
      "  'message': 'fake compile-plan failure',\n"
      "  'target': plan['entry']['target_name'],\n"
      "}\n"
      "(diag_dir / 'diagnostics.jsonl').write_text(json.dumps(diagnostic) + '\\n')\n"
      "unit_id = f\"{plan['entry']['package_id']}::{plan['entry']['target_kind']}:{plan['entry']['target_name']}\"\n"
      "events = [\n"
      "  {'schema_version': 1, 'session_id': session_id, 'sequence': 1, 'timestamp': '2026-04-17T00:00:00Z', 'eventKind': 'compile.started', 'origin': 'styio.compile-plan', 'payload': {'intent': plan['intent']}},\n"
      "  {'schema_version': 1, 'session_id': session_id, 'sequence': 2, 'timestamp': '2026-04-17T00:00:00Z', 'eventKind': 'unit.entered', 'origin': 'styio.compile-plan', 'payload': {'unit_id': unit_id}},\n"
      "  {'schema_version': 1, 'session_id': session_id, 'sequence': 3, 'timestamp': '2026-04-17T00:00:01Z', 'eventKind': 'diagnostic.emitted', 'origin': 'styio.diagnostics', 'payload': diagnostic},\n"
      "  {'schema_version': 1, 'session_id': session_id, 'sequence': 4, 'timestamp': '2026-04-17T00:00:02Z', 'eventKind': 'unit.exited', 'origin': 'styio.compile-plan', 'payload': {'unit_id': unit_id, 'executed': False, 'success': False}},\n"
      "  {'schema_version': 1, 'session_id': session_id, 'sequence': 5, 'timestamp': '2026-04-17T00:00:02Z', 'eventKind': 'compile.failed', 'origin': 'styio.compile-plan', 'payload': {'intent': plan['intent'], 'executed': False}},\n"
      "]\n"
      "runtime_events_path.write_text(''.join(json.dumps(event) + '\\n' for event in events))\n"
      "sys.stderr.write('fake compile-plan failure\\n')\n"
      "sys.exit(17)\n"
      "PY\n"
      "  exit $?\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n");
  return binary;
}

}  // namespace

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

TEST(BuildCliTests, NonDryRunBuildIsBlockedByPublishedCompatibilityPhase)
{
  const fs::path root = MakeTempDir("build-contract-gate");
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
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const fs::path fake_styio = root / "fake-styio";
  WriteExecutable(
      fake_styio,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"active_integration_phase\":\"bootstrap-single-file\",\"supported_contracts\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[],\"runtime_events\":[]},\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":false,\"project_execution_via_compile_plan\":false,\"runtime_event_stream\":false},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n");

  const int exit_code = spio::RunCli({
      "build",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  EXPECT_EQ(exit_code, spio::kExitContract);
}

TEST(RunCliTests, DryRunEmitsRunIntentForUniqueBinaryTarget)
{
  const fs::path root = MakeTempDir("run-dry-run");
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
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const spio::BuildPlanResult result = spio::WriteBuildCompilePlan({
      .manifest_path = root / "spio.toml",
      .intent = "run",
  });
  const json plan = json::parse(ReadFile(result.plan_path));
  EXPECT_EQ(plan["intent"], "run");
  EXPECT_EQ(plan["entry"]["target_kind"], "bin");
  EXPECT_EQ(plan["entry"]["target_name"], "app");
}

TEST(CheckPlanTests, WritesCompilePlanForCheckIntent)
{
  const fs::path root = MakeTempDir("check-dry-run");
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
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const spio::BuildPlanResult result = spio::WriteBuildCompilePlan({
      .manifest_path = root / "spio.toml",
      .intent = "check",
  });
  const json plan = json::parse(ReadFile(result.plan_path));
  EXPECT_EQ(plan["intent"], "check");
  EXPECT_EQ(plan["entry"]["target_kind"], "bin");
  EXPECT_EQ(plan["entry"]["target_name"], "app");
}

TEST(RunCliTests, RejectsLibSelection)
{
  const fs::path root = MakeTempDir("run-rejects-lib");
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
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const int exit_code = spio::RunCli({
      "--json",
      "run",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--lib",
      "--dry-run",
  });
  EXPECT_EQ(exit_code, spio::kExitUsage);
}

TEST(RunCliTests, NonDryRunRunIsBlockedByPublishedCompatibilityPhase)
{
  const fs::path root = MakeTempDir("run-contract-gate");
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
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const fs::path fake_styio = root / "fake-styio";
  WriteExecutable(
      fake_styio,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"active_integration_phase\":\"bootstrap-single-file\",\"supported_contracts\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[],\"runtime_events\":[]},\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":false,\"project_execution_via_compile_plan\":false,\"runtime_event_stream\":false},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n");

  const int exit_code = spio::RunCli({
      "run",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  EXPECT_EQ(exit_code, spio::kExitContract);
}

TEST(RunCliTests, NonDryRunRunEmitsWorkflowSuccessPayload)
{
  const fs::path root = MakeTempDir("run-success-payload");
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

  const fs::path fake_styio = WriteFakeCompilePlanStyio(root, "fake-styio-success");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "run",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
      "--bin",
      "app",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "run");
  EXPECT_EQ(payload.at("mode").get<std::string>(), "execute");
  EXPECT_EQ(payload.at("workflow_payload_version").get<int>(), 1);
  EXPECT_EQ(payload.at("intent").get<std::string>(), "run");
  ASSERT_TRUE(payload.at("receipt").is_object());
  EXPECT_EQ(payload.at("receipt").at("intent").get<std::string>(), "run");
  EXPECT_EQ(payload.at("receipt").at("executed").get<bool>(), true);
  EXPECT_EQ(payload.at("receipt").at("session_id").get<std::string>(), "fake-runtime-session");
  ASSERT_TRUE(payload.at("diagnostics").is_array());
  EXPECT_TRUE(payload.at("diagnostics").empty());
  EXPECT_EQ(payload.at("runtime_session_id").get<std::string>(), "fake-runtime-session");
  EXPECT_NE(payload.at("runtime_events_path").get<std::string>().find("runtime-events.jsonl"), std::string::npos);
  ASSERT_TRUE(payload.at("runtime_events").is_array());
  EXPECT_EQ(payload.at("runtime_events").size(), 6U);
  EXPECT_EQ(payload.at("runtime_events")[0].at("eventKind").get<std::string>(), "compile.started");
  EXPECT_EQ(payload.at("runtime_events")[1].at("eventKind").get<std::string>(), "unit.entered");
  EXPECT_EQ(payload.at("runtime_events")[2].at("eventKind").get<std::string>(), "run.started");
  EXPECT_EQ(payload.at("runtime_events")[4].at("eventKind").get<std::string>(), "unit.exited");
  EXPECT_EQ(payload.at("runtime_events")[5].at("eventKind").get<std::string>(), "run.finished");
  EXPECT_NE(payload.at("stdout").get<std::string>().find("spio-run-ok"), std::string::npos);
  EXPECT_TRUE(payload.at("stderr").get<std::string>().empty());
  ASSERT_TRUE(payload.at("styio").is_object());
  EXPECT_EQ(payload.at("styio").at("integration_phase").get<std::string>(), "compile-plan-live");
}

TEST(TestCliTests, NonDryRunTestEmitsWorkflowSuccessPayload)
{
  const fs::path root = MakeTempDir("test-success-payload");
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
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", ">_(\"smoke\")\n");

  const fs::path fake_styio = WriteFakeCompilePlanStyio(root, "fake-styio-test");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "test",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "test");
  EXPECT_EQ(payload.at("mode").get<std::string>(), "execute");
  EXPECT_EQ(payload.at("workflow_payload_version").get<int>(), 1);
  EXPECT_EQ(payload.at("intent").get<std::string>(), "test");
  ASSERT_TRUE(payload.at("receipt").is_object());
  EXPECT_EQ(payload.at("receipt").at("intent").get<std::string>(), "test");
  EXPECT_EQ(payload.at("receipt").at("executed").get<bool>(), true);
  EXPECT_EQ(payload.at("receipt").at("session_id").get<std::string>(), "fake-runtime-session");
  ASSERT_TRUE(payload.at("diagnostics").is_array());
  EXPECT_TRUE(payload.at("diagnostics").empty());
  EXPECT_EQ(payload.at("runtime_session_id").get<std::string>(), "fake-runtime-session");
  ASSERT_TRUE(payload.at("runtime_events").is_array());
  EXPECT_EQ(payload.at("runtime_events").size(), 8U);
  EXPECT_EQ(payload.at("runtime_events")[1].at("eventKind").get<std::string>(), "unit.entered");
  EXPECT_EQ(payload.at("runtime_events")[2].at("eventKind").get<std::string>(), "unit.test.started");
  EXPECT_EQ(payload.at("runtime_events")[3].at("eventKind").get<std::string>(), "run.started");
  EXPECT_EQ(payload.at("runtime_events")[6].at("eventKind").get<std::string>(), "unit.test.finished");
  EXPECT_EQ(payload.at("runtime_events")[7].at("eventKind").get<std::string>(), "run.finished");
  EXPECT_EQ(payload.at("runtime_events")[2].at("payload").at("test_name").get<std::string>(), "smoke");
  EXPECT_EQ(payload.at("runtime_events")[6].at("payload").at("test_name").get<std::string>(), "smoke");
  EXPECT_NE(payload.at("stdout").get<std::string>().find("spio-test-ok"), std::string::npos);
  EXPECT_TRUE(payload.at("stderr").get<std::string>().empty());
  ASSERT_TRUE(payload.at("styio").is_object());
  EXPECT_EQ(payload.at("styio").at("integration_phase").get<std::string>(), "compile-plan-live");
}

TEST(BuildCliTests, CompilerFailurePublishesStructuredDiagnosticsPayload)
{
  const fs::path root = MakeTempDir("build-failure-payload");
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

  const fs::path fake_styio = WriteFailingCompilePlanStyio(root, "fake-styio-failure");

  testing::internal::CaptureStderr();
  const int exit_code = spio::RunCli({
      "--json",
      "build",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
      "--bin",
      "app",
  });
  const std::string stderr_text = testing::internal::GetCapturedStderr();

  ASSERT_EQ(exit_code, spio::kExitCompiler);
  const json payload = json::parse(stderr_text);
  EXPECT_EQ(payload.at("category").get<std::string>(), "CompilerError");
  EXPECT_EQ(payload.at("command").get<std::string>(), "build");
  EXPECT_EQ(payload.at("child_program").get<std::string>(), fake_styio.string());
  EXPECT_EQ(payload.at("child_exit_code").get<int>(), 17);
  EXPECT_EQ(payload.at("intent").get<std::string>(), "build");
  ASSERT_TRUE(payload.at("receipt").is_object());
  EXPECT_EQ(payload.at("receipt").at("intent").get<std::string>(), "build");
  EXPECT_EQ(payload.at("receipt").at("session_id").get<std::string>(), "fake-runtime-session-failure");
  ASSERT_TRUE(payload.at("diagnostics").is_array());
  ASSERT_EQ(payload.at("diagnostics").size(), 1U);
  EXPECT_EQ(payload.at("diagnostics")[0].at("code").get<std::string>(), "fake.compile-plan.failure");
  EXPECT_EQ(payload.at("diagnostics")[0].at("message").get<std::string>(), "fake compile-plan failure");
  EXPECT_EQ(payload.at("runtime_session_id").get<std::string>(), "fake-runtime-session-failure");
  EXPECT_NE(payload.at("runtime_events_path").get<std::string>().find("runtime-events.jsonl"), std::string::npos);
  ASSERT_TRUE(payload.at("runtime_events").is_array());
  EXPECT_EQ(payload.at("runtime_events").size(), 5U);
  EXPECT_EQ(payload.at("runtime_events")[1].at("eventKind").get<std::string>(), "unit.entered");
  EXPECT_EQ(payload.at("runtime_events")[2].at("eventKind").get<std::string>(), "diagnostic.emitted");
  EXPECT_EQ(payload.at("runtime_events")[3].at("eventKind").get<std::string>(), "unit.exited");
  EXPECT_EQ(payload.at("runtime_events")[4].at("eventKind").get<std::string>(), "compile.failed");
  EXPECT_EQ(payload.at("stderr").get<std::string>(), "fake compile-plan failure\n");

  const fs::path diagnostics_path = payload.at("diagnostics_path").get<std::string>();
  EXPECT_TRUE(fs::exists(diagnostics_path));
  const fs::path receipt_path = payload.at("receipt_path").get<std::string>();
  EXPECT_TRUE(fs::exists(receipt_path));
  const fs::path runtime_events_path = payload.at("runtime_events_path").get<std::string>();
  EXPECT_TRUE(fs::exists(runtime_events_path));
}

TEST(TestCliTests, DryRunEmitsTestIntentForUniqueTestTarget)
{
  const fs::path root = MakeTempDir("test-dry-run");
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
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", "# smoke := true\n");

  const spio::BuildPlanResult result = spio::WriteBuildCompilePlan({
      .manifest_path = root / "spio.toml",
      .intent = "test",
  });
  const json plan = json::parse(ReadFile(result.plan_path));
  EXPECT_EQ(plan["intent"], "test");
  EXPECT_EQ(plan["entry"]["target_kind"], "test");
  EXPECT_EQ(plan["entry"]["target_name"], "smoke");
  ASSERT_TRUE(plan["packages"][0]["targets"].contains("tests"));
  EXPECT_EQ(plan["packages"][0]["targets"]["tests"][0]["name"], "smoke");
}

TEST(TestCliTests, RejectsBinSelection)
{
  const fs::path root = MakeTempDir("test-rejects-bin");
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
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", "# smoke := true\n");

  const int exit_code = spio::RunCli({
      "--json",
      "test",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--bin",
      "app",
      "--dry-run",
  });
  EXPECT_EQ(exit_code, spio::kExitUsage);
}

TEST(TestCliTests, NonDryRunTestIsBlockedByPublishedCompatibilityPhase)
{
  const fs::path root = MakeTempDir("test-contract-gate");
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
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n");
  WriteFile(root / "tests/smoke.styio", "# smoke := true\n");

  const fs::path fake_styio = root / "fake-styio";
  WriteExecutable(
      fake_styio,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"active_integration_phase\":\"bootstrap-single-file\",\"supported_contracts\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[],\"runtime_events\":[]},\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":false,\"project_execution_via_compile_plan\":false,\"runtime_event_stream\":false},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n");

  const int exit_code = spio::RunCli({
      "test",
      "--manifest-path",
      (root / "spio.toml").string(),
      "--styio-bin",
      fake_styio.string(),
  });
  EXPECT_EQ(exit_code, spio::kExitContract);
}
